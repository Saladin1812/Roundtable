#include <array>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "app_config.hpp"
#include "app_layout.hpp"
#include "app_startup.hpp"
#include "app_theme.hpp"
#include "dap_session.hpp"
#include "debugger_controller.hpp"
#include "debug_session.hpp"
#include "pane_refresh.hpp"
#include "pane_state.hpp"

namespace {

    enum class eWatchActionMode : std::uint8_t {
        NONE,
        EDIT,
        REMOVE,
        MOVE_UP,
        MOVE_DOWN,
        DUPLICATE,
    };

    enum class eBreakpointActionMode : std::uint8_t {
        NONE,
        REMOVE,
    };

    struct SAsyncDapControlState {
        std::atomic_bool running             = false;
        std::atomic_bool pause_requested     = false;
        std::atomic_bool terminate_requested = false;
        std::atomic_bool quit_requested      = false;
        std::jthread     worker;
    };

    std::size_t launchProfileIndex(const std::vector<SLaunchProfileConfig>& profiles, const std::string& active_profile) {
        for (std::size_t index = 0; index < profiles.size(); ++index) {
            if (profiles[index].name == active_profile) {
                return index;
            }
        }

        return 0;
    }

    std::string compactPathForStatus(std::string source_path) {
        if (source_path.empty()) {
            return "<unknown>";
        }

        const auto last_separator = source_path.find_last_of("/\\");
        if (last_separator == std::string::npos) {
            return source_path;
        }

        return source_path.substr(last_separator + 1);
    }

    std::string compactPathWithParent(std::string source_path) {
        if (source_path.empty()) {
            return "<unknown>";
        }

        const auto filename_start = source_path.find_last_of("/\\");
        if (filename_start == std::string::npos) {
            return source_path;
        }

        const auto parent_end   = filename_start;
        const auto parent_start = source_path.find_last_of("/\\", parent_end == 0 ? 0 : parent_end - 1);
        if (parent_start == std::string::npos) {
            return source_path;
        }

        return ".../" + source_path.substr(parent_start + 1);
    }

    SStoppedLocation stackFrameLocation(const SStoppedStackFrame& stack_frame) {
        return {
            .function_name = stack_frame.function_name,
            .source_path   = stack_frame.source_path,
            .line          = stack_frame.line,
            .column        = stack_frame.column,
        };
    }

    SStoppedLocation selectedStoppedLocation(const SStoppedContext& stopped_context, const SDebugSelection& selection) {
        if (selection.frame_index < stopped_context.stack_frames.size()) {
            return stackFrameLocation(stopped_context.stack_frames[selection.frame_index]);
        }

        return stopped_context.location;
    }

    std::string sourceBreakpointTextForLocation(const SStoppedContext& stopped_context, const SDebugSelection& selection) {
        const auto location = selectedStoppedLocation(stopped_context, selection);
        if (location.source_path.empty() || location.line <= 0) {
            return "";
        }

        return location.source_path + ":" + std::to_string(location.line);
    }

    std::string describeBreakpoint(const SSourceBreakpointConfig& breakpoint) {
        return compactPathWithParent(breakpoint.source_path.string()) + ":" + std::to_string(breakpoint.line);
    }

    std::vector<std::string> formatStackPaneRows(const std::vector<SStoppedStackFrame>& stack_frames) {
        std::vector<std::string> rows;
        rows.reserve(stack_frames.size());

        for (std::size_t index = 0; index < stack_frames.size(); ++index) {
            const auto& stack_frame = stack_frames[index];
            std::string row         = "#" + std::to_string(index) + " ";
            row += stack_frame.function_name.empty() ? "<unknown>" : stack_frame.function_name;
            if (!stack_frame.source_path.empty()) {
                row += " ";
                row += compactPathForStatus(stack_frame.source_path);
                if (stack_frame.line > 0) {
                    row += ":" + std::to_string(stack_frame.line);
                }
            }
            rows.push_back(std::move(row));
        }

        return rows;
    }

    std::vector<std::string> formatThreadPaneRows(const std::vector<SStoppedThread>& threads) {
        std::vector<std::string> rows;
        rows.reserve(threads.size());

        for (std::size_t index = 0; index < threads.size(); ++index) {
            const auto& thread = threads[index];
            std::string row    = "#" + std::to_string(index) + " T:" + std::to_string(thread.id);
            if (!thread.name.empty()) {
                row += " " + thread.name;
            }
            rows.push_back(std::move(row));
        }

        return rows;
    }

    std::size_t selectedThreadIndex(const std::vector<SStoppedThread>& threads, std::int64_t thread_id) {
        for (std::size_t index = 0; index < threads.size(); ++index) {
            if (threads[index].id == thread_id) {
                return index;
            }
        }

        return 0;
    }

    std::vector<std::string> formatBreakpointPaneRows(const std::vector<SSourceBreakpointConfig>& breakpoints) {
        std::vector<std::string> rows;
        rows.reserve(breakpoints.size());

        for (std::size_t index = 0; index < breakpoints.size(); ++index) {
            const auto&       breakpoint = breakpoints[index];
            const std::string state      = breakpoint.enabled ? "[x] " : "[ ] ";
            std::string       status;
            if (!breakpoint.enabled) {
                status = "off";
            } else if (!breakpoint.adapter_status_known) {
                status = "pending";
            } else if (breakpoint.adapter_verified) {
                status = "ok";
            } else {
                status = "fail";
            }

            std::string row = state + status + " #" + std::to_string(index) + " :" + std::to_string(breakpoint.line) + " " + compactPathWithParent(breakpoint.source_path.string());
            if (breakpoint.enabled && breakpoint.adapter_status_known && breakpoint.adapter_line > 0 && breakpoint.adapter_line != breakpoint.line) {
                row += " -> :" + std::to_string(breakpoint.adapter_line);
            }
            if (breakpoint.enabled && !breakpoint.adapter_message.empty()) {
                row += " - " + breakpoint.adapter_message;
            }

            rows.push_back(std::move(row));
        }

        return rows;
    }

    std::optional<eCommand> findCommandForKeys(const std::vector<SKeybinding>& keybindings, const std::string& keys) {
        const auto keybinding_iterator = std::ranges::find_if(keybindings, [&](const SKeybinding& keybinding) { return keybinding.keys == keys; });
        if (keybinding_iterator == keybindings.end()) {
            return std::nullopt;
        }

        return keybinding_iterator->command;
    }

    std::string lowercaseCopy(std::string text) {
        std::ranges::transform(text, text.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        return text;
    }

    bool selectProfileBySearch(SProfilePickerState& profile_picker_state, const std::vector<SLaunchProfileConfig>& profiles) {
        if (profile_picker_state.search_query.empty()) {
            return false;
        }

        const auto search_query = lowercaseCopy(profile_picker_state.search_query);
        for (std::size_t index = 0; index < profiles.size(); ++index) {
            if (lowercaseCopy(profiles[index].name).starts_with(search_query)) {
                profile_picker_state.selected_index = index;
                return true;
            }
        }

        return false;
    }

    bool shouldShowStartupDashboard(const SCliOptions& cli_options, const SAppConfig& app_config) {
        return !cli_options.force_mock && !cli_options.config_path_explicit && !cli_options.profile.has_value() && !cli_options.launch_program.has_value() &&
            app_config.session_mode == eSessionMode::DAP_LAUNCH && app_config.dap_launch.program.empty();
    }

    std::string dashboardConfigPathLabel(const SCliOptions& cli_options) {
        if (!cli_options.config_path_explicit) {
            if (const auto default_path = findDefaultAppConfigPath(); default_path.has_value()) {
                return std::filesystem::absolute(default_path.value()).string();
            }
        }

        return std::filesystem::absolute(cli_options.config_path).string();
    }

    std::filesystem::path expandHomeDirectory(const std::string& text) {
        if (text == "~" || text.starts_with("~/")) {
            if (const char* home = std::getenv("HOME"); home != nullptr) {
                if (text.size() == 1) {
                    return std::filesystem::path(home);
                }
                return std::filesystem::path(home) / text.substr(2);
            }
        }

        return std::filesystem::path(text);
    }

    std::string unexpandHomeDirectory(const std::filesystem::path& path, const std::string& original_text) {
        if (!original_text.starts_with("~")) {
            return path.string();
        }

        if (const char* home = std::getenv("HOME"); home != nullptr) {
            const auto home_path   = std::filesystem::path(home).lexically_normal().string();
            const auto target_path = path.lexically_normal().string();
            const bool exact_home  = target_path == home_path;
            const bool inside_home = target_path.starts_with(home_path + std::string{"/"});
            if (exact_home) {
                return "~";
            }
            if (inside_home) {
                return "~/" + target_path.substr(home_path.size() + 1);
            }
        }

        return path.string();
    }

    std::optional<std::string> completePathInput(const std::string& input) {
        const std::string effective_input = input.empty() ? "." : input;
        const auto        expanded_input  = expandHomeDirectory(effective_input);
        const bool        input_is_dir    = std::filesystem::is_directory(expanded_input);
        const bool        ends_with_sep   = !effective_input.empty() && (effective_input.back() == '/' || effective_input.back() == '\\');
        const auto        directory       = input_is_dir && ends_with_sep ? expanded_input : expanded_input.parent_path();
        const auto        prefix          = input_is_dir && ends_with_sep ? std::string{} : expanded_input.filename().string();
        const auto        scan_directory  = directory.empty() ? std::filesystem::path(".") : directory;

        std::error_code   error_code;
        if (!std::filesystem::exists(scan_directory, error_code) || error_code) {
            return std::nullopt;
        }

        std::vector<std::filesystem::path> matches;
        for (const auto& entry : std::filesystem::directory_iterator(scan_directory, error_code)) {
            if (error_code) {
                return std::nullopt;
            }

            const auto filename = entry.path().filename().string();
            if (filename.starts_with(prefix)) {
                matches.push_back(entry.path());
            }
        }

        if (matches.empty()) {
            return std::nullopt;
        }

        std::ranges::sort(matches);
        auto completed_path = matches.front();
        if (matches.size() > 1) {
            std::string common_prefix = matches.front().filename().string();
            for (const auto& match : matches | std::views::drop(1)) {
                const auto  filename    = match.filename().string();
                std::size_t common_size = 0;
                while (common_size < common_prefix.size() && common_size < filename.size() && common_prefix[common_size] == filename[common_size]) {
                    ++common_size;
                }
                common_prefix.resize(common_size);
            }

            completed_path = scan_directory / common_prefix;
        }

        if (std::filesystem::is_directory(completed_path, error_code) && !error_code) {
            completed_path /= "";
        }

        return unexpandHomeDirectory(completed_path, effective_input);
    }

} // namespace

int main(int argc, char** argv) {
    using namespace ftxui;

    const SAppStartupResult startup_result = initializeAppStartup(argc, argv, std::cout);
    if (startup_result.should_exit) {
        return startup_result.exit_code;
    }

    const SCliOptions              cli_options                  = startup_result.cli_options;
    const std::string              config_path                  = startup_result.config_path;
    std::string                    display_config_path          = dashboardConfigPathLabel(cli_options);
    SAppConfig                     app_config                   = startup_result.app_config;
    const auto                     buildActiveTheme             = [&](eThemePreset preset) { return applyThemeOverrides(buildTheme(preset), app_config.theme_overrides); };
    eThemePreset                   active_theme_preset          = app_config.theme_preset;
    SAppTheme                      app_theme                    = buildActiveTheme(active_theme_preset);
    bool                           dashboard_active             = shouldShowStartupDashboard(cli_options, app_config);
    SSessionBootstrapResult        bootstrap_result             = dashboard_active ?
                           SSessionBootstrapResult{
                               .session                      = nullptr,
                               .selection                    = {},
                               .disassembly_start_address    = 0x401000,
                               .disassembly_memory_reference = "",
                               .stopped_context              = {},
                               .status_message               = "Choose a launch action",
                               .state                        = eDebuggerSessionState::TERMINATED,
        } :
                           bootstrapSession(app_config);
    std::unique_ptr<IDebugSession> debug_session                = std::move(bootstrap_result.session);
    SDebugSelection                debug_selection              = bootstrap_result.selection;
    std::uint64_t                  disassembly_start_address    = bootstrap_result.disassembly_start_address;
    std::string                    disassembly_memory_reference = bootstrap_result.disassembly_memory_reference;
    SStoppedContext                stopped_context              = bootstrap_result.stopped_context;
    eDebuggerSessionState          session_state                = bootstrap_result.state;
    std::string                    base_session_status          = bootstrap_result.status_message;
    std::string                    transient_status_message     = {};
    std::int64_t                   memory_navigation_offset     = 0;
    auto                           screen                       = ScreenInteractive::Fullscreen();
    SViewVisibilityState           view_visibility              = {
                               .show_memory_view      = app_config.show_memory_view,
                               .show_disassembly_view = app_config.show_disassembly_view,
    };
    eFocusPane               focused_pane                 = normalizeFocusedPane(app_config.startup_focus, view_visibility);
    std::vector<SKeybinding> keybindings                  = app_config.keybindings;
    bool                     leader_pending               = false;
    const auto               buildInitialWatchExpressions = [](const SAppConfig& config) {
        if (!config.watches.empty()) {
            std::vector<SWatchExpression> configured_watches;
            configured_watches.reserve(config.watches.size());
            for (const auto& watch : config.watches) {
                configured_watches.push_back({.expression = watch});
            }
            return configured_watches;
        }

        if (config.session_mode == eSessionMode::MOCK) {
            return std::vector<SWatchExpression>{
                {.expression = "a"},
                {.expression = "ptr"},
            };
        }

        return std::vector<SWatchExpression>{};
    };
    std::vector<SWatchExpression> watch_expressions    = buildInitialWatchExpressions(app_config);
    std::string                   manual_memory_target = {};
    SPromptState                  prompt_state         = {};
    SThemePickerState             theme_picker_state   = {
                      .active          = false,
                      .original_preset = active_theme_preset,
                      .selected_index  = themePresetIndex(active_theme_preset),
    };
    SProfilePickerState   profile_picker_state    = {};
    eWatchActionMode      watch_action_mode       = eWatchActionMode::NONE;
    eBreakpointActionMode breakpoint_action_mode  = eBreakpointActionMode::NONE;
    SAsyncDapControlState async_dap_control_state = {};

    SSelectablePaneState  locals_pane = {
         .title = " Locals ",
         .rows  = {},
    };
    SSelectablePaneState threads_pane = {
        .title = " Threads ",
        .rows  = {},
    };
    SSelectablePaneState stack_pane = {
        .title = " Stack ",
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
    SSelectablePaneState breakpoints_pane = {
        .title = " Breakpoints ",
        .rows  = {},
    };
    std::string          memory_target_label = {};
    SMemoryRenderContext memory_context      = {};

    const auto           isDapControlRunning = [&] { return async_dap_control_state.running.load(); };

    const auto           clearDebuggerPanes = [&] {
        locals_pane.rows                = {"Session terminated"};
        threads_pane.rows               = {"Session terminated"};
        stack_pane.rows                 = {"Session terminated"};
        memory_view_pane.rows           = {"Session terminated"};
        disassembly_pane.rows           = {"Session terminated"};
        watch_list_pane.rows            = {"Session terminated"};
        breakpoints_pane.rows           = {"Session terminated"};
        locals_pane.selected_index      = 0;
        threads_pane.selected_index     = 0;
        stack_pane.selected_index       = 0;
        memory_view_pane.selected_index = 0;
        disassembly_pane.selected_index = 0;
        watch_list_pane.selected_index  = 0;
        breakpoints_pane.selected_index = 0;
        memory_target_label             = {};
        memory_context                  = {};
        stopped_context                 = {};
    };

    const auto isSessionTerminated = [&] { return session_state == eDebuggerSessionState::TERMINATED; };

    const auto refreshAllPanes = [&] {
        if (dashboard_active || debug_session == nullptr) {
            return;
        }
        if (isDapControlRunning()) {
            return;
        }
        if (isSessionTerminated()) {
            clearDebuggerPanes();
            return;
        }

        threads_pane.title = " Threads [" + std::to_string(stopped_context.threads.size()) + "] ";
        threads_pane.rows  = formatThreadPaneRows(stopped_context.threads);
        threads_pane.selected_index =
            std::min(selectedThreadIndex(stopped_context.threads, debug_selection.thread_id), threads_pane.rows.empty() ? 0UL : threads_pane.rows.size() - 1);
        stack_pane.title                = " Stack [T:" + std::to_string(debug_selection.thread_id) + "] ";
        stack_pane.rows                 = formatStackPaneRows(stopped_context.stack_frames);
        stack_pane.selected_index       = std::min(debug_selection.frame_index, stack_pane.rows.empty() ? 0UL : stack_pane.rows.size() - 1);
        const auto enabled_breakpoints  = std::ranges::count_if(app_config.breakpoints, [](const SSourceBreakpointConfig& breakpoint) { return breakpoint.enabled; });
        breakpoints_pane.title          = " Breakpoints [" + std::to_string(enabled_breakpoints) + "/" + std::to_string(app_config.breakpoints.size()) + " on] ";
        breakpoints_pane.rows           = formatBreakpointPaneRows(app_config.breakpoints);
        breakpoints_pane.selected_index = std::min(breakpoints_pane.selected_index, breakpoints_pane.rows.empty() ? 0UL : breakpoints_pane.rows.size() - 1);

        SPaneRefreshInputs inputs = {
            .debug_session                = *debug_session,
            .debug_selection              = debug_selection,
            .disassembly_start_address    = disassembly_start_address,
            .disassembly_memory_reference = disassembly_memory_reference,
            .memory_navigation_offset     = memory_navigation_offset,
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

    const auto applyBootstrapResult = [&](SSessionBootstrapResult new_bootstrap_result, std::string status_message) {
        bootstrap_result             = std::move(new_bootstrap_result);
        debug_session                = std::move(bootstrap_result.session);
        debug_selection              = bootstrap_result.selection;
        disassembly_start_address    = bootstrap_result.disassembly_start_address;
        disassembly_memory_reference = bootstrap_result.disassembly_memory_reference;
        stopped_context              = bootstrap_result.stopped_context;
        session_state                = bootstrap_result.state;
        base_session_status          = bootstrap_result.status_message;
        transient_status_message     = std::move(status_message);
        memory_navigation_offset     = 0;
    };

    const auto launchProgram = [&](const std::filesystem::path& program_path) {
        if (program_path.empty()) {
            transient_status_message = "Enter a program path to launch";
            return;
        }

        const auto absolute_program_path        = std::filesystem::absolute(program_path);
        app_config.session_mode                 = eSessionMode::DAP_LAUNCH;
        app_config.dap_launch.program           = absolute_program_path.string();
        app_config.dap_launch.continue_once     = false;
        app_config.dap_launch.working_directory = absolute_program_path.has_parent_path() ? absolute_program_path.parent_path().string() : std::filesystem::current_path().string();
        dashboard_active                        = false;
        watch_expressions                       = buildInitialWatchExpressions(app_config);
        memory_navigation_offset                = 0;
        applyBootstrapResult(bootstrapSession(app_config), "Launching " + absolute_program_path.filename().string());
        refreshAllPanes();
    };

    const auto clearBreakpointSourceInActiveSession = [&](const std::filesystem::path& source_path) {
        if (session_state != eDebuggerSessionState::STOPPED) {
            return true;
        }

        auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get());
        if (dap_session == nullptr || source_path.empty()) {
            return true;
        }

        const auto response = dap_session->setBreakpoints({
            .source_path = std::filesystem::absolute(source_path).string(),
            .breakpoints = {},
        });
        if (!response.success) {
            transient_status_message = "DAP clear breakpoints failed: " + response.error_message;
            return false;
        }

        return true;
    };

    const auto hasBreakpointForSource = [&](const std::filesystem::path& source_path) {
        if (source_path.empty()) {
            return false;
        }

        const auto absolute_source_path = std::filesystem::absolute(source_path);
        return std::ranges::any_of(app_config.breakpoints, [&](const SSourceBreakpointConfig& breakpoint) {
            return breakpoint.enabled && !breakpoint.source_path.empty() && std::filesystem::absolute(breakpoint.source_path) == absolute_source_path;
        });
    };

    const auto applyBreakpointsToActiveSession = [&]() {
        if (isDapControlRunning()) {
            transient_status_message = "Breakpoints cannot be changed while debugger is running";
            return false;
        }
        if (session_state == eDebuggerSessionState::TERMINATED || session_state == eDebuggerSessionState::ERROR) {
            transient_status_message = "Breakpoint queued for next DAP launch";
            return true;
        }

        auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get());
        if (dap_session == nullptr) {
            transient_status_message = "Breakpoint queued for next DAP launch";
            return true;
        }

        std::string breakpoint_error_message;
        if (!configureDapBreakpoints(*dap_session, app_config.breakpoints, breakpoint_error_message)) {
            transient_status_message = breakpoint_error_message;
            return false;
        }

        transient_status_message = "Breakpoint applied";
        return true;
    };

    const auto moveSelectedWatchUp = [&]() {
        if (watch_list_pane.selected_index == 0 || watch_list_pane.selected_index >= watch_expressions.size()) {
            transient_status_message = "Watch is already at the top";
            return;
        }

        std::swap(watch_expressions[watch_list_pane.selected_index], watch_expressions[watch_list_pane.selected_index - 1]);
        --watch_list_pane.selected_index;
        memory_navigation_offset = 0;
        transient_status_message = "Watch moved up";
        refreshAllPanes();
    };

    const auto moveSelectedWatchDown = [&]() {
        if (watch_list_pane.selected_index >= watch_expressions.size() || watch_list_pane.selected_index + 1 >= watch_expressions.size()) {
            transient_status_message = "Watch is already at the bottom";
            return;
        }

        std::swap(watch_expressions[watch_list_pane.selected_index], watch_expressions[watch_list_pane.selected_index + 1]);
        ++watch_list_pane.selected_index;
        memory_navigation_offset = 0;
        transient_status_message = "Watch moved down";
        refreshAllPanes();
    };

    const auto duplicateSelectedWatch = [&]() {
        if (watch_list_pane.selected_index >= watch_expressions.size()) {
            transient_status_message = "No watch selected";
            return;
        }

        const auto insert_position = watch_expressions.begin() + static_cast<std::ptrdiff_t>(watch_list_pane.selected_index + 1);
        watch_expressions.insert(insert_position, watch_expressions[watch_list_pane.selected_index]);
        ++watch_list_pane.selected_index;
        memory_navigation_offset = 0;
        transient_status_message = "Watch duplicated";
        refreshAllPanes();
    };

    const auto performWatchAction = [&](eWatchActionMode action_mode) {
        if (watch_expressions.empty()) {
            transient_status_message = "No watch entries";
            return;
        }

        switch (action_mode) {
            case eWatchActionMode::NONE: break;
            case eWatchActionMode::EDIT:
                if (watch_list_pane.selected_index < watch_expressions.size()) {
                    prompt_state = beginPrompt(ePromptMode::EDIT_WATCH, watch_expressions[watch_list_pane.selected_index].expression, true);
                }
                break;
            case eWatchActionMode::REMOVE:
                if (watch_list_pane.selected_index < watch_expressions.size()) {
                    watch_expressions.erase(watch_expressions.begin() + static_cast<std::ptrdiff_t>(watch_list_pane.selected_index));
                    if (watch_list_pane.selected_index > 0 && watch_list_pane.selected_index >= watch_expressions.size()) {
                        --watch_list_pane.selected_index;
                    }
                    memory_navigation_offset = 0;
                    transient_status_message = "Watch removed";
                    refreshAllPanes();
                }
                break;
            case eWatchActionMode::MOVE_UP: moveSelectedWatchUp(); break;
            case eWatchActionMode::MOVE_DOWN: moveSelectedWatchDown(); break;
            case eWatchActionMode::DUPLICATE: duplicateSelectedWatch(); break;
        }
    };

    const auto chooseOrPerformWatchAction = [&](eWatchActionMode action_mode, std::string choose_message) {
        if (watch_expressions.empty()) {
            transient_status_message = "No watch entries";
            return;
        }

        if (focused_pane != eFocusPane::WATCH_LIST) {
            focused_pane             = eFocusPane::WATCH_LIST;
            watch_action_mode        = action_mode;
            transient_status_message = std::move(choose_message);
            refreshAllPanes();
            return;
        }

        performWatchAction(action_mode);
    };

    const auto reloadConfig = [&] {
        if (isDapControlRunning() || session_state == eDebuggerSessionState::RUNNING || session_state == eDebuggerSessionState::LAUNCHING ||
            session_state == eDebuggerSessionState::TERMINATING) {
            transient_status_message = "Config reload is disabled while debugger is running";
            return;
        }

        const SAppConfig previous_config      = app_config;
        const auto       reload_config_result = loadAppConfigForCliWithDiagnostics(config_path, cli_options.config_path_explicit);
        SAppConfig       reloaded_config      = reload_config_result.config;
        applyCliOverrides(cli_options, reloaded_config);

        app_config          = std::move(reloaded_config);
        keybindings         = app_config.keybindings;
        display_config_path = dashboardConfigPathLabel(cli_options);
        if (!app_config.watches.empty() || session_state != eDebuggerSessionState::STOPPED) {
            watch_expressions = buildInitialWatchExpressions(app_config);
        }
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
        profile_picker_state = {};

        dashboard_active = shouldShowStartupDashboard(cli_options, app_config);
        if (dashboard_active) {
            debug_session            = nullptr;
            debug_selection          = {};
            stopped_context          = {};
            session_state            = eDebuggerSessionState::TERMINATED;
            base_session_status      = "Choose a launch action";
            transient_status_message = "Config reloaded";
            memory_navigation_offset = 0;
        } else if (session_state == eDebuggerSessionState::STOPPED) {
            applyBreakpointsToActiveSession();
            for (const auto& previous_breakpoint : previous_config.breakpoints) {
                if (!hasBreakpointForSource(previous_breakpoint.source_path)) {
                    clearBreakpointSourceInActiveSession(previous_breakpoint.source_path);
                }
            }

            if (auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get()); dap_session != nullptr) {
                stopped_context = updateDapStoppedContext(*dap_session, debug_selection, disassembly_start_address, disassembly_memory_reference);
            }

            const bool launch_settings_changed = previous_config.session_mode != app_config.session_mode || previous_config.dap_launch.command != app_config.dap_launch.command ||
                previous_config.dap_launch.liblldb_path != app_config.dap_launch.liblldb_path || previous_config.dap_launch.program != app_config.dap_launch.program ||
                previous_config.dap_launch.arguments != app_config.dap_launch.arguments ||
                previous_config.dap_launch.working_directory != app_config.dap_launch.working_directory ||
                previous_config.dap_launch.stop_on_entry != app_config.dap_launch.stop_on_entry || previous_config.dap_launch.continue_once != app_config.dap_launch.continue_once;
            transient_status_message = launch_settings_changed ? "Config reloaded; launch changes apply on restart" : "Config reloaded";
        } else {
            applyBootstrapResult(bootstrapSession(app_config), "Config reloaded");
        }

        if (!reload_config_result.diagnostics.empty()) {
            transient_status_message = "Config reload warning: " + reload_config_result.diagnostics.front();
            if (reload_config_result.diagnostics.size() > 1) {
                transient_status_message += " (+" + std::to_string(reload_config_result.diagnostics.size() - 1) + " more)";
            }
        }

        memory_navigation_offset = 0;
        refreshAllPanes();
    };

    const auto executeDapControl = [&](const std::string& action_name, const std::string& stopped_action_name, auto send_request) {
        auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get());
        if (dap_session == nullptr) {
            transient_status_message = action_name + " is only available in DAP sessions";
            return;
        }
        if (session_state == eDebuggerSessionState::TERMINATED) {
            transient_status_message = action_name + " is unavailable after session termination";
            return;
        }
        if (session_state == eDebuggerSessionState::ERROR) {
            transient_status_message = action_name + " is unavailable because the session failed to start";
            return;
        }
        if (isDapControlRunning()) {
            transient_status_message = "Debugger is already running";
            return;
        }
        if (debug_selection.thread_id == 0) {
            transient_status_message = action_name + " failed: no active thread";
            return;
        }

        const int thread_id = static_cast<int>(debug_selection.thread_id);
        session_state       = eDebuggerSessionState::RUNNING;
        async_dap_control_state.running.store(true);
        async_dap_control_state.pause_requested.store(false);
        async_dap_control_state.terminate_requested.store(false);
        transient_status_message = "Running: " + stopped_action_name;

        async_dap_control_state.worker = std::jthread([&, dap_session, action_name, stopped_action_name, send_request, thread_id](std::stop_token) mutable {
            bool        success = false;
            std::string error_message;

            bool        terminated = false;

            if (!send_request(*dap_session, thread_id)) {
                error_message = action_name + " failed: " + dap_session->getLastError();
            } else if (!dap_session->waitForStoppedEvent()) {
                terminated = async_dap_control_state.terminate_requested.load() && dap_session->getLastError() == "DAP session ended before a stopped event";
                if (!terminated) {
                    error_message = "Wait after " + stopped_action_name + " failed: " + dap_session->getLastError();
                }
            } else {
                success = true;
            }

            screen.Post(Closure([&, dap_session, success, terminated, error_message, stopped_action_name] {
                async_dap_control_state.running.store(false);
                async_dap_control_state.terminate_requested.store(false);
                if (terminated) {
                    async_dap_control_state.pause_requested.store(false);
                    session_state            = eDebuggerSessionState::TERMINATED;
                    base_session_status      = "Session terminated";
                    transient_status_message = "Terminated";
                    clearDebuggerPanes();
                    if (async_dap_control_state.quit_requested.exchange(false)) {
                        screen.Exit();
                    }
                    return;
                }
                if (!success) {
                    async_dap_control_state.pause_requested.store(false);
                    session_state            = eDebuggerSessionState::ERROR;
                    transient_status_message = error_message;
                    return;
                }

                stopped_context = updateDapStoppedContext(*dap_session, debug_selection, disassembly_start_address, disassembly_memory_reference);

                const std::string completed_action_name = async_dap_control_state.pause_requested.exchange(false) ? "pause" : stopped_action_name;
                async_dap_control_state.quit_requested.store(false);
                session_state            = eDebuggerSessionState::STOPPED;
                memory_navigation_offset = 0;
                transient_status_message = "Stopped after " + completed_action_name;
                refreshAllPanes();
            }));
            screen.PostEvent(Event::Custom);
        });
    };

    const auto continueActiveDapSession = [&]() {
        executeDapControl("Continue", "continue", [](CDapDebugSession& dap_session, int thread_id) {
            return dap_session.sendContinueRequest({
                .thread_id = thread_id,
            });
        });
    };

    const auto stepOverActiveDapSession = [&]() {
        executeDapControl("Step over", "step over", [](CDapDebugSession& dap_session, int thread_id) {
            return dap_session.sendStepOverRequest({
                .thread_id = thread_id,
            });
        });
    };

    const auto stepIntoActiveDapSession = [&]() {
        executeDapControl("Step into", "step into", [](CDapDebugSession& dap_session, int thread_id) {
            return dap_session.sendStepIntoRequest({
                .thread_id = thread_id,
            });
        });
    };

    const auto stepOutActiveDapSession = [&]() {
        executeDapControl("Step out", "step out", [](CDapDebugSession& dap_session, int thread_id) {
            return dap_session.sendStepOutRequest({
                .thread_id = thread_id,
            });
        });
    };

    const auto pauseActiveDapSession = [&]() {
        auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get());
        if (dap_session == nullptr) {
            transient_status_message = "Pause is only available in DAP sessions";
            return;
        }
        if (!isDapControlRunning()) {
            transient_status_message = "Pause is only available while debugger is running";
            return;
        }
        if (debug_selection.thread_id == 0) {
            transient_status_message = "Pause failed: no active thread";
            return;
        }

        if (!dap_session->sendPauseRequest({
                .thread_id = static_cast<int>(debug_selection.thread_id),
            })) {
            transient_status_message = "Pause failed: " + dap_session->getLastError();
            return;
        }

        async_dap_control_state.pause_requested.store(true);
        transient_status_message = "Pause requested";
    };

    const auto disconnectActiveDapSession = [&](bool exit_after_disconnect) {
        auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get());
        if (dap_session == nullptr) {
            if (exit_after_disconnect) {
                screen.Exit();
                return;
            }
            transient_status_message = "Terminate is only available in DAP sessions";
            return;
        }
        if (session_state == eDebuggerSessionState::TERMINATED || session_state == eDebuggerSessionState::ERROR) {
            if (exit_after_disconnect) {
                screen.Exit();
                return;
            }
            transient_status_message = "Session is already terminated";
            return;
        }
        if (session_state == eDebuggerSessionState::TERMINATING) {
            async_dap_control_state.quit_requested.store(exit_after_disconnect || async_dap_control_state.quit_requested.load());
            transient_status_message = "Terminate already requested";
            return;
        }
        if (isDapControlRunning()) {
            if (!dap_session->sendDisconnectRequest({.terminate_debuggee = true})) {
                transient_status_message = "Terminate failed: " + dap_session->getLastError();
                return;
            }

            session_state = eDebuggerSessionState::TERMINATING;
            async_dap_control_state.terminate_requested.store(true);
            async_dap_control_state.pause_requested.store(false);
            async_dap_control_state.quit_requested.store(exit_after_disconnect);
            transient_status_message = "Terminate requested";
            return;
        }

        session_state = eDebuggerSessionState::TERMINATING;
        async_dap_control_state.running.store(true);
        async_dap_control_state.pause_requested.store(false);
        async_dap_control_state.terminate_requested.store(true);
        async_dap_control_state.quit_requested.store(exit_after_disconnect);
        transient_status_message = "Terminate requested";

        async_dap_control_state.worker = std::jthread([&, dap_session](std::stop_token) {
            bool        success = false;
            std::string error_message;

            if (!dap_session->sendDisconnectRequest({.terminate_debuggee = true})) {
                error_message = "Terminate failed: " + dap_session->getLastError();
            } else if (!dap_session->waitForTerminatedEvent()) {
                error_message = "Wait after terminate failed: " + dap_session->getLastError();
            } else {
                success = true;
            }

            screen.Post(Closure([&, success, error_message] {
                async_dap_control_state.running.store(false);
                async_dap_control_state.pause_requested.store(false);
                async_dap_control_state.terminate_requested.store(false);
                if (!success) {
                    async_dap_control_state.quit_requested.store(false);
                    session_state            = eDebuggerSessionState::ERROR;
                    transient_status_message = error_message;
                    return;
                }

                session_state            = eDebuggerSessionState::TERMINATED;
                base_session_status      = "Session terminated";
                transient_status_message = "Terminated";
                clearDebuggerPanes();
                if (async_dap_control_state.quit_requested.exchange(false)) {
                    screen.Exit();
                }
            }));
            screen.PostEvent(Event::Custom);
        });
    };

    const auto terminateActiveDapSession = [&]() { disconnectActiveDapSession(false); };

    const auto quitApplication = [&]() {
        if (session_state == eDebuggerSessionState::TERMINATING) {
            async_dap_control_state.quit_requested.store(true);
            transient_status_message = "Waiting for debugger disconnect before quitting";
            return;
        }
        if (isDapControlRunning() || session_state == eDebuggerSessionState::STOPPED || session_state == eDebuggerSessionState::RUNNING) {
            disconnectActiveDapSession(true);
            return;
        }

        screen.Exit();
    };

    const auto restartSession = [&]() {
        if (isDapControlRunning() || session_state == eDebuggerSessionState::RUNNING || session_state == eDebuggerSessionState::TERMINATING) {
            transient_status_message = "Restart is unavailable while debugger is running";
            return;
        }

        if (app_config.session_mode == eSessionMode::MOCK) {
            applyBootstrapResult(bootstrapSession(app_config), "Mock session restarted");
            refreshAllPanes();
            return;
        }

        if (session_state == eDebuggerSessionState::STOPPED) {
            auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get());
            if (dap_session != nullptr) {
                if (!dap_session->sendDisconnectRequest({.terminate_debuggee = true})) {
                    session_state            = eDebuggerSessionState::ERROR;
                    transient_status_message = "Restart failed: " + dap_session->getLastError();
                    return;
                }

                if (!dap_session->waitForTerminatedEvent()) {
                    session_state            = eDebuggerSessionState::ERROR;
                    transient_status_message = "Restart failed while stopping current session: " + dap_session->getLastError();
                    return;
                }
            }
        }

        applyBootstrapResult(bootstrapSession(app_config), "Session restarted");
        if (session_state == eDebuggerSessionState::ERROR) {
            transient_status_message = "Restart failed: " + base_session_status;
        }
        refreshAllPanes();
    };

    refreshAllPanes();

    auto renderer = Renderer([&] {
        return renderRoundtableLayout({
            .locals_pane          = locals_pane,
            .threads_pane         = threads_pane,
            .stack_pane           = stack_pane,
            .memory_view_pane     = memory_view_pane,
            .disassembly_pane     = disassembly_pane,
            .watch_list_pane      = watch_list_pane,
            .breakpoints_pane     = breakpoints_pane,
            .view_visibility      = view_visibility,
            .memory_context       = memory_context,
            .theme                = app_theme,
            .keybindings          = keybindings,
            .launch_profiles      = app_config.launch_profiles,
            .stopped_context      = stopped_context,
            .debug_selection      = debug_selection,
            .prompt_state         = prompt_state,
            .theme_picker_state   = theme_picker_state,
            .profile_picker_state = profile_picker_state,
            .dashboard_state      = {.active = dashboard_active, .config_path = display_config_path, .launch_profile_count = app_config.launch_profiles.size()},
            .focused_pane         = focused_pane,
            .current_status       = transient_status_message.empty() ? base_session_status : transient_status_message,
            .leader_pending       = leader_pending,
        });
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
                        focused_pane             = eFocusPane::WATCH_LIST;
                        memory_navigation_offset = 0;
                    }
                } else if (prompt_state.mode == ePromptMode::EDIT_WATCH) {
                    if (!watch_expressions.empty() && watch_list_pane.selected_index < watch_expressions.size() && !prompt_state.input.empty()) {
                        watch_expressions[watch_list_pane.selected_index].expression = prompt_state.input;
                        focused_pane                                                 = eFocusPane::WATCH_LIST;
                        memory_navigation_offset                                     = 0;
                    }
                } else if (prompt_state.mode == ePromptMode::ADD_BREAKPOINT) {
                    const auto breakpoint = parseSourceBreakpointConfig(prompt_state.input);
                    if (breakpoint.has_value()) {
                        app_config.breakpoints.push_back(breakpoint.value());
                        breakpoints_pane.selected_index = app_config.breakpoints.empty() ? 0UL : app_config.breakpoints.size() - 1;
                        focused_pane                    = eFocusPane::BREAKPOINTS;
                        if (applyBreakpointsToActiveSession()) {
                            transient_status_message = "Breakpoint added: " + describeBreakpoint(breakpoint.value());
                        }
                    } else {
                        transient_status_message = "Invalid breakpoint, expected source.cpp:line";
                    }
                } else if (prompt_state.mode == ePromptMode::MEMORY_TARGET) {
                    manual_memory_target     = prompt_state.input;
                    focused_pane             = eFocusPane::MEMORY_VIEW;
                    memory_navigation_offset = 0;
                } else if (prompt_state.mode == ePromptMode::LAUNCH_PROGRAM) {
                    launchProgram(prompt_state.input);
                }

                prompt_state = {};
                if (!dashboard_active) {
                    refreshAllPanes();
                }
                return true;
            }

            if (event == Event::Tab && prompt_state.mode == ePromptMode::LAUNCH_PROGRAM) {
                if (const auto completed_path = completePathInput(prompt_state.input); completed_path.has_value()) {
                    prompt_state.input            = completed_path.value();
                    prompt_state.cursor_index     = prompt_state.input.size();
                    prompt_state.replace_on_input = false;
                }
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

        if (profile_picker_state.active) {
            if (event == Event::Escape) {
                profile_picker_state     = {};
                transient_status_message = {};
                return true;
            }

            if (event == Event::Return) {
                if (profile_picker_state.selected_index >= app_config.launch_profiles.size()) {
                    profile_picker_state     = {};
                    transient_status_message = "No launch profiles configured";
                    return true;
                }

                const std::string profile_name = app_config.launch_profiles[profile_picker_state.selected_index].name;
                profile_picker_state           = {};
                if (!applyLaunchProfile(app_config, profile_name)) {
                    transient_status_message = "Launch profile not found: " + profile_name;
                    return true;
                }

                dashboard_active = false;
                restartSession();
                return true;
            }

            const auto move_up   = event == Event::ArrowUp || event == Event::Character('k');
            const auto move_down = event == Event::ArrowDown || event == Event::Character('j');

            if (move_up && profile_picker_state.selected_index > 0) {
                --profile_picker_state.selected_index;
                profile_picker_state.replace_on_input = true;
            } else if (move_down && profile_picker_state.selected_index + 1 < app_config.launch_profiles.size()) {
                ++profile_picker_state.selected_index;
                profile_picker_state.replace_on_input = true;
            } else if (event == Event::Backspace) {
                if (!profile_picker_state.search_query.empty()) {
                    profile_picker_state.search_query.pop_back();
                    selectProfileBySearch(profile_picker_state, app_config.launch_profiles);
                }
                profile_picker_state.replace_on_input = false;
            } else if (event.is_character()) {
                if (profile_picker_state.replace_on_input) {
                    profile_picker_state.search_query = event.character();
                } else {
                    profile_picker_state.search_query += event.character();
                }
                profile_picker_state.replace_on_input = false;
                selectProfileBySearch(profile_picker_state, app_config.launch_profiles);
            } else if (!(move_up || move_down)) {
                return true;
            }

            if (profile_picker_state.selected_index < app_config.launch_profiles.size()) {
                transient_status_message = "Profile: " + app_config.launch_profiles[profile_picker_state.selected_index].name;
            }
            return true;
        }

        if (watch_action_mode != eWatchActionMode::NONE && focused_pane == eFocusPane::WATCH_LIST) {
            if (event == Event::Escape) {
                watch_action_mode        = eWatchActionMode::NONE;
                transient_status_message = {};
                return true;
            }

            if (event == Event::Return) {
                performWatchAction(watch_action_mode);

                watch_action_mode = eWatchActionMode::NONE;
                return true;
            }

            const bool handled = handleVerticalNavigation(event, watch_list_pane);
            if (handled) {
                refreshAllPanes();
            }
            return true;
        }

        if (breakpoint_action_mode != eBreakpointActionMode::NONE && focused_pane == eFocusPane::BREAKPOINTS) {
            if (event == Event::Escape) {
                breakpoint_action_mode   = eBreakpointActionMode::NONE;
                transient_status_message = {};
                return true;
            }

            if (event == Event::Return) {
                if (breakpoint_action_mode == eBreakpointActionMode::REMOVE && breakpoints_pane.selected_index < app_config.breakpoints.size()) {
                    const auto removed_breakpoint  = app_config.breakpoints[breakpoints_pane.selected_index];
                    const auto removed_source_path = removed_breakpoint.source_path;
                    app_config.breakpoints.erase(app_config.breakpoints.begin() + static_cast<std::ptrdiff_t>(breakpoints_pane.selected_index));
                    if (breakpoints_pane.selected_index > 0 && breakpoints_pane.selected_index >= app_config.breakpoints.size()) {
                        --breakpoints_pane.selected_index;
                    }
                    const bool applied = applyBreakpointsToActiveSession();
                    const bool cleared = hasBreakpointForSource(removed_source_path) || clearBreakpointSourceInActiveSession(removed_source_path);
                    if (applied && cleared) {
                        transient_status_message = "Breakpoint removed: " + describeBreakpoint(removed_breakpoint);
                    }
                    refreshAllPanes();
                }

                breakpoint_action_mode = eBreakpointActionMode::NONE;
                return true;
            }

            const bool handled = handleVerticalNavigation(event, breakpoints_pane);
            if (handled) {
                refreshAllPanes();
            }
            return true;
        }

        if (dashboard_active) {
            if (view_visibility.show_shortcuts_overlay && (event == Event::Escape || event == Event::Character('?'))) {
                view_visibility.show_shortcuts_overlay = false;
                return true;
            }

            if (event == Event::Character('q')) {
                screen.Exit();
                return true;
            }

            if (event == Event::Character('b')) {
                prompt_state = beginPrompt(ePromptMode::LAUNCH_PROGRAM, std::filesystem::current_path().string() + "/", true);
                return true;
            }

            if (event == Event::Character('p')) {
                if (app_config.launch_profiles.empty()) {
                    transient_status_message = "No launch profiles configured";
                    return true;
                }

                profile_picker_state = {
                    .active           = true,
                    .selected_index   = launchProfileIndex(app_config.launch_profiles, app_config.active_profile),
                    .search_query     = {},
                    .replace_on_input = true,
                };
                transient_status_message = "Profile: " + app_config.launch_profiles[profile_picker_state.selected_index].name;
                return true;
            }

            if (event == Event::Character('i')) {
                const auto init_result = initializeUserAppConfig(false);
                if (init_result.ok) {
                    display_config_path = std::filesystem::absolute(init_result.path).string();
                }
                transient_status_message = init_result.message;
                return true;
            }

            if (event == Event::Character('r')) {
                reloadConfig();
                return true;
            }

            if (event == Event::Character('?')) {
                view_visibility.show_shortcuts_overlay = !view_visibility.show_shortcuts_overlay;
                return true;
            }

            return true;
        }

        if (event == Event::Character('q')) {
            quitApplication();
            return true;
        }

        if (event == Event::Character('r')) {
            if (isDapControlRunning()) {
                transient_status_message = "Refresh is disabled while debugger is running";
                return true;
            }
            if (session_state == eDebuggerSessionState::TERMINATED) {
                transient_status_message = "Session terminated; reload config to start again";
                return true;
            }

            refreshAllPanes();
            return true;
        }

        if (event == Event::F5) {
            continueActiveDapSession();
            return true;
        }

        if (event == Event::F10) {
            stepOverActiveDapSession();
            return true;
        }

        if (event == Event::F11) {
            stepIntoActiveDapSession();
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
                        chooseOrPerformWatchAction(eWatchActionMode::EDIT, "Choose watch with j/k, press Return to edit, Esc to cancel");
                        return true;
                    }
                    if (command.value() == eCommand::REMOVE_WATCH) {
                        chooseOrPerformWatchAction(eWatchActionMode::REMOVE, "Choose watch with j/k, press Return to remove, Esc to cancel");
                        return true;
                    }
                    if (command.value() == eCommand::MOVE_WATCH_UP) {
                        chooseOrPerformWatchAction(eWatchActionMode::MOVE_UP, "Choose watch with j/k, press Return to move up, Esc to cancel");
                        return true;
                    }
                    if (command.value() == eCommand::MOVE_WATCH_DOWN) {
                        chooseOrPerformWatchAction(eWatchActionMode::MOVE_DOWN, "Choose watch with j/k, press Return to move down, Esc to cancel");
                        return true;
                    }
                    if (command.value() == eCommand::DUPLICATE_WATCH) {
                        chooseOrPerformWatchAction(eWatchActionMode::DUPLICATE, "Choose watch with j/k, press Return to duplicate, Esc to cancel");
                        return true;
                    }
                    if (command.value() == eCommand::ADD_BREAKPOINT) {
                        const auto current_location_breakpoint = sourceBreakpointTextForLocation(stopped_context, debug_selection);
                        prompt_state                           = beginPrompt(ePromptMode::ADD_BREAKPOINT, current_location_breakpoint, !current_location_breakpoint.empty());
                        return true;
                    }
                    if (command.value() == eCommand::REMOVE_BREAKPOINT) {
                        if (app_config.breakpoints.empty()) {
                            transient_status_message = "No breakpoints to remove";
                            return true;
                        }
                        if (focused_pane != eFocusPane::BREAKPOINTS) {
                            focused_pane             = eFocusPane::BREAKPOINTS;
                            breakpoint_action_mode   = eBreakpointActionMode::REMOVE;
                            transient_status_message = "Choose breakpoint with j/k, press Return to remove, Esc to cancel";
                            refreshAllPanes();
                            return true;
                        }

                        const auto removed_breakpoint  = app_config.breakpoints[breakpoints_pane.selected_index];
                        const auto removed_source_path = removed_breakpoint.source_path;
                        app_config.breakpoints.erase(app_config.breakpoints.begin() + static_cast<std::ptrdiff_t>(breakpoints_pane.selected_index));
                        if (breakpoints_pane.selected_index > 0 && breakpoints_pane.selected_index >= app_config.breakpoints.size()) {
                            --breakpoints_pane.selected_index;
                        }
                        const bool applied = applyBreakpointsToActiveSession();
                        const bool cleared = hasBreakpointForSource(removed_source_path) || clearBreakpointSourceInActiveSession(removed_source_path);
                        if (applied && cleared) {
                            transient_status_message = "Breakpoint removed: " + describeBreakpoint(removed_breakpoint);
                        }
                        refreshAllPanes();
                        return true;
                    }
                    if (command.value() == eCommand::TOGGLE_BREAKPOINT) {
                        if (app_config.breakpoints.empty()) {
                            transient_status_message = "No breakpoints to toggle";
                            return true;
                        }
                        if (focused_pane != eFocusPane::BREAKPOINTS) {
                            focused_pane             = eFocusPane::BREAKPOINTS;
                            transient_status_message = "Choose breakpoint with j/k, press Space E to toggle";
                            refreshAllPanes();
                            return true;
                        }
                        if (breakpoints_pane.selected_index < app_config.breakpoints.size()) {
                            auto& breakpoint   = app_config.breakpoints[breakpoints_pane.selected_index];
                            breakpoint.enabled = !breakpoint.enabled;
                            if (applyBreakpointsToActiveSession()) {
                                transient_status_message = std::string("Breakpoint ") + (breakpoint.enabled ? "enabled: " : "disabled: ") + describeBreakpoint(breakpoint);
                            }
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
                    if (command.value() == eCommand::CHOOSE_PROFILE) {
                        if (app_config.launch_profiles.empty()) {
                            transient_status_message = "No launch profiles configured";
                            return true;
                        }
                        if (isDapControlRunning() || session_state == eDebuggerSessionState::RUNNING || session_state == eDebuggerSessionState::TERMINATING) {
                            transient_status_message = "Profile picker is unavailable while debugger is running";
                            return true;
                        }
                        profile_picker_state = {
                            .active           = true,
                            .selected_index   = launchProfileIndex(app_config.launch_profiles, app_config.active_profile),
                            .search_query     = {},
                            .replace_on_input = true,
                        };
                        transient_status_message = "Profile: " + app_config.launch_profiles[profile_picker_state.selected_index].name;
                        return true;
                    }
                    if (command.value() == eCommand::RELOAD_CONFIG) {
                        reloadConfig();
                        return true;
                    }
                    if (command.value() == eCommand::CONTINUE_EXECUTION) {
                        continueActiveDapSession();
                        return true;
                    }
                    if (command.value() == eCommand::STEP_OVER) {
                        stepOverActiveDapSession();
                        return true;
                    }
                    if (command.value() == eCommand::STEP_INTO) {
                        stepIntoActiveDapSession();
                        return true;
                    }
                    if (command.value() == eCommand::STEP_OUT) {
                        stepOutActiveDapSession();
                        return true;
                    }
                    if (command.value() == eCommand::PAUSE_EXECUTION) {
                        pauseActiveDapSession();
                        return true;
                    }
                    if (command.value() == eCommand::TERMINATE_SESSION) {
                        terminateActiveDapSession();
                        return true;
                    }
                    if (command.value() == eCommand::RESTART_SESSION) {
                        restartSession();
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

        if (focused_pane == eFocusPane::THREADS) {
            const bool handled = handleVerticalNavigation(event, threads_pane);
            if (handled) {
                if (threads_pane.selected_index < stopped_context.threads.size()) {
                    debug_selection.thread_id   = stopped_context.threads[threads_pane.selected_index].id;
                    debug_selection.frame_index = 0;
                    memory_navigation_offset    = 0;

                    if (auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get()); dap_session != nullptr && session_state == eDebuggerSessionState::STOPPED) {
                        stopped_context = updateDapStoppedContext(*dap_session, debug_selection, disassembly_start_address, disassembly_memory_reference);
                    }

                    refreshAllPanes();
                }
            }
            return handled;
        }

        if (focused_pane == eFocusPane::STACK) {
            const bool handled = handleVerticalNavigation(event, stack_pane);
            if (handled) {
                if (stack_pane.selected_index < stopped_context.stack_frames.size()) {
                    debug_selection.frame_index = stack_pane.selected_index;
                    stopped_context.location    = stackFrameLocation(stopped_context.stack_frames[stack_pane.selected_index]);
                    memory_navigation_offset    = 0;
                    refreshAllPanes();
                }
            }
            return handled;
        }

        if (focused_pane == eFocusPane::LOCALS) {
            const bool handled = handleVerticalNavigation(event, locals_pane);
            if (handled) {
                memory_navigation_offset = 0;
                refreshAllPanes();
            }
            return handled;
        }
        if (focused_pane == eFocusPane::WATCH_LIST) {
            const bool handled = handleVerticalNavigation(event, watch_list_pane);
            if (handled) {
                memory_navigation_offset = 0;
                refreshAllPanes();
            }
            return handled;
        }
        if (focused_pane == eFocusPane::BREAKPOINTS) {
            const bool handled = handleVerticalNavigation(event, breakpoints_pane);
            if (handled) {
                refreshAllPanes();
            }
            return handled;
        }
        if (focused_pane == eFocusPane::MEMORY_VIEW) {
            if (const auto navigation_delta = memoryNavigationDelta(event, 8, memory_view_pane.rows.size()); navigation_delta.has_value()) {
                memory_navigation_offset += navigation_delta.value();
                refreshAllPanes();
                return true;
            }
            return handleVerticalNavigation(event, memory_view_pane);
        }
        if (focused_pane == eFocusPane::DISASSEMBLY_VIEW) {
            return handleVerticalNavigation(event, disassembly_pane);
        }

        return false;
    });

    screen.Loop(component);
}
