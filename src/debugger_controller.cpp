#include "debugger_controller.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iterator>
#include <map>
#include <optional>
#include <vector>

#include "codelldb_locator.hpp"

namespace {

    bool initializeDapSession(CDapDebugSession& dap_session, std::string& error_message) {
        if (!dap_session.connect()) {
            error_message = "DAP connect failed: " + dap_session.getLastError();
            return false;
        }

        if (!dap_session.initialize()) {
            error_message = "DAP initialize failed: " + dap_session.getLastError();
            return false;
        }

        return true;
    }

    bool finalizeDapSessionStop(CDapDebugSession& dap_session, bool continue_once, SDebugSelection& selection, std::uint64_t& disassembly_start_address,
                                std::string& disassembly_memory_reference, SStoppedContext& stopped_context, std::string& error_message, const std::string& continue_failure_prefix,
                                const std::string& wait_after_continue_prefix) {
        if (!dap_session.configurationDone()) {
            error_message = "DAP configurationDone failed: " + dap_session.getLastError();
            return false;
        }

        if (!dap_session.waitForStoppedEvent()) {
            error_message = "DAP waitForStoppedEvent failed: " + dap_session.getLastError();
            return false;
        }

        stopped_context = updateDapStoppedContext(dap_session, selection, disassembly_start_address, disassembly_memory_reference);

        if (continue_once && selection.thread_id != 0) {
            const auto continue_response = dap_session.continueExecution({
                .thread_id = static_cast<int>(selection.thread_id),
            });

            if (!continue_response.success) {
                error_message = continue_failure_prefix + continue_response.error_message;
                return false;
            }

            if (!dap_session.waitForStoppedEvent()) {
                error_message = wait_after_continue_prefix + dap_session.getLastError();
                return false;
            }

            stopped_context = updateDapStoppedContext(dap_session, selection, disassembly_start_address, disassembly_memory_reference);
        }

        return true;
    }

} // namespace

bool configureDapBreakpoints(CDapDebugSession& dap_session, std::vector<SSourceBreakpointConfig>& breakpoints, std::string& error_message) {
    std::map<std::string, std::vector<SDapSourceBreakpoint>> breakpoints_by_source;
    std::map<std::string, std::vector<std::size_t>>          breakpoint_indices_by_source;

    for (std::size_t index = 0; index < breakpoints.size(); ++index) {
        auto& breakpoint                = breakpoints[index];
        breakpoint.adapter_status_known = false;
        breakpoint.adapter_verified     = false;
        breakpoint.adapter_line         = 0;
        breakpoint.adapter_message.clear();

        if (breakpoint.line <= 0 || breakpoint.source_path.empty()) {
            continue;
        }

        const auto source_path        = std::filesystem::absolute(breakpoint.source_path).string();
        auto&      source_breakpoints = breakpoints_by_source[source_path];
        if (breakpoint.enabled) {
            source_breakpoints.push_back({
                .line = breakpoint.line,
            });
            breakpoint_indices_by_source[source_path].push_back(index);
        }
    }

    for (const auto& [source_path, source_breakpoints] : breakpoints_by_source) {
        const auto response = dap_session.setBreakpoints({
            .source_path = source_path,
            .breakpoints = source_breakpoints,
        });

        if (!response.success) {
            error_message = "DAP setBreakpoints failed: " + response.error_message;
            return false;
        }

        const auto indices_iterator = breakpoint_indices_by_source.find(source_path);
        if (indices_iterator == breakpoint_indices_by_source.end()) {
            continue;
        }

        const auto& breakpoint_indices = indices_iterator->second;
        const auto  status_count       = std::min(breakpoint_indices.size(), response.breakpoints.size());
        for (std::size_t status_index = 0; status_index < status_count; ++status_index) {
            auto&       breakpoint          = breakpoints[breakpoint_indices[status_index]];
            const auto& resolved_breakpoint = response.breakpoints[status_index];
            breakpoint.adapter_status_known = true;
            breakpoint.adapter_verified     = resolved_breakpoint.verified;
            breakpoint.adapter_line         = resolved_breakpoint.line;
            breakpoint.adapter_message      = resolved_breakpoint.message;
        }
    }

    return true;
}

SStoppedContext updateDapStoppedContext(CDapDebugSession& dap_session, SDebugSelection& selection, std::uint64_t& disassembly_start_address,
                                        std::string& disassembly_memory_reference) {
    SStoppedContext stopped_context = {};

    auto            threads = dap_session.getThreads();
    if (threads.success && !threads.threads.empty()) {
        stopped_context.threads.reserve(threads.threads.size());
        for (const auto& thread : threads.threads) {
            stopped_context.threads.push_back({
                .id   = thread.id,
                .name = thread.name,
            });
        }

        const auto selected_thread = std::ranges::find_if(threads.threads, [&](const SDapThread& thread) { return thread.id == selection.thread_id; });
        if (selected_thread == threads.threads.end()) {
            selection.thread_id   = threads.threads.front().id;
            selection.frame_index = 0;
        }
    }

    disassembly_start_address = 0x401000;
    disassembly_memory_reference.clear();

    if (selection.thread_id != 0) {
        const auto stack_trace = dap_session.getStackTrace({
            .thread_id   = static_cast<int>(selection.thread_id),
            .start_frame = 0,
            .levels      = 16,
        });

        if (stack_trace.success && !stack_trace.stack_frames.empty()) {
            const auto main_frame_iterator     = std::ranges::find_if(stack_trace.stack_frames, [](const SDapStackFrame& frame) { return frame.name == "main"; });
            const auto selected_frame_iterator = main_frame_iterator != stack_trace.stack_frames.end() ? main_frame_iterator : stack_trace.stack_frames.begin();

            stopped_context.stack_frames.reserve(stack_trace.stack_frames.size());
            for (const auto& frame : stack_trace.stack_frames) {
                stopped_context.stack_frames.push_back({
                    .function_name = frame.name,
                    .source_path   = frame.source_path,
                    .line          = frame.line,
                    .column        = frame.column,
                });
            }

            selection.frame_index    = static_cast<std::size_t>(std::distance(stack_trace.stack_frames.begin(), selected_frame_iterator));
            stopped_context.location = {
                .function_name = selected_frame_iterator->name,
                .source_path   = selected_frame_iterator->source_path,
                .line          = selected_frame_iterator->line,
                .column        = selected_frame_iterator->column,
            };

            if (!selected_frame_iterator->instruction_pointer_reference.empty()) {
                disassembly_memory_reference = selected_frame_iterator->instruction_pointer_reference;
                try {
                    disassembly_start_address = std::stoull(selected_frame_iterator->instruction_pointer_reference, nullptr, 0);
                } catch (const std::exception&) { disassembly_start_address = 0x401000; }
            }
        }
    }

    return stopped_context;
}

SSessionBootstrapResult bootstrapSession(SAppConfig& app_config) {
    if (app_config.session_mode == eSessionMode::MOCK) {
        return {
            .session                      = std::make_unique<CMockDebugSession>(),
            .selection                    = {.thread_id = 1, .frame_index = 0},
            .disassembly_start_address    = 0x401000,
            .disassembly_memory_reference = "",
            .stopped_context =
                {
                    .location = {.function_name = "main", .source_path = "mock_sample.cpp", .line = 12, .column = 5},
                    .threads  = {{.id = 1, .name = "main thread"}},
                    .stack_frames =
                        {
                            {.function_name = "main", .source_path = "mock_sample.cpp", .line = 12, .column = 5},
                            {.function_name = "_start", .source_path = "", .line = 0, .column = 0},
                        },
                },
            .status_message = "Mock session",
            .state          = eDebuggerSessionState::MOCK,
        };
    }

    const auto detected_install = app_config.codelldb_auto_detect.enabled ? findCodeLldbInstall(app_config.codelldb_auto_detect.candidate_roots) : std::nullopt;
    const auto resolved_command = app_config.dap_launch.command.empty() && detected_install.has_value() ? detected_install->command : app_config.dap_launch.command;
    const auto resolved_liblldb_path =
        app_config.dap_launch.liblldb_path.empty() && detected_install.has_value() ? detected_install->liblldb_path : app_config.dap_launch.liblldb_path;
    auto dap_session = std::make_unique<CDapDebugSession>(std::make_unique<CTcpDapTransport>(),
                                                          SDapEndpointConfig{
                                                              .transport_kind = eDapTransportKind::TCP,
                                                              .command        = resolved_command,
                                                              .arguments      = {"--liblldb", resolved_liblldb_path},
                                                              .auth_token     = "",
                                                          });

    if (resolved_command.empty() || resolved_liblldb_path.empty() || app_config.dap_launch.program.empty()) {
        return {
            .session                      = std::move(dap_session),
            .selection                    = {},
            .disassembly_start_address    = 0x401000,
            .disassembly_memory_reference = "",
            .stopped_context              = {},
            .status_message               = "DAP launch config is incomplete and autodetect failed",
            .state                        = eDebuggerSessionState::ERROR,
        };
    }

    std::string bootstrap_error_message;
    if (!initializeDapSession(*dap_session, bootstrap_error_message)) {
        return {
            .session                      = std::move(dap_session),
            .selection                    = {},
            .disassembly_start_address    = 0x401000,
            .disassembly_memory_reference = "",
            .stopped_context              = {},
            .status_message               = bootstrap_error_message,
            .state                        = eDebuggerSessionState::ERROR,
        };
    }

    SDebugSelection selection                 = {};
    std::uint64_t   disassembly_start_address = 0x401000;
    std::string     disassembly_memory_reference;
    SStoppedContext stopped_context;

    if (!dap_session->launch({
            .program           = app_config.dap_launch.program,
            .arguments         = app_config.dap_launch.arguments,
            .working_directory = app_config.dap_launch.working_directory,
            .stop_on_entry     = app_config.dap_launch.stop_on_entry,
        })) {
        return {
            .session                      = std::move(dap_session),
            .selection                    = {},
            .disassembly_start_address    = 0x401000,
            .disassembly_memory_reference = "",
            .stopped_context              = {},
            .status_message               = "DAP launch failed: " + dap_session->getLastError(),
            .state                        = eDebuggerSessionState::ERROR,
        };
    }

    if (!configureDapBreakpoints(*dap_session, app_config.breakpoints, bootstrap_error_message)) {
        return {
            .session                      = std::move(dap_session),
            .selection                    = {},
            .disassembly_start_address    = 0x401000,
            .disassembly_memory_reference = "",
            .stopped_context              = {},
            .status_message               = bootstrap_error_message,
            .state                        = eDebuggerSessionState::ERROR,
        };
    }

    if (!finalizeDapSessionStop(*dap_session, app_config.dap_launch.continue_once, selection, disassembly_start_address, disassembly_memory_reference, stopped_context,
                                bootstrap_error_message, "DAP continue failed: ", "DAP wait after continue failed: ")) {
        return {
            .session                      = std::move(dap_session),
            .selection                    = selection,
            .disassembly_start_address    = disassembly_start_address,
            .disassembly_memory_reference = disassembly_memory_reference,
            .stopped_context              = stopped_context,
            .status_message               = bootstrap_error_message,
            .state                        = eDebuggerSessionState::ERROR,
        };
    }

    return {
        .session                      = std::move(dap_session),
        .selection                    = selection,
        .disassembly_start_address    = disassembly_start_address,
        .disassembly_memory_reference = disassembly_memory_reference,
        .stopped_context              = stopped_context,
        .status_message               = selection.thread_id != 0 ? "DAP launch session" : "DAP launch session without active thread",
        .state                        = eDebuggerSessionState::STOPPED,
    };
}
