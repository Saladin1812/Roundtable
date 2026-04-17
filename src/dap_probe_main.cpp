#include <iostream>
#include <memory>

#include "dap_session.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: roundtable_dap_probe <adapter-path> <liblldb-path>\n";
        return 1;
    }

    SDapEndpointConfig endpoint_config = {
        .transport_kind = eDapTransportKind::STDIO,
        .command        = argv[1],
        .arguments      = {"--liblldb", argv[2]},
    };

    CDapDebugSession dap_session(std::make_unique<CStdioDapTransport>(), endpoint_config);

    if (!dap_session.connect()) {
        std::cerr << "connect failed: " << dap_session.getLastError() << '\n';
        return 2;
    }

    if (!dap_session.initialize()) {
        std::cerr << "initialize failed: " << dap_session.getLastError() << '\n';
        return 3;
    }

    const auto capabilities = dap_session.getCapabilities();
    std::cout << "initialize ok\n";
    std::cout << "supports_memory_read=" << capabilities.supports_memory_read << '\n';
    std::cout << "supports_memory_write=" << capabilities.supports_memory_write << '\n';
    std::cout << "supports_watch_expressions=" << capabilities.supports_watch_expressions << '\n';
    std::cout << "supports_disassembly=" << capabilities.supports_disassembly << '\n';
    std::cout << "supports_data_breakpoints=" << capabilities.supports_data_breakpoints << '\n';
    return 0;
}
