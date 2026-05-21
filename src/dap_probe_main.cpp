#include <iostream>
#include <memory>
#include <filesystem>
#include <functional>
#include <string_view>

#include "dap_session.hpp"
#include "memory_selection.hpp"
#include "memory_view.hpp"

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

        const auto sample_source_path  = std::filesystem::absolute("samples/dap_sample_target.cpp").string();
        const auto breakpoint_response = dap_session.setBreakpoints({
            .source_path = sample_source_path,
            .breakpoints =
                {
                    {.line = 15},
                },
        });
        if (!breakpoint_response.success || breakpoint_response.breakpoints.empty() || !breakpoint_response.breakpoints.front().verified) {
            std::cerr << "setBreakpoints failed: " << breakpoint_response.error_message << '\n';
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
            std::cout << "local name=" << local.name << " value=" << local.value << " type=" << local.type << " memory_reference=" << local.memory_reference
                      << " variables_reference=" << local.variables_reference << '\n';
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

            std::cerr << "probe: readMemory\n";
            const auto address_watch_results = dap_session.evaluateWatches(
                {
                    .thread_id   = threads_response.threads.front().id,
                    .frame_index = selected_frame_index,
                },
                {
                    {.expression = "sample_bytes.data()"},
                    {.expression = "&sample_bytes._M_elems[0]"},
                    {.expression = "&sample_bytes[0]"},
                    {.expression = "&sample_bytes"},
                    {.expression = "&sample_value"},
                });
            for (const auto& address_watch_result : address_watch_results) {
                std::cout << "address watch expression=" << address_watch_result.expression << " value=" << address_watch_result.value << " type=" << address_watch_result.type
                          << " memory_reference=" << address_watch_result.memory_reference << " error=" << address_watch_result.error_message << '\n';
            }
            std::size_t selected_local_index = 0;
            for (std::size_t local_index = 0; local_index < locals.size(); ++local_index) {
                if (locals[local_index].name == "sample_value") {
                    selected_local_index = local_index;
                    break;
                }
            }

            const auto memory_read_request   = buildMemoryReadRequest(dap_session,
                                                                      {
                                                                          .thread_id   = threads_response.threads.front().id,
                                                                          .frame_index = selected_frame_index,
                                                                    },
                                                                      locals, selected_local_index, 0x1000);
            const auto synthetic_memory_rows = buildSyntheticMemoryRows(locals, selected_local_index, memory_read_request.bytes_per_row);
            const auto memory_read_result    = dap_session.readMemory(
                {
                       .thread_id   = threads_response.threads.front().id,
                       .frame_index = selected_frame_index,
                },
                memory_read_request);

            std::cout << "memory address=0x" << std::hex << std::uppercase << memory_read_request.start_address << std::dec << '\n';
            if (synthetic_memory_rows.has_value()) {
                for (const auto& row : synthetic_memory_rows.value()) {
                    std::cout << "synthetic memory row=" << row << '\n';
                }
            }
            std::cout << "memory error=" << memory_read_result.error_message << '\n';
            const auto memory_rows = generateMemoryViewRows(memory_read_result);
            std::cout << "memory row count=" << memory_rows.size() << '\n';
            for (const auto& row : memory_rows) {
                std::cout << "memory row=" << row << '\n';
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

    const auto smoke_control_request = [&](std::string_view name, auto send_request, int result_base_code) -> int {
        const auto threads_response = dap_session.getThreads();
        if (!threads_response.success || threads_response.threads.empty()) {
            std::cerr << name << " failed: no active thread\n";
            return result_base_code;
        }

        std::cerr << "probe: " << name << '\n';
        if (!send_request(threads_response.threads.front().id)) {
            std::cerr << name << " request failed: " << dap_session.getLastError() << '\n';
            return result_base_code + 1;
        }

        if (!dap_session.waitForStoppedEvent()) {
            std::cerr << name << " waitForStoppedEvent failed: " << dap_session.getLastError() << '\n';
            return result_base_code + 2;
        }

        std::cout << name << " ok\n";
        return query_stopped_state(result_base_code + 3);
    };

    if (mode == "launch") {
        int step_result = smoke_control_request("stepInto", [&](int thread_id) { return dap_session.sendStepIntoRequest({.thread_id = thread_id}); }, 20);
        if (step_result != 0) {
            return step_result;
        }

        step_result = smoke_control_request("stepOut", [&](int thread_id) { return dap_session.sendStepOutRequest({.thread_id = thread_id}); }, 30);
        if (step_result != 0) {
            return step_result;
        }

        step_result = smoke_control_request("stepOver", [&](int thread_id) { return dap_session.sendStepOverRequest({.thread_id = thread_id}); }, 40);
        if (step_result != 0) {
            return step_result;
        }
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
