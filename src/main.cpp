#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <tuple>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "app_config.hpp"
#include "app_theme.hpp"
#include "dap_session.hpp"
#include "debug_session.hpp"
#include "pane_refresh.hpp"
#include "pane_state.hpp"

namespace {

    constexpr std::array<eThemePreset, 4> kThemePresets = {
        eThemePreset::DEFAULT,
        eThemePreset::AMBER,
        eThemePreset::ICE,
        eThemePreset::FOREST,
    };

    enum class ePromptMode : std::uint8_t {
        NONE,
        ADD_WATCH,
        EDIT_WATCH,
        MEMORY_TARGET,
    };

    enum class eWatchActionMode : std::uint8_t {
        NONE,
        EDIT,
        REMOVE,
    };

    struct SPromptState {
        ePromptMode mode = ePromptMode::NONE;
        std::string input;
        std::size_t cursor_index     = 0;
        bool        replace_on_input = false;
    };

    struct SThemePickerState {
        bool         active          = false;
        eThemePreset original_preset = eThemePreset::DEFAULT;
        std::size_t  selected_index  = 0;
    };

    SPromptState beginPrompt(ePromptMode mode, std::string initial_input = "", bool replace_on_input = false) {
        return {
            .mode             = mode,
            .input            = std::move(initial_input),
            .cursor_index     = initial_input.size(),
            .replace_on_input = replace_on_input,
        };
    }

    std::string buildPromptDisplay(const SPromptState& prompt_state) {
        std::string display_input = prompt_state.input;
        const auto  cursor_index  = std::min(prompt_state.cursor_index, display_input.size());
        display_input.insert(cursor_index, "|");
        return "> " + display_input;
    }

    std::string themePresetName(eThemePreset preset) {
        switch (preset) {
            case eThemePreset::DEFAULT: return "default";
            case eThemePreset::AMBER: return "amber";
            case eThemePreset::ICE: return "ice";
            case eThemePreset::FOREST: return "forest";
        }

        return "default";
    }

    std::size_t themePresetIndex(eThemePreset preset) {
        for (std::size_t index = 0; index < kThemePresets.size(); ++index) {
            if (kThemePresets[index] == preset) {
                return index;
            }
        }

        return 0;
    }

    struct SSessionBootstrapResult {
        std::unique_ptr<IDebugSession> session;
        SDebugSelection                selection;
        std::uint64_t                  disassembly_start_address = 0x401000;
        std::string                    disassembly_memory_reference;
        std::string                    status_message;
    };

    struct SLeaderHintRow {
        std::string keys;
        std::string description;
        eCommand    command       = eCommand::FOCUS_MEMORY;
        bool        pane_specific = false;
    };

    int commandPriority(eCommand command, eFocusPane focused_pane) {
        switch (focused_pane) {
            case eFocusPane::WATCH_LIST:
                if (command == eCommand::EDIT_WATCH || command == eCommand::REMOVE_WATCH || command == eCommand::ADD_WATCH) {
                    return 0;
                }
                if (command == eCommand::SET_MEMORY_TARGET) {
                    return 1;
                }
                break;
            case eFocusPane::LOCALS:
                if (command == eCommand::SET_MEMORY_TARGET || command == eCommand::FOCUS_MEMORY) {
                    return 1;
                }
                break;
            case eFocusPane::MEMORY_VIEW:
                if (command == eCommand::SET_MEMORY_TARGET || command == eCommand::TOGGLE_DISASSEMBLY) {
                    return 0;
                }
                break;
            case eFocusPane::DISASSEMBLY_VIEW:
                if (command == eCommand::TOGGLE_MEMORY || command == eCommand::FOCUS_MEMORY) {
                    return 0;
                }
                break;
        }

        return 2;
    }

    bool isPaneSpecificCommand(eCommand command, eFocusPane focused_pane) {
        return commandPriority(command, focused_pane) == 0;
    }

    std::vector<SLeaderHintRow> buildLeaderHintRows(const std::vector<SKeybinding>& keybindings, eFocusPane focused_pane) {
        std::vector<SLeaderHintRow> hints;
        hints.reserve(keybindings.size());

        for (const auto& keybinding : keybindings) {
            if (!keybinding.keys.starts_with("Space ") || keybinding.keys.size() <= 6) {
                continue;
            }

            hints.push_back({
                .keys          = keybinding.keys.substr(6),
                .description   = commandDescription(keybinding.command),
                .command       = keybinding.command,
                .pane_specific = isPaneSpecificCommand(keybinding.command, focused_pane),
            });
        }

        std::ranges::stable_sort(hints, [&](const SLeaderHintRow& left, const SLeaderHintRow& right) {
            return commandPriority(left.command, focused_pane) < commandPriority(right.command, focused_pane);
        });

        return hints;
    }

    std::optional<std::tuple<std::string, std::string, std::string>> splitMemoryRow(const std::string& row) {
        const auto first_separator = row.find("  ");
        if (first_separator == std::string::npos) {
            return std::nullopt;
        }

        const auto second_separator = row.find("  ", first_separator + 2);
        if (second_separator == std::string::npos) {
            return std::nullopt;
        }

        return std::make_tuple(row.substr(0, first_separator), row.substr(first_separator + 2, second_separator - (first_separator + 2)), row.substr(second_separator + 2));
    }

    std::optional<std::tuple<std::string, std::string, std::string>> splitLocalRow(const std::string& row) {
        const auto type_separator = row.find(" : ");
        if (type_separator == std::string::npos) {
            return std::nullopt;
        }

        const auto value_separator = row.find(" = ", type_separator + 3);
        if (value_separator == std::string::npos) {
            return std::nullopt;
        }

        return std::make_tuple(row.substr(0, type_separator), row.substr(type_separator + 3, value_separator - (type_separator + 3)), row.substr(value_separator + 3));
    }

    std::optional<std::tuple<std::string, std::string, std::string, bool>> splitWatchRow(const std::string& row) {
        const auto value_separator = row.find(" = ");
        if (value_separator != std::string::npos) {
            const auto type_separator = row.rfind(" : ");
            if (type_separator != std::string::npos && type_separator > value_separator) {
                return std::make_tuple(row.substr(0, value_separator), row.substr(value_separator + 3, type_separator - (value_separator + 3)), row.substr(type_separator + 3),
                                       true);
            }
        }

        const auto error_separator = row.find(" : ");
        if (error_separator == std::string::npos) {
            return std::nullopt;
        }

        return std::make_tuple(row.substr(0, error_separator), row.substr(error_separator + 3), std::string{}, false);
    }

    std::vector<std::string> splitMemoryByteTokens(const std::string& hex_bytes) {
        std::vector<std::string> tokens;
        std::size_t              token_start = 0;

        while (token_start < hex_bytes.size()) {
            const auto separator = hex_bytes.find(' ', token_start);
            if (separator == std::string::npos) {
                tokens.push_back(hex_bytes.substr(token_start));
                break;
            }

            if (separator > token_start) {
                tokens.push_back(hex_bytes.substr(token_start, separator - token_start));
            }

            token_start = separator + 1;
        }

        return tokens;
    }

    bool isHighlightedMemoryByte(const SMemoryByteHighlight& highlight, std::size_t row_index, std::size_t byte_index) {
        if (highlight.byte_count == 0) {
            return false;
        }

        if (highlight.synthetic) {
            const std::size_t absolute_offset = (row_index * highlight.row_stride) + byte_index;
            return absolute_offset >= highlight.start_offset && absolute_offset < highlight.start_offset + highlight.byte_count;
        }

        const std::uint64_t absolute_address = highlight.start_address + static_cast<std::uint64_t>((row_index * highlight.row_stride) + byte_index);
        return absolute_address >= highlight.start_address && absolute_address < highlight.start_address + highlight.byte_count;
    }

    ftxui::Element renderMemoryRow(const std::string& row, std::size_t row_index, bool is_selected, const SAppTheme& theme, const SMemoryRenderContext& memory_context) {
        using namespace ftxui;

        const auto memory_parts = splitMemoryRow(row);
        if (!memory_parts.has_value()) {
            auto fallback = text(row) | color(is_selected ? theme.selected_foreground : theme.memory_ascii);
            if (is_selected) {
                fallback = fallback | bgcolor(theme.selected_background);
            }
            return fallback;
        }

        const auto& [address, hex_bytes, ascii] = memory_parts.value();
        const auto byte_tokens                  = splitMemoryByteTokens(hex_bytes);

        Elements   hex_elements;
        Elements   ascii_elements;
        hex_elements.reserve(byte_tokens.size() * 2);
        ascii_elements.reserve(ascii.size());

        const auto default_hex_color          = is_selected ? theme.selected_memory_hex : theme.memory_hex;
        const auto default_ascii_color        = is_selected ? theme.selected_memory_ascii : theme.memory_ascii;
        const auto row_background             = theme.selected_background;
        const auto highlight_hex_background   = theme.memory_highlight_hex_background;
        const auto highlight_ascii_background = theme.memory_highlight_ascii_background;

        for (std::size_t byte_index = 0; byte_index < byte_tokens.size(); ++byte_index) {
            const bool highlighted = memory_context.highlight.has_value() && isHighlightedMemoryByte(memory_context.highlight.value(), row_index, byte_index);

            auto       hex_element =
                text(byte_tokens[byte_index]) | color(highlighted ? (is_selected ? theme.selected_memory_highlight_hex : theme.memory_highlight_hex) : default_hex_color);
            auto ascii_element = text(byte_index < ascii.size() ? std::string(1, ascii[byte_index]) : "") |
                color(highlighted ? (is_selected ? theme.selected_memory_highlight_ascii : theme.memory_highlight_ascii) : default_ascii_color);

            if (is_selected && !highlighted) {
                hex_element = hex_element | bgcolor(row_background);
            }
            if (is_selected && !highlighted) {
                ascii_element = ascii_element | bgcolor(row_background);
            }

            if (highlighted) {
                hex_element   = hex_element | bgcolor(highlight_hex_background);
                ascii_element = ascii_element | bgcolor(highlight_ascii_background);
            }

            hex_elements.push_back(hex_element);
            if (byte_index + 1 < byte_tokens.size()) {
                auto       separator        = text(" ") | color(default_hex_color);
                const bool next_highlighted = memory_context.highlight.has_value() && isHighlightedMemoryByte(memory_context.highlight.value(), row_index, byte_index + 1);
                if (is_selected && !(highlighted && next_highlighted)) {
                    separator = separator | bgcolor(row_background);
                }
                if (highlighted && next_highlighted) {
                    separator = separator | bgcolor(highlight_hex_background);
                }
                hex_elements.push_back(separator);
            }
            ascii_elements.push_back(ascii_element);
        }

        auto address_element = text(address) | color(is_selected ? theme.selected_memory_address : theme.memory_address);
        auto spacing_element = text("  ") | color(default_hex_color);
        auto middle_spacing  = text("  ") | color(default_hex_color);

        if (is_selected) {
            address_element = address_element | bgcolor(row_background);
            spacing_element = spacing_element | bgcolor(row_background);
            middle_spacing  = middle_spacing | bgcolor(row_background);
        }

        return hbox({
            address_element,
            spacing_element,
            hbox(std::move(hex_elements)),
            middle_spacing,
            hbox(std::move(ascii_elements)),
        });
    }

    ftxui::Element renderPaneRow(const std::string& row, std::size_t row_index, bool is_selected, const SAppTheme& theme, const std::string& pane_title, bool is_memory_pane,
                                 const SMemoryRenderContext& memory_context) {
        using namespace ftxui;

        Element row_element;
        if (is_memory_pane) {
            row_element = renderMemoryRow(row, row_index, is_selected, theme, memory_context);
        } else if (pane_title.find("Locals") != std::string::npos) {
            const auto local_parts = splitLocalRow(row);
            if (local_parts.has_value()) {
                const auto& [name, type, value] = local_parts.value();
                row_element                     = hbox({
                    text(name) | color(is_selected ? theme.selected_variable_name : theme.variable_name),
                    text(" : ") | color(is_selected ? theme.selected_foreground : theme.chrome),
                    text(type) | color(is_selected ? theme.selected_variable_type : theme.variable_type),
                    text(" = ") | color(is_selected ? theme.selected_foreground : theme.chrome),
                    text(value) | color(is_selected ? theme.selected_foreground : theme.chrome),
                });
            } else {
                row_element = text(row) | color(is_selected ? theme.selected_foreground : theme.chrome);
            }
        } else if (pane_title.find("Watch List") != std::string::npos) {
            const auto watch_parts = splitWatchRow(row);
            if (watch_parts.has_value()) {
                const auto& [expression, middle, type, has_type] = watch_parts.value();
                if (has_type) {
                    row_element = hbox({
                        text(expression) | color(is_selected ? theme.selected_variable_name : theme.variable_name),
                        text(" = ") | color(is_selected ? theme.selected_foreground : theme.chrome),
                        text(middle) | color(is_selected ? theme.selected_foreground : theme.chrome),
                        text(" : ") | color(is_selected ? theme.selected_foreground : theme.chrome),
                        text(type) | color(is_selected ? theme.selected_variable_type : theme.variable_type),
                    });
                } else {
                    row_element = hbox({
                        text(expression) | color(is_selected ? theme.selected_variable_name : theme.variable_name),
                        text(" : ") | color(is_selected ? theme.selected_foreground : theme.chrome),
                        text(middle) | color(is_selected ? theme.selected_foreground : theme.chrome),
                    });
                }
            } else {
                row_element = text(row) | color(is_selected ? theme.selected_foreground : theme.chrome);
            }
        } else {
            row_element = text(row) | color(is_selected ? theme.selected_foreground : theme.chrome);
        }

        if (is_selected && !is_memory_pane) {
            row_element = row_element | bgcolor(theme.selected_background);
        }

        return row_element;
    }

    ftxui::Element renderSelectablePane(const SSelectablePaneState& pane, bool is_focused, const SAppTheme& theme, const SMemoryRenderContext& memory_context = {}) {
        using namespace ftxui;

        Elements rows;
        Element  title = text(pane.title) | bold | color(theme.title);
        if (is_focused) {
            title = title | bgcolor(theme.selected_background) | color(theme.selected_foreground);
        }

        rows.push_back(title);
        rows.push_back(separator());

        if (pane.rows.empty()) {
            rows.push_back(text("(empty)") | color(theme.chrome));
        } else {
            const bool is_memory_pane = pane.title.find("Memory") != std::string::npos;
            for (std::size_t i = 0; i < pane.rows.size(); ++i) {
                rows.push_back(renderPaneRow(pane.rows[i], i, is_focused && pane.selected_index == i, theme, pane.title, is_memory_pane, memory_context));
            }
        }

        return vbox(rows) | border | color(is_focused ? theme.accent : theme.chrome);
    }

    std::optional<eCommand> findCommandForKeys(const std::vector<SKeybinding>& keybindings, const std::string& keys) {
        const auto keybinding_iterator = std::ranges::find_if(keybindings, [&](const SKeybinding& keybinding) { return keybinding.keys == keys; });
        if (keybinding_iterator == keybindings.end()) {
            return std::nullopt;
        }

        return keybinding_iterator->command;
    }

    ftxui::Element renderShortcutsOverlay(const std::vector<SKeybinding>& keybindings, const SAppTheme& theme) {
        using namespace ftxui;

        Elements rows = {
            text(" Roundtable Shortcuts ") | bold | color(theme.title),
            separator(),
            text("Tab  Cycle focus") | color(theme.chrome),
            text("r  Refresh panes") | color(theme.chrome),
            text("q  Quit") | color(theme.chrome),
        };

        for (const auto& keybinding : keybindings) {
            rows.push_back(hbox({
                text(keybinding.keys) | color(theme.hint_key),
                text("  "),
                text(commandDescription(keybinding.command)) | color(theme.hint_description),
            }));
        }

        return window(text(" Shortcuts ") | color(theme.title), vbox(rows)) | size(WIDTH, GREATER_THAN, 48) | color(theme.overlay_border);
    }

    ftxui::Element renderPromptOverlay(const SPromptState& prompt_state, const SAppTheme& theme) {
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
            case ePromptMode::NONE: return text("") | color(theme.chrome);
        }

        return window(text(title) | color(theme.title),
                      vbox({
                          text(hint) | color(theme.chrome),
                          separator(),
                          text(buildPromptDisplay(prompt_state)) | color(theme.accent),
                      })) |
            size(WIDTH, GREATER_THAN, 48) | color(theme.overlay_border);
    }

    ftxui::Element renderThemePickerOverlay(const SThemePickerState& theme_picker_state, const SAppTheme& theme) {
        using namespace ftxui;

        Elements rows = {
            text("j/k or arrows to preview") | color(theme.chrome),
            text("Return keep  Esc cancel") | color(theme.chrome),
            separator(),
        };

        for (std::size_t index = 0; index < kThemePresets.size(); ++index) {
            Element row = hbox({
                              text(index == theme_picker_state.selected_index ? "> " : "  "),
                              text(themePresetName(kThemePresets[index])),
                          }) |
                color(theme.chrome);
            if (index == theme_picker_state.selected_index) {
                row = row | bgcolor(theme.selected_background) | color(theme.selected_foreground);
            }
            rows.push_back(row);
        }

        return window(text(" Theme Picker ") | color(theme.title), vbox(rows)) | size(WIDTH, EQUAL, 30) | color(theme.overlay_border);
    }

    ftxui::Element renderLeaderPopup(const std::vector<SKeybinding>& keybindings, eFocusPane focused_pane, const SAppTheme& theme) {
        using namespace ftxui;

        const auto hints = buildLeaderHintRows(keybindings, focused_pane);
        Elements   left_column;
        Elements   right_column;
        left_column.push_back(text(" Pane-first ") | bold | color(theme.title));
        right_column.push_back(text(" Global ") | bold | color(theme.title));

        for (const auto& hint : hints) {
            Elements& target_column = hint.pane_specific ? left_column : right_column;
            target_column.push_back(hbox({
                text(hint.keys) | color(hint.pane_specific ? theme.hint_specific_key : theme.hint_key),
                text("  "),
                text(hint.description) | color(hint.pane_specific ? theme.hint_specific_text : theme.hint_description),
            }));
        }

        return window(text(" Leader ") | color(theme.title),
                      hbox({
                          vbox(left_column) | flex,
                          separator(),
                          vbox(right_column) | flex,
                      })) |
            size(WIDTH, GREATER_THAN, 58) | color(theme.overlay_border);
    }

    ftxui::Element renderAuxiliaryViews(const SViewVisibilityState& view_visibility, const SSelectablePaneState& memory_view_pane, const SSelectablePaneState& disassembly_pane,
                                        eFocusPane focused_pane, const SAppTheme& theme, const SMemoryRenderContext& memory_context) {
        using namespace ftxui;

        const bool show_memory       = view_visibility.show_memory_view;
        const bool show_disassembly  = view_visibility.show_disassembly_view;
        const bool memory_is_focused = focused_pane == eFocusPane::MEMORY_VIEW;
        const bool disasm_is_focused = focused_pane == eFocusPane::DISASSEMBLY_VIEW;

        if (show_memory && show_disassembly) {
            return hbox({
                       renderSelectablePane(memory_view_pane, memory_is_focused, theme, memory_context) | flex,
                       renderSelectablePane(disassembly_pane, disasm_is_focused, theme) | flex,
                   }) |
                flex;
        }

        if (show_memory) {
            return renderSelectablePane(memory_view_pane, memory_is_focused, theme, memory_context) | flex;
        }

        if (show_disassembly) {
            return renderSelectablePane(disassembly_pane, disasm_is_focused, theme) | flex;
        }

        return renderSelectablePane(
                   {
                       .title = " Views ",
                       .rows  = {"Enable Memory or Disassembly with Space t / Space a"},
                   },
                   false, theme) |
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

} // namespace

int main() {
    using namespace ftxui;

    SAppConfig                     app_config                   = loadAppConfig("roundtable.toml");
    const auto                     buildActiveTheme             = [&](eThemePreset preset) { return applyThemeOverrides(buildTheme(preset), app_config.theme_overrides); };
    eThemePreset                   active_theme_preset          = app_config.theme_preset;
    SAppTheme                      app_theme                    = buildActiveTheme(active_theme_preset);
    SSessionBootstrapResult        bootstrap_result             = bootstrapSession(app_config);
    std::unique_ptr<IDebugSession> debug_session                = std::move(bootstrap_result.session);
    SDebugSelection                debug_selection              = bootstrap_result.selection;
    std::uint64_t                  disassembly_start_address    = bootstrap_result.disassembly_start_address;
    std::string                    disassembly_memory_reference = bootstrap_result.disassembly_memory_reference;
    std::string                    base_session_status          = bootstrap_result.status_message;
    std::string                    transient_status_message     = {};
    auto                           screen                       = ScreenInteractive::Fullscreen();
    SViewVisibilityState           view_visibility              = {
                               .show_memory_view      = app_config.show_memory_view,
                               .show_disassembly_view = app_config.show_disassembly_view,
    };
    eFocusPane                    focused_pane         = normalizeFocusedPane(app_config.startup_focus, view_visibility);
    std::vector<SKeybinding>      keybindings          = app_config.keybindings;
    bool                          leader_pending       = false;
    std::vector<SWatchExpression> watch_expressions    = app_config.session_mode == eSessionMode::MOCK ?
           std::vector<SWatchExpression>{
            {.expression = "a"},
            {.expression = "ptr"},
        } :
           std::vector<SWatchExpression>{
            {.expression = "sample_value"},
            {.expression = "sample_bytes"},
        };
    std::string                   manual_memory_target = {};
    SPromptState                  prompt_state         = {};
    SThemePickerState             theme_picker_state   = {
                      .active          = false,
                      .original_preset = active_theme_preset,
                      .selected_index  = themePresetIndex(active_theme_preset),
    };
    eWatchActionMode     watch_action_mode = eWatchActionMode::NONE;

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
    std::string          memory_target_label = {};
    SMemoryRenderContext memory_context      = {};

    const auto           refreshAllPanes = [&] {
        SPaneRefreshInputs inputs = {
                      .debug_session                = *debug_session,
                      .debug_selection              = debug_selection,
                      .disassembly_start_address    = disassembly_start_address,
                      .disassembly_memory_reference = disassembly_memory_reference,
                      .focused_pane                 = focused_pane,
                      .watch_expressions            = watch_expressions,
                      .manual_memory_target         = manual_memory_target,
        };
        SPaneRefreshOutputs outputs = {
                      .locals_pane         = locals_pane,
                      .memory_view_pane    = memory_view_pane,
                      .disassembly_pane    = disassembly_pane,
                      .watch_list_pane     = watch_list_pane,
                      .memory_target_label = memory_target_label,
                      .memory_context      = memory_context,
        };
        refreshPaneRows(inputs, outputs);
    };

    const auto reloadConfig = [&] {
        app_config                            = loadAppConfig("roundtable.toml");
        keybindings                           = app_config.keybindings;
        active_theme_preset                   = app_config.theme_preset;
        app_theme                             = buildActiveTheme(active_theme_preset);
        view_visibility.show_memory_view      = app_config.show_memory_view;
        view_visibility.show_disassembly_view = app_config.show_disassembly_view;
        focused_pane                          = normalizeFocusedPane(focused_pane, view_visibility);

        theme_picker_state = {
            .active          = false,
            .original_preset = active_theme_preset,
            .selected_index  = themePresetIndex(active_theme_preset),
        };

        bootstrap_result             = bootstrapSession(app_config);
        debug_session                = std::move(bootstrap_result.session);
        debug_selection              = bootstrap_result.selection;
        disassembly_start_address    = bootstrap_result.disassembly_start_address;
        disassembly_memory_reference = bootstrap_result.disassembly_memory_reference;
        base_session_status          = bootstrap_result.status_message;
        transient_status_message     = "Config reloaded";

        refreshAllPanes();
    };

    refreshAllPanes();

    auto renderer = Renderer([&] {
        const std::string current_status  = transient_status_message.empty() ? base_session_status : transient_status_message;
        Element           locals          = renderSelectablePane(locals_pane, focused_pane == eFocusPane::LOCALS, app_theme);
        Element           watch_list      = renderSelectablePane(watch_list_pane, focused_pane == eFocusPane::WATCH_LIST, app_theme);
        Element           auxiliary_views = renderAuxiliaryViews(view_visibility, memory_view_pane, disassembly_pane, focused_pane, app_theme, memory_context);

        Elements          status_items = {
            text(" Roundtable ") | bgcolor(app_theme.selected_background) | color(app_theme.selected_foreground),
            separator(),
            text(" " + current_status + " ") | color(app_theme.chrome),
            separator(),
            text(" Tab cycle ") | color(app_theme.chrome),
            separator(),
            text(" r refresh ") | color(app_theme.chrome),
            separator(),
            text(" Space commands ") | color(app_theme.accent),
            separator(),
            text(" q quit ") | color(app_theme.chrome),
        };

        Element content = vbox({
            hbox({
                locals | size(WIDTH, EQUAL, 28),
                auxiliary_views | flex,
                watch_list | size(WIDTH, EQUAL, 28),
            }) | flex,
            hbox(status_items) | border | color(app_theme.chrome),
        });

        if (view_visibility.show_shortcuts_overlay) {
            content = dbox({
                content,
                renderShortcutsOverlay(keybindings, app_theme) | center,
            });
        }

        if (leader_pending) {
            content = dbox({
                content,
                vbox({
                    filler(),
                    renderLeaderPopup(keybindings, focused_pane, app_theme) | center,
                    filler() | size(HEIGHT, EQUAL, 5),
                }),
            });
        }

        if (theme_picker_state.active) {
            content = dbox({
                content,
                renderThemePickerOverlay(theme_picker_state, app_theme) | center,
            });
        }

        if (prompt_state.mode != ePromptMode::NONE) {
            content = dbox({
                content,
                renderPromptOverlay(prompt_state, app_theme) | center,
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
                refreshAllPanes();
                return true;
            }

            if (event == Event::ArrowLeft) {
                if (prompt_state.replace_on_input) {
                    prompt_state.cursor_index     = 0;
                    prompt_state.replace_on_input = false;
                } else if (prompt_state.cursor_index > 0) {
                    --prompt_state.cursor_index;
                }
                return true;
            }

            if (event == Event::ArrowRight) {
                if (prompt_state.replace_on_input) {
                    prompt_state.cursor_index     = prompt_state.input.size();
                    prompt_state.replace_on_input = false;
                } else if (prompt_state.cursor_index < prompt_state.input.size()) {
                    ++prompt_state.cursor_index;
                }
                return true;
            }

            if (event == Event::Home) {
                prompt_state.cursor_index     = 0;
                prompt_state.replace_on_input = false;
                return true;
            }

            if (event == Event::End) {
                prompt_state.cursor_index     = prompt_state.input.size();
                prompt_state.replace_on_input = false;
                return true;
            }

            if (event == Event::Backspace) {
                if (prompt_state.replace_on_input) {
                    prompt_state.cursor_index     = 0;
                    prompt_state.replace_on_input = false;
                    prompt_state.input.clear();
                } else if (prompt_state.cursor_index > 0 && !prompt_state.input.empty()) {
                    prompt_state.input.erase(prompt_state.cursor_index - 1, 1);
                    --prompt_state.cursor_index;
                }
                return true;
            }

            if (event == Event::Delete) {
                if (prompt_state.replace_on_input) {
                    prompt_state.cursor_index     = prompt_state.input.size();
                    prompt_state.replace_on_input = false;
                    prompt_state.input.clear();
                } else if (prompt_state.cursor_index < prompt_state.input.size()) {
                    prompt_state.input.erase(prompt_state.cursor_index, 1);
                }
                return true;
            }

            if (event.is_character()) {
                if (prompt_state.replace_on_input) {
                    prompt_state.input            = event.character();
                    prompt_state.cursor_index     = prompt_state.input.size();
                    prompt_state.replace_on_input = false;
                } else {
                    prompt_state.input.insert(prompt_state.cursor_index, event.character());
                    prompt_state.cursor_index += event.character().size();
                }
                return true;
            }

            return true;
        }

        if (theme_picker_state.active) {
            if (event == Event::Escape) {
                active_theme_preset      = theme_picker_state.original_preset;
                app_theme                = buildActiveTheme(active_theme_preset);
                theme_picker_state       = {};
                transient_status_message = {};
                return true;
            }

            if (event == Event::Return) {
                theme_picker_state.active = false;
                transient_status_message  = {};
                return true;
            }

            const auto move_up   = event == Event::ArrowUp || event == Event::Character('k');
            const auto move_down = event == Event::ArrowDown || event == Event::Character('j');

            if (move_up && theme_picker_state.selected_index > 0) {
                --theme_picker_state.selected_index;
            } else if (move_down && theme_picker_state.selected_index + 1 < kThemePresets.size()) {
                ++theme_picker_state.selected_index;
            } else if (!(move_up || move_down)) {
                return true;
            }

            active_theme_preset      = kThemePresets[theme_picker_state.selected_index];
            app_theme                = buildActiveTheme(active_theme_preset);
            transient_status_message = "Theme preview: " + themePresetName(active_theme_preset);
            return true;
        }

        if (watch_action_mode != eWatchActionMode::NONE && focused_pane == eFocusPane::WATCH_LIST) {
            if (event == Event::Escape) {
                watch_action_mode        = eWatchActionMode::NONE;
                transient_status_message = {};
                return true;
            }

            if (event == Event::Return) {
                if (watch_action_mode == eWatchActionMode::EDIT) {
                    if (!watch_expressions.empty() && watch_list_pane.selected_index < watch_expressions.size()) {
                        prompt_state = beginPrompt(ePromptMode::EDIT_WATCH, watch_expressions[watch_list_pane.selected_index].expression, true);
                    }
                } else if (watch_action_mode == eWatchActionMode::REMOVE) {
                    if (!watch_expressions.empty() && watch_list_pane.selected_index < watch_expressions.size()) {
                        watch_expressions.erase(watch_expressions.begin() + static_cast<std::ptrdiff_t>(watch_list_pane.selected_index));
                        if (watch_list_pane.selected_index > 0 && watch_list_pane.selected_index >= watch_expressions.size()) {
                            --watch_list_pane.selected_index;
                        }
                        refreshAllPanes();
                    }
                }

                watch_action_mode        = eWatchActionMode::NONE;
                transient_status_message = {};
                return true;
            }

            const bool handled = handleVerticalNavigation(event, watch_list_pane);
            if (handled) {
                refreshAllPanes();
            }
            return true;
        }

        if (event == Event::Character('q')) {
            screen.Exit();
            return true;
        }

        if (event == Event::Character('r')) {
            refreshAllPanes();
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
                        prompt_state = beginPrompt(ePromptMode::ADD_WATCH);
                        return true;
                    }
                    if (command.value() == eCommand::EDIT_WATCH) {
                        if (watch_expressions.empty()) {
                            transient_status_message = "No watch entries to edit";
                            return true;
                        }
                        if (focused_pane != eFocusPane::WATCH_LIST) {
                            focused_pane             = eFocusPane::WATCH_LIST;
                            watch_action_mode        = eWatchActionMode::EDIT;
                            transient_status_message = "Choose watch with j/k, press Return to edit, Esc to cancel";
                            refreshAllPanes();
                            return true;
                        }
                        if (!watch_expressions.empty() && watch_list_pane.selected_index < watch_expressions.size()) {
                            prompt_state = beginPrompt(ePromptMode::EDIT_WATCH, watch_expressions[watch_list_pane.selected_index].expression, true);
                            return true;
                        }
                        return true;
                    }
                    if (command.value() == eCommand::REMOVE_WATCH) {
                        if (watch_expressions.empty()) {
                            transient_status_message = "No watch entries to remove";
                            return true;
                        }
                        if (focused_pane != eFocusPane::WATCH_LIST) {
                            focused_pane             = eFocusPane::WATCH_LIST;
                            watch_action_mode        = eWatchActionMode::REMOVE;
                            transient_status_message = "Choose watch with j/k, press Return to remove, Esc to cancel";
                            refreshAllPanes();
                            return true;
                        }
                        if (!watch_expressions.empty() && watch_list_pane.selected_index < watch_expressions.size()) {
                            watch_expressions.erase(watch_expressions.begin() + static_cast<std::ptrdiff_t>(watch_list_pane.selected_index));
                            if (watch_list_pane.selected_index > 0 && watch_list_pane.selected_index >= watch_expressions.size()) {
                                --watch_list_pane.selected_index;
                            }
                            focused_pane = eFocusPane::WATCH_LIST;
                            refreshAllPanes();
                        }
                        return true;
                    }
                    if (command.value() == eCommand::SET_MEMORY_TARGET) {
                        prompt_state = beginPrompt(ePromptMode::MEMORY_TARGET, manual_memory_target);
                        return true;
                    }
                    if (command.value() == eCommand::CYCLE_THEME) {
                        theme_picker_state = {
                            .active          = true,
                            .original_preset = active_theme_preset,
                            .selected_index  = themePresetIndex(active_theme_preset),
                        };
                        transient_status_message = "Theme preview: " + themePresetName(active_theme_preset);
                        return true;
                    }
                    if (command.value() == eCommand::RELOAD_CONFIG) {
                        reloadConfig();
                        return true;
                    }
                    executeCommand(command.value(), focused_pane, view_visibility);
                    refreshAllPanes();
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
            refreshAllPanes();
            return true;
        }

        if (focused_pane == eFocusPane::LOCALS) {
            const bool handled = handleVerticalNavigation(event, locals_pane);
            if (handled) {
                refreshAllPanes();
            }
            return handled;
        }
        if (focused_pane == eFocusPane::WATCH_LIST) {
            const bool handled = handleVerticalNavigation(event, watch_list_pane);
            if (handled) {
                refreshAllPanes();
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
