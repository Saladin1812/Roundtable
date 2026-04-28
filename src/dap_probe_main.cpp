#include <iostream>
#include <memory>
#include <functional>
#include <string_view>

#include "dap_session.hpp"

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "Usage: roundtable_dap_probe <launch|attach> <adapter-path> <liblldb-path> <program-path-or-pid>\n";
        return 1;
    }

    SDapEndpointConfig endpoint_config = {
        .transport_kind = eDapTransportKind::TCP,
        .command        = argv[2],
        .arguments =
            {
                "--liblldb",
                argv[3],
                "--settings",
                R"({"evaluateForHovers":true,"commandCompletions":true})",
            },
        .auth_token = "roundtable-probe-token",
    };

    CDapDebugSession dap_session(std::make_unique<CTcpDapTransport>(), endpoint_config);

    std::cerr << "probe: connect\n";
    if (!dap_session.connect()) {
        std::cerr << "connect failed: " << dap_session.getLastError() << '\n';
        return 2;
    }

    std::cerr << "probe: initialize\n";
    if (!dap_session.initialize()) {
        std::cerr << "initialize failed: " << dap_session.getLastError() << '\n';
        return 3;
    }

    const std::string_view mode = argv[1];
    if (mode == "launch") {
        std::cerr << "probe: launch\n";
        if (!dap_session.launch({
                .program           = argv[4],
                .arguments         = {},
                .working_directory = ".",
                .stop_on_entry     = true,
            })) {
            std::cerr << "launch failed: " << dap_session.getLastError() << '\n';
            return 4;
        }
    } else if (mode == "attach") {
        std::cerr << "probe: attach\n";
        if (!dap_session.attach({
                .process_id    = std::stoll(argv[4]),
                .stop_on_entry = true,
            })) {
            std::cerr << "attach failed: " << dap_session.getLastError() << '\n';
            return 4;
        }
    } else {
        std::cerr << "unknown mode: " << mode << '\n';
        return 4;
    }

    std::cerr << "probe: configurationDone\n";
    if (!dap_session.sendConfigurationDoneRequest()) {
        std::cerr << "sendConfigurationDoneRequest failed: " << dap_session.getLastError() << '\n';
        return 5;
    }

    std::function<int(int)> query_stopped_state = [&](int result_base_code) -> int {
        std::cerr << "probe: getThreads\n";
        const auto threads_response = dap_session.getThreads();
        if (!threads_response.success) {
            std::cerr << "getThreads failed: " << threads_response.error_message << '\n';
            return result_base_code;
        }

        std::cout << "threads ok\n";
        std::cout << "thread_count=" << threads_response.threads.size() << '\n';
        for (const auto& thread : threads_response.threads) {
            std::cout << "thread id=" << thread.id << " name=" << thread.name << '\n';
        }

        if (threads_response.threads.empty()) {
            return 0;
        }

        std::cerr << "probe: getStackTrace\n";
        const auto stack_trace_response = dap_session.getStackTrace({
            .thread_id   = threads_response.threads.front().id,
            .start_frame = 0,
            .levels      = 10,
        });

        if (!stack_trace_response.success) {
            std::cerr << "getStackTrace failed: " << stack_trace_response.error_message << '\n';
            return result_base_code + 1;
        }

        std::cout << "stack_frames ok\n";
        std::cout << "stack_frame_count=" << stack_trace_response.stack_frames.size() << '\n';
        for (const auto& stack_frame : stack_trace_response.stack_frames) {
            std::cout << "frame id=" << stack_frame.id << " name=" << stack_frame.name << " path=" << stack_frame.source_path << " line=" << stack_frame.line
                      << " column=" << stack_frame.column << '\n';
        }

        std::size_t selected_frame_index = 0;
        for (std::size_t frame_index = 0; frame_index < stack_trace_response.stack_frames.size(); ++frame_index) {
            if (stack_trace_response.stack_frames[frame_index].name == "main") {
                selected_frame_index = frame_index;
                break;
            }
        }

        std::cerr << "probe: getLocals\n";
        const auto locals = dap_session.getLocals({
            .thread_id   = threads_response.threads.front().id,
            .frame_index = selected_frame_index,
        });

        std::cout << "locals_count=" << locals.size() << '\n';
        for (const auto& local : locals) {
            std::cout << "local name=" << local.name << " value=" << local.value << " type=" << local.type << '\n';
        }

        const bool should_continue_to_user_frame =
            !stack_trace_response.stack_frames.empty() && (stack_trace_response.stack_frames.front().name.find("_start") != std::string::npos || locals.empty());

        if (!should_continue_to_user_frame) {
            std::cerr << "probe: evaluateWatches\n";
            const auto watch_results = dap_session.evaluateWatches(
                {
                    .thread_id   = threads_response.threads.front().id,
                    .frame_index = selected_frame_index,
                },
                {
                    {.expression = "sample_value"},
                    {.expression = "sample_bytes"},
                });

            std::cout << "watch_count=" << watch_results.size() << '\n';
            for (const auto& watch_result : watch_results) {
                std::cout << "watch expression=" << watch_result.expression << " value=" << watch_result.value << " type=" << watch_result.type
                          << " error=" << watch_result.error_message << '\n';
            }
        }

        if (should_continue_to_user_frame) {
            std::cerr << "probe: continue\n";
            const auto continue_response = dap_session.continueExecution({
                .thread_id = threads_response.threads.front().id,
            });

            if (!continue_response.success) {
                std::cerr << "continueExecution failed: " << continue_response.error_message << '\n';
                return result_base_code + 2;
            }

            std::cerr << "probe: waitForStoppedEvent\n";
            if (!dap_session.waitForStoppedEvent()) {
                std::cerr << "waitForStoppedEvent failed: " << dap_session.getLastError() << '\n';
                return result_base_code + 3;
            }

            return query_stopped_state(result_base_code + 4);
        }

        return 0;
    };

    const int query_result = query_stopped_state(6);
    if (query_result != 0) {
        return query_result;
    }

    const auto capabilities = dap_session.getCapabilities();
    std::cout << "initialize ok\n";
    std::cout << "supports_memory_read=" << capabilities.supports_memory_read << '\n';
    std::cout << "supports_memory_write=" << capabilities.supports_memory_write << '\n';
    std::cout << "supports_watch_expressions=" << capabilities.supports_watch_expressions << '\n';
    std::cout << "supports_disassembly=" << capabilities.supports_disassembly << '\n';
    std::cout << "supports_data_breakpoints=" << capabilities.supports_data_breakpoints << '\n';
    std::cout << (mode == "launch" ? "launch ok\n" : "attach ok\n");
    return 0;
}
