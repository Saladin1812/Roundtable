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
        last_sent_message_ = message;

        if (!send_succeeds_) {
            error_message = send_failure_message_;
            return false;
        }

        error_message.clear();
        return true;
    }

    bool readMessage(std::string& message, std::string& error_message) override {
        if (!read_succeeds_) {
            message.clear();
            error_message = read_failure_message_;
            return false;
        }

        if (read_messages_.empty()) {
            message.clear();
            error_message = "No stub DAP messages available";
            return false;
        }

        message = read_messages_.front();
        read_messages_.erase(read_messages_.begin());
        error_message.clear();
        return true;
    }

    bool isConnected() const override {
        return connected_;
    }

    SDapEndpointConfig getLastEndpointConfig() const {
        return last_endpoint_config_;
    }

    std::string getLastSentMessage() const {
        return last_sent_message_;
    }

    void setNextReadMessage(std::string next_read_message) {
        read_messages_.clear();
        read_messages_.push_back(std::move(next_read_message));
    }

    void setReadMessages(std::vector<std::string> read_messages) {
        read_messages_ = std::move(read_messages);
    }

    void setSendFailure(std::string send_failure_message) {
        send_succeeds_        = false;
        send_failure_message_ = std::move(send_failure_message);
    }

    void setReadFailure(std::string read_failure_message) {
        read_succeeds_        = false;
        read_failure_message_ = std::move(read_failure_message);
    }

  private:
    bool                     should_connect_ = false;
    bool                     connected_      = false;
    bool                     send_succeeds_  = true;
    bool                     read_succeeds_  = true;
    std::string              failure_message_;
    std::string              send_failure_message_;
    std::string              read_failure_message_;
    std::vector<std::string> read_messages_;
    std::string              last_sent_message_;
    SDapEndpointConfig       last_endpoint_config_;
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

TEST_CASE("CDapDebugSession builds an initialize request message") {
    const std::string request_message = CDapDebugSession::buildInitializeRequestMessage({
        .client_id   = "roundtable-test",
        .client_name = "Roundtable Test",
    });

    CHECK(request_message.find("\"command\":\"initialize\"") != std::string::npos);
    CHECK(request_message.find("\"clientID\":\"roundtable-test\"") != std::string::npos);
    CHECK(request_message.find("\"clientName\":\"Roundtable Test\"") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses initialize capabilities from a response message") {
    const std::string            response_message = "{\"success\":true,\"body\":{\"supportsReadMemoryRequest\":true,\"supportsWriteMemoryRequest\":false,"
                                                    "\"supportsEvaluateForHovers\":true,\"supportsDisassembleRequest\":true,\"supportsDataBreakpoints\":false}}";

    const SDapInitializeResponse response = CDapDebugSession::parseInitializeResponseMessage(response_message);

    CHECK(response.success);
    CHECK(response.capabilities.supports_read_memory);
    CHECK_FALSE(response.capabilities.supports_write_memory);
    CHECK(response.capabilities.supports_evaluate);
    CHECK(response.capabilities.supports_disassemble);
    CHECK_FALSE(response.capabilities.supports_data_breakpoints);
}

TEST_CASE("CDapDebugSession initializes from transport messages") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setNextReadMessage("{\"success\":true,\"body\":{\"supportsReadMemoryRequest\":true,\"supportsWriteMemoryRequest\":true,"
                                  "\"supportsEvaluateForHovers\":true,\"supportsDisassembleRequest\":false,\"supportsDataBreakpoints\":true}}");

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    REQUIRE(dap_session.initialize());

    const SDebugCapabilities capabilities = dap_session.getCapabilities();
    CHECK(capabilities.supports_memory_read);
    CHECK(capabilities.supports_memory_write);
    CHECK(capabilities.supports_watch_expressions);
    CHECK_FALSE(capabilities.supports_disassembly);
    CHECK(capabilities.supports_data_breakpoints);
}

TEST_CASE("CDapDebugSession reports initialize parse failures") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setNextReadMessage("{\"success\":false}");

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    CHECK_FALSE(dap_session.initialize());
    CHECK(dap_session.getLastError() == "DAP initialize response did not report success");
}

TEST_CASE("CDapDebugSession builds a readMemory request message") {
    const std::string request_message = CDapDebugSession::buildReadMemoryRequestMessage(7,
                                                                                        {
                                                                                            .memory_reference = "0x1000",
                                                                                            .offset           = 0,
                                                                                            .count            = 16,
                                                                                        });

    CHECK(request_message.find("\"seq\":7") != std::string::npos);
    CHECK(request_message.find("\"command\":\"readMemory\"") != std::string::npos);
    CHECK(request_message.find("\"memoryReference\":\"0x1000\"") != std::string::npos);
    CHECK(request_message.find("\"count\":16") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses a readMemory response message") {
    const std::string            response_message = R"({"success":true,"body":{"address":"0x1000","data":"SGVsbG8="}})";

    const SDapReadMemoryResponse response = CDapDebugSession::parseReadMemoryResponseMessage(response_message);

    REQUIRE(response.success);
    REQUIRE(response.memory_bytes.size() == 5);
    CHECK(response.memory_bytes[0] == 0x48);
    CHECK(response.memory_bytes[1] == 0x65);
    CHECK(response.memory_bytes[2] == 0x6C);
    CHECK(response.memory_bytes[3] == 0x6C);
    CHECK(response.memory_bytes[4] == 0x6F);
}

TEST_CASE("CDapDebugSession builds a launch request message") {
    const std::string request_message = CDapDebugSession::buildLaunchRequestMessage(9,
                                                                                    {
                                                                                        .program           = "/tmp/program",
                                                                                        .arguments         = {"arg1", "arg2"},
                                                                                        .working_directory = "/tmp",
                                                                                        .stop_on_entry     = true,
                                                                                    });

    CHECK(request_message.find("\"seq\":9") != std::string::npos);
    CHECK(request_message.find("\"command\":\"launch\"") != std::string::npos);
    CHECK(request_message.find("\"program\":\"/tmp/program\"") != std::string::npos);
    CHECK(request_message.find("\"args\":[\"arg1\",\"arg2\"]") != std::string::npos);
    CHECK(request_message.find("\"cwd\":\"/tmp\"") != std::string::npos);
    CHECK(request_message.find("\"stopOnEntry\":true") != std::string::npos);
}

TEST_CASE("CDapDebugSession builds an attach request message") {
    const std::string request_message = CDapDebugSession::buildAttachRequestMessage(11,
                                                                                    {
                                                                                        .process_id    = 4242,
                                                                                        .stop_on_entry = true,
                                                                                    });

    CHECK(request_message.find("\"seq\":11") != std::string::npos);
    CHECK(request_message.find("\"command\":\"attach\"") != std::string::npos);
    CHECK(request_message.find("\"pid\":4242") != std::string::npos);
    CHECK(request_message.find("\"stopOnEntry\":true") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses generic DAP protocol messages") {
    const auto event_message = CDapDebugSession::parseProtocolMessage(R"({"type":"event","event":"stopped"})");
    CHECK(event_message.type == "event");
    CHECK(event_message.event_name == "stopped");

    const auto response_message = CDapDebugSession::parseProtocolMessage(R"({"type":"response","command":"launch","success":true})");
    CHECK(response_message.type == "response");
    CHECK(response_message.command_name == "launch");
    CHECK(response_message.success);
}

TEST_CASE("CDapDebugSession launches after receiving initialized event and launch response") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"event","event":"initialized"})",
        R"({"type":"response","command":"launch","success":true})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    REQUIRE(dap_session.launch({
        .program           = "/tmp/program",
        .arguments         = {},
        .working_directory = "/tmp",
        .stop_on_entry     = true,
    }));
}

TEST_CASE("CDapDebugSession completes configurationDone and sees a stopped event") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"response","command":"configurationDone","success":true})",
        R"({"type":"event","event":"stopped"})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    REQUIRE(dap_session.configurationDone());
    REQUIRE(dap_session.waitForStoppedEvent());
}

TEST_CASE("CDapDebugSession attaches after receiving initialized event") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"event","event":"initialized"})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    REQUIRE(dap_session.attach({
        .process_id    = 4242,
        .stop_on_entry = true,
    }));
}
