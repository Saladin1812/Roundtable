#include <iostream>
#include <memory>
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
    if (!dap_session.configurationDone()) {
        std::cerr << "configurationDone failed: " << dap_session.getLastError() << '\n';
        return 5;
    }

    std::cerr << "probe: waitForStoppedEvent\n";
    if (!dap_session.waitForStoppedEvent()) {
        std::cerr << "waitForStoppedEvent failed: " << dap_session.getLastError() << '\n';
        return 6;
    }

    const auto capabilities = dap_session.getCapabilities();
    std::cout << "initialize ok\n";
    std::cout << "supports_memory_read=" << capabilities.supports_memory_read << '\n';
    std::cout << "supports_memory_write=" << capabilities.supports_memory_write << '\n';
    std::cout << "supports_watch_expressions=" << capabilities.supports_watch_expressions << '\n';
    std::cout << "supports_disassembly=" << capabilities.supports_disassembly << '\n';
    std::cout << "supports_data_breakpoints=" << capabilities.supports_data_breakpoints << '\n';
    std::cout << (mode == "launch" ? "launch ok\n" : "attach ok\n");
    std::cout << "stopped ok\n";
    return 0;
}
