#include <catch2/catch_test_macros.hpp>

#include "dap_session.hpp"

class CStubDapTransport : public IDapTransport {
  public:
    explicit CStubDapTransport(bool should_connect, std::string failure_message = "") : should_connect_(should_connect), failure_message_(std::move(failure_message)) {}

    bool connect(const SDapEndpointConfig& endpoint_config, std::string& error_message) override {
        last_endpoint_config_ = endpoint_config;

        if (!should_connect_) {
            error_message = failure_message_;
            connected_    = false;
            return false;
        }

        error_message.clear();
        connected_ = true;
        return true;
    }

    bool sendMessage(const std::string& message, std::string& error_message) override {
        static_cast<void>(message);
        error_message = "Not implemented in stub";
        return false;
    }

    bool readMessage(std::string& message, std::string& error_message) override {
        message.clear();
        error_message = "Not implemented in stub";
        return false;
    }

    bool isConnected() const override {
        return connected_;
    }

    SDapEndpointConfig getLastEndpointConfig() const {
        return last_endpoint_config_;
    }

  private:
    bool               should_connect_ = false;
    bool               connected_      = false;
    std::string        failure_message_;
    SDapEndpointConfig last_endpoint_config_;
};

TEST_CASE("CDapDebugSession maps adapter capabilities to app capabilities") {
    const SDapAdapterCapabilities adapter_capabilities = {
        .supports_read_memory      = true,
        .supports_write_memory     = false,
        .supports_evaluate         = true,
        .supports_disassemble      = true,
        .supports_data_breakpoints = false,
    };

    const SDebugCapabilities capabilities = CDapDebugSession::mapCapabilities(adapter_capabilities);

    CHECK(capabilities.supports_memory_read);
    CHECK_FALSE(capabilities.supports_memory_write);
    CHECK(capabilities.supports_watch_expressions);
    CHECK(capabilities.supports_disassembly);
    CHECK_FALSE(capabilities.supports_data_breakpoints);
}

TEST_CASE("CDapDebugSession stores adapter capabilities for later queries") {
    auto             transport = std::make_unique<CStubDapTransport>(true);

    CDapDebugSession dap_session(std::move(transport), {});
    dap_session.setAdapterCapabilities({
        .supports_read_memory      = true,
        .supports_write_memory     = true,
        .supports_evaluate         = false,
        .supports_disassemble      = false,
        .supports_data_breakpoints = true,
    });

    const SDebugCapabilities capabilities = dap_session.getCapabilities();

    CHECK(capabilities.supports_memory_read);
    CHECK(capabilities.supports_memory_write);
    CHECK_FALSE(capabilities.supports_watch_expressions);
    CHECK_FALSE(capabilities.supports_disassembly);
    CHECK(capabilities.supports_data_breakpoints);
}

TEST_CASE("CDapDebugSession reports transport connection failures") {
    auto             transport = std::make_unique<CStubDapTransport>(false, "Failed to connect to adapter");

    CDapDebugSession dap_session(std::move(transport),
                                 {
                                     .transport_kind = eDapTransportKind::TCP,
                                     .command        = "",
                                     .arguments      = {},
                                     .host           = "127.0.0.1",
                                     .port           = 4711,
                                 });

    CHECK_FALSE(dap_session.connect());
    CHECK_FALSE(dap_session.isConnected());
    CHECK(dap_session.getLastError() == "Failed to connect to adapter");
}

TEST_CASE("CDapDebugSession returns a clean disconnected error for memory reads") {
    auto                    transport = std::make_unique<CStubDapTransport>(false);

    CDapDebugSession        dap_session(std::move(transport), {});

    const SMemoryReadResult memory_read_result = dap_session.readMemory({},
                                                                        {
                                                                            .start_address = 0x1000,
                                                                            .byte_count    = 16,
                                                                            .bytes_per_row = 8,
                                                                        });

    CHECK(memory_read_result.start_address == 0x1000);
    CHECK(memory_read_result.memory_bytes.empty());
    CHECK(memory_read_result.error_message == "DAP session is not connected");
}

TEST_CASE("CDapDebugSession returns watch errors while disconnected") {
    auto                            transport = std::make_unique<CStubDapTransport>(false);

    CDapDebugSession                dap_session(std::move(transport), {});

    const std::vector<SWatchResult> watch_results = dap_session.evaluateWatches({},
                                                                                {
                                                                                    {.expression = "a"},
                                                                                    {.expression = "ptr"},
                                                                                });

    REQUIRE(watch_results.size() == 2);
    CHECK(watch_results[0].error_message == "DAP session is not connected");
    CHECK(watch_results[1].error_message == "DAP session is not connected");
}
