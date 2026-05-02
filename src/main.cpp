#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "app_config.hpp"
#include "dap_session.hpp"
#include "debug_session.hpp"
#include "memory_selection.hpp"
#include "memory_view.hpp"
#include "pane_rows.hpp"
#include "pane_state.hpp"

namespace {

    enum class ePromptMode : std::uint8_t {
        NONE,
        ADD_WATCH,
        EDIT_WATCH,
        MEMORY_TARGET,
    };

    struct SPromptState {
        ePromptMode mode = ePromptMode::NONE;
        std::string input;
    };

    struct SSessionBootstrapResult {
        std::unique_ptr<IDebugSession> session;
        SDebugSelection                selection;
        std::uint64_t                  disassembly_start_address = 0x401000;
        std::string                    disassembly_memory_reference;
        std::string                    status_message;
    };

    ftxui::Element renderSelectablePane(const SSelectablePaneState& pane, bool is_focused) {
        using namespace ftxui;

        Elements rows;
        Element  title = text(pane.title) | bold;
        if (is_focused) {
            title = title | inverted;
        }

        rows.push_back(title);
        rows.push_back(separator());

        if (pane.rows.empty()) {
            rows.push_back(text("(empty)"));
        } else {
            for (std::size_t i = 0; i < pane.rows.size(); ++i) {
                Element data_row = text(pane.rows[i]);
                if (is_focused && pane.selected_index == i) {
                    rows.push_back(data_row | inverted);
                } else {
                    rows.push_back(data_row);
                }
            }
        }

        return vbox(rows) | border;
    }

    std::vector<std::string> buildLeaderHints(const std::vector<SKeybinding>& keybindings) {
        std::vector<std::string> hints;
        hints.reserve(keybindings.size());

        for (const auto& keybinding : keybindings) {
            if (!keybinding.keys.starts_with("Space ") || keybinding.keys.size() <= 6) {
                continue;
            }

            hints.push_back(keybinding.keys.substr(6) + " " + commandDescription(keybinding.command));
        }

        return hints;
    }

    std::optional<eCommand> findCommandForKeys(const std::vector<SKeybinding>& keybindings, const std::string& keys) {
        const auto keybinding_iterator = std::ranges::find_if(keybindings, [&](const SKeybinding& keybinding) { return keybinding.keys == keys; });
        if (keybinding_iterator == keybindings.end()) {
            return std::nullopt;
        }

        return keybinding_iterator->command;
    }

    ftxui::Element renderShortcutsOverlay(const std::vector<SKeybinding>& keybindings) {
        using namespace ftxui;

        Elements rows = {
            text(" Roundtable Shortcuts ") | bold, separator(), text("Tab  Cycle focus"), text("r  Refresh panes"), text("q  Quit"),
        };

        for (const auto& keybinding : keybindings) {
            rows.push_back(text(keybinding.keys + "  " + commandDescription(keybinding.command)));
        }

        return window(text(" Shortcuts "), vbox(rows)) | size(WIDTH, GREATER_THAN, 48);
    }

    ftxui::Element renderPromptOverlay(const SPromptState& prompt_state) {
        using namespace ftxui;

        std::string title;
        std::string hint;
        switch (prompt_state.mode) {
            case ePromptMode::ADD_WATCH:
                title = " Add Watch ";
                hint  = "Enter expression and press Return";
                break;
            case ePromptMode::EDIT_WATCH:
                title = " Edit Watch ";
                hint  = "Update expression and press Return";
                break;
            case ePromptMode::MEMORY_TARGET:
                title = " Memory Target ";
                hint  = "Enter address or expression, empty clears override";
                break;
            case ePromptMode::NONE: return text("");
        }

        return window(text(title),
                      vbox({
                          text(hint),
                          separator(),
                          text("> " + prompt_state.input),
                      })) |
            size(WIDTH, GREATER_THAN, 48);
    }

    ftxui::Element renderAuxiliaryViews(const SViewVisibilityState& view_visibility, const SSelectablePaneState& memory_view_pane, const SSelectablePaneState& disassembly_pane,
                                        eFocusPane focused_pane) {
        using namespace ftxui;

        const bool show_memory       = view_visibility.show_memory_view;
        const bool show_disassembly  = view_visibility.show_disassembly_view;
        const bool memory_is_focused = focused_pane == eFocusPane::MEMORY_VIEW;
        const bool disasm_is_focused = focused_pane == eFocusPane::DISASSEMBLY_VIEW;

        if (show_memory && show_disassembly) {
            return hbox({
                       renderSelectablePane(memory_view_pane, memory_is_focused) | flex,
                       renderSelectablePane(disassembly_pane, disasm_is_focused) | flex,
                   }) |
                flex;
        }

        if (show_memory) {
            return renderSelectablePane(memory_view_pane, memory_is_focused) | flex;
        }

        if (show_disassembly) {
            return renderSelectablePane(disassembly_pane, disasm_is_focused) | flex;
        }

        return renderSelectablePane(
                   {
                       .title = " Views ",
                       .rows  = {"Enable Memory or Disassembly with Space t / Space a"},
                   },
                   false) |
            flex;
    }

    SSessionBootstrapResult bootstrapSession(const SAppConfig& app_config) {
        if (app_config.session_mode != eSessionMode::DAP_LAUNCH) {
            return {
                .session                      = std::make_unique<CMockDebugSession>(),
                .selection                    = {},
                .disassembly_start_address    = 0x401000,
                .disassembly_memory_reference = "",
                .status_message               = "Mock session",
            };
        }

        auto dap_session = std::make_unique<CDapDebugSession>(std::make_unique<CTcpDapTransport>(),
                                                              SDapEndpointConfig{
                                                                  .transport_kind = eDapTransportKind::TCP,
                                                                  .command        = app_config.dap_launch.command,
                                                                  .arguments      = {"--liblldb", app_config.dap_launch.liblldb_path},
                                                                  .auth_token     = "",
                                                              });

        if (app_config.dap_launch.command.empty() || app_config.dap_launch.liblldb_path.empty() || app_config.dap_launch.program.empty()) {
            return {
                .session                      = std::move(dap_session),
                .selection                    = {},
                .disassembly_start_address    = 0x401000,
                .disassembly_memory_reference = "",
                .status_message               = "DAP launch config is incomplete",
            };
        }

        if (!dap_session->connect()) {
            return {
                .session                      = std::move(dap_session),
                .selection                    = {},
                .disassembly_start_address    = 0x401000,
                .disassembly_memory_reference = "",
                .status_message               = "DAP connect failed: " + dap_session->getLastError(),
            };
        }

        if (!dap_session->initialize()) {
            return {
                .session                      = std::move(dap_session),
                .selection                    = {},
                .disassembly_start_address    = 0x401000,
                .disassembly_memory_reference = "",
                .status_message               = "DAP initialize failed: " + dap_session->getLastError(),
            };
        }

        if (!dap_session->launch({
                .program           = app_config.dap_launch.program,
                .arguments         = {},
                .working_directory = app_config.dap_launch.working_directory,
                .stop_on_entry     = app_config.dap_launch.stop_on_entry,
            })) {
            return {
                .session                      = std::move(dap_session),
                .selection                    = {},
                .disassembly_start_address    = 0x401000,
                .disassembly_memory_reference = "",
                .status_message               = "DAP launch failed: " + dap_session->getLastError(),
            };
        }

        if (!dap_session->configurationDone()) {
            return {
                .session                      = std::move(dap_session),
                .selection                    = {},
                .disassembly_start_address    = 0x401000,
                .disassembly_memory_reference = "",
                .status_message               = "DAP configurationDone failed: " + dap_session->getLastError(),
            };
        }

        if (!dap_session->waitForStoppedEvent()) {
            return {
                .session                      = std::move(dap_session),
                .selection                    = {},
                .disassembly_start_address    = 0x401000,
                .disassembly_memory_reference = "",
                .status_message               = "DAP waitForStoppedEvent failed: " + dap_session->getLastError(),
            };
        }

        SDebugSelection selection = {};
        auto            threads   = dap_session->getThreads();
        if (threads.success && !threads.threads.empty()) {
            selection.thread_id = threads.threads.front().id;
        }

        if (app_config.dap_launch.continue_once && selection.thread_id != 0) {
            const auto continue_response = dap_session->continueExecution({
                .thread_id = static_cast<int>(selection.thread_id),
            });

            if (!continue_response.success) {
                return {
                    .session                      = std::move(dap_session),
                    .selection                    = selection,
                    .disassembly_start_address    = 0x401000,
                    .disassembly_memory_reference = "",
                    .status_message               = "DAP continue failed: " + continue_response.error_message,
                };
            }

            if (!dap_session->waitForStoppedEvent()) {
                return {
                    .session                      = std::move(dap_session),
                    .selection                    = selection,
                    .disassembly_start_address    = 0x401000,
                    .disassembly_memory_reference = "",
                    .status_message               = "DAP wait after continue failed: " + dap_session->getLastError(),
                };
            }

            threads = dap_session->getThreads();
            if (threads.success && !threads.threads.empty()) {
                selection.thread_id = threads.threads.front().id;
            }
        }

        std::uint64_t disassembly_start_address = 0x401000;
        std::string   disassembly_memory_reference;
        if (selection.thread_id != 0) {
            const auto stack_trace = dap_session->getStackTrace({
                .thread_id   = static_cast<int>(selection.thread_id),
                .start_frame = 0,
                .levels      = 16,
            });

            if (stack_trace.success && !stack_trace.stack_frames.empty()) {
                const auto main_frame_iterator     = std::ranges::find_if(stack_trace.stack_frames, [](const SDapStackFrame& frame) { return frame.name == "main"; });
                const auto selected_frame_iterator = main_frame_iterator != stack_trace.stack_frames.end() ? main_frame_iterator : stack_trace.stack_frames.begin();

                selection.frame_index = static_cast<std::size_t>(std::distance(stack_trace.stack_frames.begin(), selected_frame_iterator));

                if (!selected_frame_iterator->instruction_pointer_reference.empty()) {
                    disassembly_memory_reference = selected_frame_iterator->instruction_pointer_reference;
                    try {
                        disassembly_start_address = std::stoull(selected_frame_iterator->instruction_pointer_reference, nullptr, 0);
                    } catch (const std::exception&) { disassembly_start_address = 0x401000; }
                }
            }
        }

        return {
            .session                      = std::move(dap_session),
            .selection                    = selection,
            .disassembly_start_address    = disassembly_start_address,
            .disassembly_memory_reference = disassembly_memory_reference,
            .status_message               = selection.thread_id != 0 ? "DAP launch session" : "DAP launch session without active thread",
        };
    }

    void refreshPaneRows(IDebugSession& debug_session, const SDebugSelection& debug_selection, SSelectablePaneState& locals_pane, SSelectablePaneState& memory_view_pane,
                         SSelectablePaneState& disassembly_pane, SSelectablePaneState& watch_list_pane, std::uint64_t disassembly_start_address,
                         const std::string& disassembly_memory_reference, eFocusPane focused_pane, const std::vector<SWatchExpression>& watch_expressions,
                         const std::string& manual_memory_target) {
        const auto locals        = debug_session.getLocals(debug_selection);
        const auto watch_results = debug_session.evaluateWatches(debug_selection, watch_expressions);
        locals_pane.rows         = formatLocalsPaneRows(locals);
        watch_list_pane.rows     = formatWatchListPaneRows(watch_results);

        if (!manual_memory_target.empty()) {
            if (const auto direct_address = findFirstHexAddress(manual_memory_target); direct_address.has_value()) {
                const auto memory_read_result = debug_session.readMemory(debug_selection,
                                                                         {
                                                                             .start_address    = direct_address.value(),
                                                                             .memory_reference = "",
                                                                             .byte_count       = 40,
                                                                             .bytes_per_row    = 8,
                                                                         });
                memory_view_pane.rows         = generateMemoryViewRows(memory_read_result);
            } else {
                const std::vector<SWatchResult> memory_target_results = debug_session.evaluateWatches(debug_selection, {{.expression = manual_memory_target}});
                const auto                      memory_read_request   = buildMemoryReadRequest(memory_target_results, 0, disassembly_start_address, disassembly_memory_reference);
                const auto                      memory_read_result    = debug_session.readMemory(debug_selection, memory_read_request);
                const auto                      synthetic_memory_rows = buildSyntheticMemoryRows(memory_target_results, 0, memory_read_request.bytes_per_row);
                const bool                      target_has_explicit_memory_reference = !memory_target_results.empty() && !memory_target_results.front().memory_reference.empty() &&
                    findFirstHexAddress(memory_target_results.front().memory_reference).has_value();

                if (synthetic_memory_rows.has_value() && !target_has_explicit_memory_reference) {
                    memory_view_pane.rows = synthetic_memory_rows.value();
                } else if (!memory_read_result.error_message.empty() && synthetic_memory_rows.has_value()) {
                    memory_view_pane.rows = synthetic_memory_rows.value();
                } else {
                    memory_view_pane.rows = generateMemoryViewRows(memory_read_result);
                }
            }
        } else if (focused_pane == eFocusPane::WATCH_LIST && !watch_results.empty()) {
            const auto memory_read_request   = buildMemoryReadRequest(watch_results, watch_list_pane.selected_index, disassembly_start_address, disassembly_memory_reference);
            const auto memory_read_result    = debug_session.readMemory(debug_selection, memory_read_request);
            const auto synthetic_memory_rows = buildSyntheticMemoryRows(watch_results, watch_list_pane.selected_index, memory_read_request.bytes_per_row);
            const auto selected_watch_index  = std::min(watch_list_pane.selected_index, watch_results.size() - 1);
            const bool selected_watch_has_explicit_memory_reference =
                !watch_results[selected_watch_index].memory_reference.empty() && findFirstHexAddress(watch_results[selected_watch_index].memory_reference).has_value();

            if (synthetic_memory_rows.has_value() && !selected_watch_has_explicit_memory_reference) {
                memory_view_pane.rows = synthetic_memory_rows.value();
            } else if (!memory_read_result.error_message.empty() && synthetic_memory_rows.has_value()) {
                memory_view_pane.rows = synthetic_memory_rows.value();
            } else {
                memory_view_pane.rows = generateMemoryViewRows(memory_read_result);
            }
        } else {
            const auto memory_read_request =
                buildMemoryReadRequest(debug_session, debug_selection, locals, locals_pane.selected_index, disassembly_start_address, disassembly_memory_reference);
            const auto memory_read_result    = debug_session.readMemory(debug_selection, memory_read_request);
            const auto synthetic_memory_rows = buildSyntheticMemoryRows(locals, locals_pane.selected_index, memory_read_request.bytes_per_row);
            const auto selected_local_index  = locals.empty() ? 0UL : std::min(locals_pane.selected_index, locals.size() - 1);
            const bool selected_local_has_explicit_memory_reference =
                !locals.empty() && !locals[selected_local_index].memory_reference.empty() && findFirstHexAddress(locals[selected_local_index].memory_reference).has_value();

            if (synthetic_memory_rows.has_value() && !selected_local_has_explicit_memory_reference) {
                memory_view_pane.rows = synthetic_memory_rows.value();
            } else if (!memory_read_result.error_message.empty() && synthetic_memory_rows.has_value()) {
                memory_view_pane.rows = synthetic_memory_rows.value();
            } else {
                memory_view_pane.rows = generateMemoryViewRows(memory_read_result);
            }
        }

        disassembly_pane.rows = formatDisassemblyPaneRows(debug_session.disassemble(debug_selection, disassembly_start_address, 8));

        locals_pane.selected_index      = std::min(locals_pane.selected_index, locals_pane.rows.empty() ? 0UL : locals_pane.rows.size() - 1);
        memory_view_pane.selected_index = std::min(memory_view_pane.selected_index, memory_view_pane.rows.empty() ? 0UL : memory_view_pane.rows.size() - 1);
        disassembly_pane.selected_index = std::min(disassembly_pane.selected_index, disassembly_pane.rows.empty() ? 0UL : disassembly_pane.rows.size() - 1);
        watch_list_pane.selected_index  = std::min(watch_list_pane.selected_index, watch_list_pane.rows.empty() ? 0UL : watch_list_pane.rows.size() - 1);
    }

} // namespace

int main() {
    using namespace ftxui;

    const SAppConfig        app_config                   = loadAppConfig("roundtable.toml");
    SSessionBootstrapResult bootstrap_result             = bootstrapSession(app_config);
    auto&                   debug_session                = *bootstrap_result.session;
    SDebugSelection         debug_selection              = bootstrap_result.selection;
    std::uint64_t           disassembly_start_address    = bootstrap_result.disassembly_start_address;
    std::string             disassembly_memory_reference = bootstrap_result.disassembly_memory_reference;
    std::string             session_status               = bootstrap_result.status_message;
    auto                    screen                       = ScreenInteractive::Fullscreen();
    SViewVisibilityState    view_visibility              = {
                        .show_memory_view      = app_config.show_memory_view,
                        .show_disassembly_view = app_config.show_disassembly_view,
    };
    eFocusPane                    focused_pane      = normalizeFocusedPane(app_config.startup_focus, view_visibility);
    const auto                    keybindings       = app_config.keybindings;
    bool                          leader_pending    = false;
    std::vector<SWatchExpression> watch_expressions = {
        {.expression = "sample_value"},
        {.expression = "sample_bytes"},
    };
    std::string          manual_memory_target = {};
    SPromptState         prompt_state         = {};

    SSelectablePaneState locals_pane = {
        .title = " Locals ",
        .rows  = {},
    };
    SSelectablePaneState memory_view_pane = {
        .title = " Memory View ",
        .rows  = {},
    };
    SSelectablePaneState disassembly_pane = {
        .title = " Disassembly ",
        .rows  = {},
    };
    SSelectablePaneState watch_list_pane = {
        .title = " Watch List ",
        .rows  = {},
    };

    refreshPaneRows(debug_session, debug_selection, locals_pane, memory_view_pane, disassembly_pane, watch_list_pane, disassembly_start_address, disassembly_memory_reference,
                    focused_pane, watch_expressions, manual_memory_target);

    auto renderer = Renderer([&] {
        Element  locals          = renderSelectablePane(locals_pane, focused_pane == eFocusPane::LOCALS);
        Element  watch_list      = renderSelectablePane(watch_list_pane, focused_pane == eFocusPane::WATCH_LIST);
        Element  auxiliary_views = renderAuxiliaryViews(view_visibility, memory_view_pane, disassembly_pane, focused_pane);

        Elements status_items = {
            text(" Roundtable ") | inverted,
            separator(),
            text(" " + session_status + " "),
            separator(),
            text(" Tab cycle "),
            separator(),
            text(" r refresh "),
            separator(),
            text(" Space commands "),
            separator(),
            text(" q quit "),
        };

        if (leader_pending) {
            const auto hints = buildLeaderHints(keybindings);
            for (const auto& hint : hints) {
                status_items.push_back(separator());
                status_items.push_back(text(hint));
            }
        }

        Element content = vbox({
            hbox({
                locals | size(WIDTH, EQUAL, 28),
                auxiliary_views | flex,
                watch_list | size(WIDTH, EQUAL, 28),
            }) | flex,
            hbox(status_items) | border,
        });

        if (view_visibility.show_shortcuts_overlay) {
            content = dbox({
                content,
                renderShortcutsOverlay(keybindings) | center,
            });
        }

        if (prompt_state.mode != ePromptMode::NONE) {
            content = dbox({
                content,
                renderPromptOverlay(prompt_state) | center,
            });
        }

        return content;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (prompt_state.mode != ePromptMode::NONE) {
            if (event == Event::Escape) {
                prompt_state = {};
                return true;
            }

            if (event == Event::Return) {
                if (prompt_state.mode == ePromptMode::ADD_WATCH) {
                    if (!prompt_state.input.empty()) {
                        watch_expressions.push_back({.expression = prompt_state.input});
                        focused_pane = eFocusPane::WATCH_LIST;
                    }
                } else if (prompt_state.mode == ePromptMode::EDIT_WATCH) {
                    if (!watch_expressions.empty() && watch_list_pane.selected_index < watch_expressions.size() && !prompt_state.input.empty()) {
                        watch_expressions[watch_list_pane.selected_index].expression = prompt_state.input;
                        focused_pane                                                 = eFocusPane::WATCH_LIST;
                    }
                } else if (prompt_state.mode == ePromptMode::MEMORY_TARGET) {
                    manual_memory_target = prompt_state.input;
                    focused_pane         = eFocusPane::MEMORY_VIEW;
                }

                prompt_state = {};
                refreshPaneRows(debug_session, debug_selection, locals_pane, memory_view_pane, disassembly_pane, watch_list_pane, disassembly_start_address,
                                disassembly_memory_reference, focused_pane, watch_expressions, manual_memory_target);
                return true;
            }

            if (event == Event::Backspace) {
                if (!prompt_state.input.empty()) {
                    prompt_state.input.pop_back();
                }
                return true;
            }

            if (event.is_character()) {
                prompt_state.input += event.character();
                return true;
            }

            return true;
        }

        if (event == Event::Character('q')) {
            screen.Exit();
            return true;
        }

        if (event == Event::Character('r')) {
            refreshPaneRows(debug_session, debug_selection, locals_pane, memory_view_pane, disassembly_pane, watch_list_pane, disassembly_start_address,
                            disassembly_memory_reference, focused_pane, watch_expressions, manual_memory_target);
            return true;
        }

        if (view_visibility.show_shortcuts_overlay && (event == Event::Escape || event == Event::Character('?'))) {
            view_visibility.show_shortcuts_overlay = false;
            leader_pending                         = false;
            return true;
        }

        if (leader_pending) {
            leader_pending = false;

            if (event == Event::Escape) {
                return true;
            }

            if (event.is_character()) {
                const auto command = findCommandForKeys(keybindings, "Space " + event.character());
                if (command.has_value()) {
                    if (command.value() == eCommand::ADD_WATCH) {
                        prompt_state = {
                            .mode  = ePromptMode::ADD_WATCH,
                            .input = "",
                        };
                        return true;
                    }
                    if (command.value() == eCommand::EDIT_WATCH) {
                        if (!watch_expressions.empty() && watch_list_pane.selected_index < watch_expressions.size()) {
                            prompt_state = {
                                .mode  = ePromptMode::EDIT_WATCH,
                                .input = watch_expressions[watch_list_pane.selected_index].expression,
                            };
                            return true;
                        }
                        return true;
                    }
                    if (command.value() == eCommand::REMOVE_WATCH) {
                        if (!watch_expressions.empty() && watch_list_pane.selected_index < watch_expressions.size()) {
                            watch_expressions.erase(watch_expressions.begin() + static_cast<std::ptrdiff_t>(watch_list_pane.selected_index));
                            if (watch_list_pane.selected_index > 0 && watch_list_pane.selected_index >= watch_expressions.size()) {
                                --watch_list_pane.selected_index;
                            }
                            focused_pane = eFocusPane::WATCH_LIST;
                            refreshPaneRows(debug_session, debug_selection, locals_pane, memory_view_pane, disassembly_pane, watch_list_pane, disassembly_start_address,
                                            disassembly_memory_reference, focused_pane, watch_expressions, manual_memory_target);
                        }
                        return true;
                    }
                    if (command.value() == eCommand::SET_MEMORY_TARGET) {
                        prompt_state = {
                            .mode  = ePromptMode::MEMORY_TARGET,
                            .input = manual_memory_target,
                        };
                        return true;
                    }
                    executeCommand(command.value(), focused_pane, view_visibility);
                    refreshPaneRows(debug_session, debug_selection, locals_pane, memory_view_pane, disassembly_pane, watch_list_pane, disassembly_start_address,
                                    disassembly_memory_reference, focused_pane, watch_expressions, manual_memory_target);
                    return true;
                }
            }

            return true;
        }

        if (event == Event::Character(' ')) {
            leader_pending = true;
            return true;
        }

        if (event == Event::Tab) {
            focused_pane = advanceFocusPane(focused_pane, view_visibility);
            refreshPaneRows(debug_session, debug_selection, locals_pane, memory_view_pane, disassembly_pane, watch_list_pane, disassembly_start_address,
                            disassembly_memory_reference, focused_pane, watch_expressions, manual_memory_target);
            return true;
        }

        if (focused_pane == eFocusPane::LOCALS) {
            const bool handled = handleVerticalNavigation(event, locals_pane);
            if (handled) {
                refreshPaneRows(debug_session, debug_selection, locals_pane, memory_view_pane, disassembly_pane, watch_list_pane, disassembly_start_address,
                                disassembly_memory_reference, focused_pane, watch_expressions, manual_memory_target);
            }
            return handled;
        }
        if (focused_pane == eFocusPane::WATCH_LIST) {
            const bool handled = handleVerticalNavigation(event, watch_list_pane);
            if (handled) {
                refreshPaneRows(debug_session, debug_selection, locals_pane, memory_view_pane, disassembly_pane, watch_list_pane, disassembly_start_address,
                                disassembly_memory_reference, focused_pane, watch_expressions, manual_memory_target);
            }
            return handled;
        }
        if (focused_pane == eFocusPane::MEMORY_VIEW) {
            return handleVerticalNavigation(event, memory_view_pane);
        }
        if (focused_pane == eFocusPane::DISASSEMBLY_VIEW) {
            return handleVerticalNavigation(event, disassembly_pane);
        }

        return false;
    });

    screen.Loop(component);
}
