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
                                     .auth_token     = "",
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
                                                                            .start_address    = 0x1000,
                                                                            .memory_reference = "",
                                                                            .byte_count       = 16,
                                                                            .bytes_per_row    = 8,
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

TEST_CASE("CDapDebugSession builds a threads request message") {
    const std::string request_message = CDapDebugSession::buildThreadsRequestMessage(8);

    CHECK(request_message.find("\"seq\":8") != std::string::npos);
    CHECK(request_message.find("\"command\":\"threads\"") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses a threads response message") {
    const std::string         response_message = R"({"success":true,"body":{"threads":[{"id":1,"name":"main"},{"id":2,"name":"worker"}]}})";
    const SDapThreadsResponse response         = CDapDebugSession::parseThreadsResponseMessage(response_message);

    REQUIRE(response.success);
    REQUIRE(response.threads.size() == 2);
    CHECK(response.threads[0].id == 1);
    CHECK(response.threads[0].name == "main");
    CHECK(response.threads[1].id == 2);
    CHECK(response.threads[1].name == "worker");
}

TEST_CASE("CDapDebugSession builds a stackTrace request message") {
    const std::string request_message = CDapDebugSession::buildStackTraceRequestMessage(12,
                                                                                        {
                                                                                            .thread_id   = 42,
                                                                                            .start_frame = 0,
                                                                                            .levels      = 10,
                                                                                        });

    CHECK(request_message.find("\"seq\":12") != std::string::npos);
    CHECK(request_message.find("\"command\":\"stackTrace\"") != std::string::npos);
    CHECK(request_message.find("\"threadId\":42") != std::string::npos);
    CHECK(request_message.find("\"startFrame\":0") != std::string::npos);
    CHECK(request_message.find("\"levels\":10") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses a stackTrace response message") {
    const std::string response_message =
        R"({"success":true,"body":{"stackFrames":[{"id":1001,"name":"main","line":12,"column":3,"source":{"path":"/tmp/sample.cpp"}},{"id":1002,"name":"helper","line":34,"column":1,"source":{"path":"/tmp/helper.cpp"}}]}})";
    const SDapStackTraceResponse response = CDapDebugSession::parseStackTraceResponseMessage(response_message);

    REQUIRE(response.success);
    REQUIRE(response.stack_frames.size() == 2);
    CHECK(response.stack_frames[0].id == 1001);
    CHECK(response.stack_frames[0].name == "main");
    CHECK(response.stack_frames[0].source_path == "/tmp/sample.cpp");
    CHECK(response.stack_frames[0].line == 12);
    CHECK(response.stack_frames[0].column == 3);
    CHECK(response.stack_frames[1].id == 1002);
    CHECK(response.stack_frames[1].name == "helper");
    CHECK(response.stack_frames[1].source_path == "/tmp/helper.cpp");
}

TEST_CASE("CDapDebugSession parses stackTrace frames when id is not the first field") {
    const std::string response_message =
        R"({"success":true,"body":{"stackFrames":[{"column":0,"id":1001,"instructionPointerReference":"0x7FFFF7FE3D40","line":3,"moduleId":"7FFFF7FC4000","name":"_start","source":{"name":"@_start","origin":"disassembly","sourceReference":1000}}]}})";

    const SDapStackTraceResponse response = CDapDebugSession::parseStackTraceResponseMessage(response_message);

    REQUIRE(response.success);
    REQUIRE(response.stack_frames.size() == 1);
    CHECK(response.stack_frames[0].id == 1001);
    CHECK(response.stack_frames[0].name == "_start");
    CHECK(response.stack_frames[0].line == 3);
    CHECK(response.stack_frames[0].column == 0);
    CHECK(response.stack_frames[0].source_path.empty());
}

TEST_CASE("CDapDebugSession builds a scopes request message") {
    const std::string request_message = CDapDebugSession::buildScopesRequestMessage(13,
                                                                                    {
                                                                                        .frame_id = 1001,
                                                                                    });

    CHECK(request_message.find("\"seq\":13") != std::string::npos);
    CHECK(request_message.find("\"command\":\"scopes\"") != std::string::npos);
    CHECK(request_message.find("\"frameId\":1001") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses a scopes response message") {
    const std::string        response_message = R"({"success":true,"body":{"scopes":[{"name":"Arguments","variablesReference":17},{"name":"Locals","variablesReference":23}]}})";

    const SDapScopesResponse response = CDapDebugSession::parseScopesResponseMessage(response_message);

    REQUIRE(response.success);
    REQUIRE(response.scopes.size() == 2);
    CHECK(response.scopes[0].name == "Arguments");
    CHECK(response.scopes[0].variables_reference == 17);
    CHECK(response.scopes[1].name == "Locals");
    CHECK(response.scopes[1].variables_reference == 23);
}

TEST_CASE("CDapDebugSession builds a variables request message") {
    const std::string request_message = CDapDebugSession::buildVariablesRequestMessage(14,
                                                                                       {
                                                                                           .variables_reference = 23,
                                                                                       });

    CHECK(request_message.find("\"seq\":14") != std::string::npos);
    CHECK(request_message.find("\"command\":\"variables\"") != std::string::npos);
    CHECK(request_message.find("\"variablesReference\":23") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses a variables response message") {
    const std::string response_message =
        R"({"success":true,"body":{"variables":[{"name":"value","value":"42","type":"int","variablesReference":0},{"name":"ptr","value":"0x1000","type":"char *","memoryReference":"0x1000","variablesReference":7}]}})";

    const SDapVariablesResponse response = CDapDebugSession::parseVariablesResponseMessage(response_message);

    REQUIRE(response.success);
    REQUIRE(response.variables.size() == 2);
    CHECK(response.variables[0].name == "value");
    CHECK(response.variables[0].value == "42");
    CHECK(response.variables[0].type == "int");
    CHECK(response.variables[1].name == "ptr");
    CHECK(response.variables[1].value == "0x1000");
    CHECK(response.variables[1].type == "char *");
    CHECK(response.variables[1].memory_reference == "0x1000");
    CHECK(response.variables[1].variables_reference == 7);
}

TEST_CASE("CDapDebugSession builds a continue request message") {
    const std::string request_message = CDapDebugSession::buildContinueRequestMessage(15,
                                                                                      {
                                                                                          .thread_id = 13,
                                                                                      });

    CHECK(request_message.find("\"seq\":15") != std::string::npos);
    CHECK(request_message.find("\"command\":\"continue\"") != std::string::npos);
    CHECK(request_message.find("\"threadId\":13") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses a continue response message") {
    const SDapContinueResponse response = CDapDebugSession::parseContinueResponseMessage(R"({"success":true,"body":{"allThreadsContinued":true}})");

    REQUIRE(response.success);
    CHECK(response.error_message.empty());
}

TEST_CASE("CDapDebugSession builds a terminate request message") {
    const std::string request_message = CDapDebugSession::buildTerminateRequestMessage(16);

    CHECK(request_message.find("\"seq\":16") != std::string::npos);
    CHECK(request_message.find("\"command\":\"terminate\"") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses a terminate response message") {
    const SDapTerminateResponse response = CDapDebugSession::parseTerminateResponseMessage(R"({"type":"response","command":"terminate","success":true})");

    REQUIRE(response.success);
    CHECK(response.error_message.empty());
}

TEST_CASE("CDapDebugSession builds a disconnect request message") {
    const std::string request_message = CDapDebugSession::buildDisconnectRequestMessage(17,
                                                                                        {
                                                                                            .terminate_debuggee = true,
                                                                                        });

    CHECK(request_message.find("\"seq\":17") != std::string::npos);
    CHECK(request_message.find("\"command\":\"disconnect\"") != std::string::npos);
    CHECK(request_message.find("\"terminateDebuggee\":true") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses a disconnect response message") {
    const SDapDisconnectResponse response = CDapDebugSession::parseDisconnectResponseMessage(R"({"type":"response","command":"disconnect","success":true})");

    REQUIRE(response.success);
    CHECK(response.error_message.empty());
}

TEST_CASE("CDapDebugSession builds a step over request message") {
    const std::string request_message = CDapDebugSession::buildStepOverRequestMessage(16,
                                                                                      {
                                                                                          .thread_id = 13,
                                                                                      });

    CHECK(request_message.find("\"seq\":16") != std::string::npos);
    CHECK(request_message.find("\"command\":\"next\"") != std::string::npos);
    CHECK(request_message.find("\"threadId\":13") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses a step over response message") {
    const SDapStepOverResponse response = CDapDebugSession::parseStepOverResponseMessage(R"({"success":true})");

    REQUIRE(response.success);
    CHECK(response.error_message.empty());
}

TEST_CASE("CDapDebugSession builds a step into request message") {
    const std::string request_message = CDapDebugSession::buildStepIntoRequestMessage(17,
                                                                                      {
                                                                                          .thread_id = 13,
                                                                                      });

    CHECK(request_message.find("\"seq\":17") != std::string::npos);
    CHECK(request_message.find("\"command\":\"stepIn\"") != std::string::npos);
    CHECK(request_message.find("\"threadId\":13") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses a step into response message") {
    const SDapStepIntoResponse response = CDapDebugSession::parseStepIntoResponseMessage(R"({"success":true})");

    REQUIRE(response.success);
    CHECK(response.error_message.empty());
}

TEST_CASE("CDapDebugSession builds a step out request message") {
    const std::string request_message = CDapDebugSession::buildStepOutRequestMessage(18,
                                                                                     {
                                                                                         .thread_id = 13,
                                                                                     });

    CHECK(request_message.find("\"seq\":18") != std::string::npos);
    CHECK(request_message.find("\"command\":\"stepOut\"") != std::string::npos);
    CHECK(request_message.find("\"threadId\":13") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses a step out response message") {
    const SDapStepOutResponse response = CDapDebugSession::parseStepOutResponseMessage(R"({"success":true})");

    REQUIRE(response.success);
    CHECK(response.error_message.empty());
}

TEST_CASE("CDapDebugSession builds a pause request message") {
    const std::string request_message = CDapDebugSession::buildPauseRequestMessage(19,
                                                                                   {
                                                                                       .thread_id = 13,
                                                                                   });

    CHECK(request_message.find("\"seq\":19") != std::string::npos);
    CHECK(request_message.find("\"command\":\"pause\"") != std::string::npos);
    CHECK(request_message.find("\"threadId\":13") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses a pause response message") {
    const SDapPauseResponse response = CDapDebugSession::parsePauseResponseMessage(R"({"success":true})");

    REQUIRE(response.success);
    CHECK(response.error_message.empty());
}

TEST_CASE("CDapDebugSession builds an evaluate request message") {
    const std::string request_message = CDapDebugSession::buildEvaluateRequestMessage(16,
                                                                                      {
                                                                                          .expression = "sample_value",
                                                                                          .frame_id   = 1014,
                                                                                          .context    = "watch",
                                                                                      });

    CHECK(request_message.find("\"seq\":16") != std::string::npos);
    CHECK(request_message.find("\"command\":\"evaluate\"") != std::string::npos);
    CHECK(request_message.find("\"expression\":\"sample_value\"") != std::string::npos);
    CHECK(request_message.find("\"frameId\":1014") != std::string::npos);
    CHECK(request_message.find("\"context\":\"watch\"") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses an evaluate response message") {
    const SDapEvaluateResponse response = CDapDebugSession::parseEvaluateResponseMessage(R"({"success":true,"body":{"result":"42","type":"const int","variablesReference":0}})");

    REQUIRE(response.success);
    CHECK(response.result == "42");
    CHECK(response.type == "const int");
    CHECK(response.error_message.empty());
}

TEST_CASE("CDapDebugSession parses escaped characters in evaluate response strings") {
    const SDapEvaluateResponse response = CDapDebugSession::parseEvaluateResponseMessage(
        R"({"success":true,"body":{"result":"{_M_elems:\"Hello!\\0A\"}","type":"volatile std::array<unsigned char, 8>","variablesReference":1019}})");

    REQUIRE(response.success);
    CHECK(response.result == "{_M_elems:\"Hello!\\0A\"}");
    CHECK(response.type == "volatile std::array<unsigned char, 8>");
}

TEST_CASE("CDapDebugSession parses a failed evaluate response message") {
    const SDapEvaluateResponse response =
        CDapDebugSession::parseEvaluateResponseMessage(R"({"success":false,"command":"","message":"error: use of undeclared identifier 'sample_value'"})");

    CHECK_FALSE(response.success);
    CHECK(response.error_message == "error: use of undeclared identifier 'sample_value'");
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

TEST_CASE("CDapDebugSession escapes launch request strings") {
    const std::string request_message = CDapDebugSession::buildLaunchRequestMessage(9,
                                                                                    {
                                                                                        .program           = R"(/tmp/program"quoted")",
                                                                                        .arguments         = {R"(arg"1)", R"(path\value)"},
                                                                                        .working_directory = R"(/tmp/work"dir")",
                                                                                        .stop_on_entry     = true,
                                                                                    });

    CHECK(request_message.find(R"("program":"/tmp/program\"quoted\"")") != std::string::npos);
    CHECK(request_message.find(R"("args":["arg\"1","path\\value"])") != std::string::npos);
    CHECK(request_message.find(R"("cwd":"/tmp/work\"dir\"")") != std::string::npos);
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

TEST_CASE("CDapDebugSession builds a setBreakpoints request message") {
    const std::string request_message = CDapDebugSession::buildSetBreakpointsRequestMessage(12,
                                                                                            {
                                                                                                .source_path = "/tmp/program.cpp",
                                                                                                .breakpoints = {{.line = 42}, {.line = 51}},
                                                                                            });

    CHECK(request_message.find("\"seq\":12") != std::string::npos);
    CHECK(request_message.find("\"command\":\"setBreakpoints\"") != std::string::npos);
    CHECK(request_message.find("\"path\":\"/tmp/program.cpp\"") != std::string::npos);
    CHECK(request_message.find("\"breakpoints\":[{\"line\":42},{\"line\":51}]") != std::string::npos);
    CHECK(request_message.find("\"sourceModified\":false") != std::string::npos);
}

TEST_CASE("CDapDebugSession parses a setBreakpoints response message") {
    const SDapSetBreakpointsResponse response = CDapDebugSession::parseSetBreakpointsResponseMessage(
        R"({"type":"response","command":"setBreakpoints","success":true,"body":{"breakpoints":[{"verified":true,"line":42},{"verified":false,"line":51,"message":"No source found"}]}})");

    CHECK(response.success);
    CHECK(response.breakpoint_count == 2);
    REQUIRE(response.breakpoints.size() == 2);
    CHECK(response.breakpoints[0].verified);
    CHECK(response.breakpoints[0].line == 42);
    CHECK_FALSE(response.breakpoints[1].verified);
    CHECK(response.breakpoints[1].line == 51);
    CHECK(response.breakpoints[1].message == "No source found");
    CHECK(response.error_message.empty());
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

TEST_CASE("CDapDebugSession sets breakpoints from a setBreakpoints response") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"event","event":"output"})",
        R"({"type":"response","command":"setBreakpoints","success":true,"body":{"breakpoints":[{"verified":true,"line":42}]}})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    const auto response = dap_session.setBreakpoints({
        .source_path = "/tmp/program.cpp",
        .breakpoints = {{.line = 42}},
    });

    CHECK(response.success);
    CHECK(response.breakpoint_count == 1);
    REQUIRE(response.breakpoints.size() == 1);
    CHECK(response.breakpoints[0].verified);
    CHECK(response.breakpoints[0].line == 42);
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

TEST_CASE("CDapDebugSession stops waiting when the session terminates") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"event","event":"terminated"})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    CHECK_FALSE(dap_session.waitForStoppedEvent());
    CHECK(dap_session.getLastError() == "DAP session ended before a stopped event");
}

TEST_CASE("CDapDebugSession waits for a terminated event") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"response","command":"terminate","success":true})",
        R"({"type":"event","event":"terminated"})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    REQUIRE(dap_session.waitForTerminatedEvent());
}

TEST_CASE("CDapDebugSession treats a successful disconnect response as session end") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"response","command":"disconnect","success":true})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    REQUIRE(dap_session.waitForTerminatedEvent());
}

TEST_CASE("CDapDebugSession sends configurationDone without waiting for later responses") {
    auto             transport = std::make_unique<CStubDapTransport>(true);

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    REQUIRE(dap_session.sendConfigurationDoneRequest());
}

TEST_CASE("CDapDebugSession sends terminate without waiting for a response") {
    auto             transport          = std::make_unique<CStubDapTransport>(true);
    auto*            transport_observer = transport.get();

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    REQUIRE(dap_session.sendTerminateRequest());
    CHECK(transport_observer->getLastSentMessage().find("\"command\":\"terminate\"") != std::string::npos);
}

TEST_CASE("CDapDebugSession sends disconnect without waiting for a response") {
    auto             transport          = std::make_unique<CStubDapTransport>(true);
    auto*            transport_observer = transport.get();

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    REQUIRE(dap_session.sendDisconnectRequest({.terminate_debuggee = true}));
    CHECK(transport_observer->getLastSentMessage().find("\"command\":\"disconnect\"") != std::string::npos);
    CHECK(transport_observer->getLastSentMessage().find("\"terminateDebuggee\":true") != std::string::npos);
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

TEST_CASE("CDapDebugSession returns threads from a threads response") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"event","event":"output","body":{"category":"console","output":"hello"}})",
        R"({"type":"response","command":"threads","success":true,"body":{"threads":[{"id":1,"name":"main"}]}})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    const auto threads_response = dap_session.getThreads();

    REQUIRE(threads_response.success);
    REQUIRE(threads_response.threads.size() == 1);
    CHECK(threads_response.threads[0].id == 1);
    CHECK(threads_response.threads[0].name == "main");
}

TEST_CASE("CDapDebugSession returns stack frames from a stackTrace response") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"event","event":"output","body":{"category":"console","output":"hello"}})",
        R"({"type":"response","command":"stackTrace","success":true,"body":{"stackFrames":[{"id":1001,"name":"main","line":12,"column":3,"source":{"path":"/tmp/sample.cpp"}}]}})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    const auto stack_trace_response = dap_session.getStackTrace({
        .thread_id   = 1,
        .start_frame = 0,
        .levels      = 20,
    });

    REQUIRE(stack_trace_response.success);
    REQUIRE(stack_trace_response.stack_frames.size() == 1);
    CHECK(stack_trace_response.stack_frames[0].id == 1001);
    CHECK(stack_trace_response.stack_frames[0].name == "main");
    CHECK(stack_trace_response.stack_frames[0].source_path == "/tmp/sample.cpp");
}

TEST_CASE("CDapDebugSession returns scopes from a scopes response") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"event","event":"output","body":{"category":"console","output":"hello"}})",
        R"({"type":"response","command":"scopes","success":true,"body":{"scopes":[{"name":"Locals","variablesReference":23}]}})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    const auto scopes_response = dap_session.getScopes({
        .frame_id = 1001,
    });

    REQUIRE(scopes_response.success);
    REQUIRE(scopes_response.scopes.size() == 1);
    CHECK(scopes_response.scopes[0].name == "Locals");
    CHECK(scopes_response.scopes[0].variables_reference == 23);
}

TEST_CASE("CDapDebugSession returns variables from a variables response") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"event","event":"output","body":{"category":"console","output":"hello"}})",
        R"({"type":"response","command":"variables","success":true,"body":{"variables":[{"name":"value","value":"42","type":"int","variablesReference":0}]}})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    const auto variables_response = dap_session.getVariables({
        .variables_reference = 23,
    });

    REQUIRE(variables_response.success);
    REQUIRE(variables_response.variables.size() == 1);
    CHECK(variables_response.variables[0].name == "value");
    CHECK(variables_response.variables[0].value == "42");
    CHECK(variables_response.variables[0].type == "int");
}

TEST_CASE("CDapDebugSession continues execution from a continue response") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"event","event":"continued","body":{"threadId":13}})",
        R"({"type":"response","command":"continue","success":true,"body":{"allThreadsContinued":true}})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    const auto continue_response = dap_session.continueExecution({
        .thread_id = 13,
    });

    REQUIRE(continue_response.success);
    CHECK(continue_response.error_message.empty());
}

TEST_CASE("CDapDebugSession preserves stopped events received before a control response") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"event","event":"stopped","body":{"threadId":13}})",
        R"({"type":"response","command":"continue","success":true,"body":{"allThreadsContinued":true}})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    const auto continue_response = dap_session.continueExecution({
        .thread_id = 13,
    });

    REQUIRE(continue_response.success);
    REQUIRE(dap_session.waitForStoppedEvent());
}

TEST_CASE("CDapDebugSession steps over from a next response") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"event","event":"continued","body":{"threadId":13}})",
        R"({"type":"response","command":"next","success":true})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    const auto step_response = dap_session.stepOver({
        .thread_id = 13,
    });

    REQUIRE(step_response.success);
    CHECK(step_response.error_message.empty());
}

TEST_CASE("CDapDebugSession steps into from a stepIn response") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"event","event":"continued","body":{"threadId":13}})",
        R"({"type":"response","command":"stepIn","success":true})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    const auto step_response = dap_session.stepInto({
        .thread_id = 13,
    });

    REQUIRE(step_response.success);
    CHECK(step_response.error_message.empty());
}

TEST_CASE("CDapDebugSession steps out from a stepOut response") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"event","event":"continued","body":{"threadId":13}})",
        R"({"type":"response","command":"stepOut","success":true})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    const auto step_response = dap_session.stepOut({
        .thread_id = 13,
    });

    REQUIRE(step_response.success);
    CHECK(step_response.error_message.empty());
}

TEST_CASE("CDapDebugSession sends a pause request without reading a response") {
    auto               transport     = std::make_unique<CStubDapTransport>(true);
    CStubDapTransport* transport_ptr = transport.get();
    CDapDebugSession   dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    REQUIRE(dap_session.sendPauseRequest({
        .thread_id = 13,
    }));

    const std::string request_message = transport_ptr->getLastSentMessage();
    CHECK(request_message.find("\"command\":\"pause\"") != std::string::npos);
    CHECK(request_message.find("\"threadId\":13") != std::string::npos);
}

TEST_CASE("CDapDebugSession evaluates an expression from an evaluate response") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"event","event":"output","body":{"category":"console","output":"hello"}})",
        R"({"type":"response","command":"evaluate","success":true,"body":{"result":"42","type":"const int","variablesReference":0}})",
    });

    CDapDebugSession dap_session(std::move(transport), {});
    dap_session.setAdapterCapabilities({
        .supports_read_memory      = false,
        .supports_write_memory     = false,
        .supports_evaluate         = true,
        .supports_disassemble      = false,
        .supports_data_breakpoints = false,
    });

    REQUIRE(dap_session.connect());
    const auto evaluate_response = dap_session.evaluate({
        .expression = "sample_value",
        .frame_id   = 1014,
        .context    = "watch",
    });

    REQUIRE(evaluate_response.success);
    CHECK(evaluate_response.result == "42");
    CHECK(evaluate_response.type == "const int");
}

TEST_CASE("CDapDebugSession resolves locals through stackTrace scopes and variables") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"response","command":"stackTrace","success":true,"body":{"stackFrames":[{"id":1001,"name":"main","line":12,"column":3,"source":{"path":"/tmp/sample.cpp"}}]}})",
        R"({"type":"response","command":"scopes","success":true,"body":{"scopes":[{"name":"Registers","variablesReference":11},{"name":"Locals","variablesReference":23}]}})",
        R"({"type":"response","command":"variables","success":true,"body":{"variables":[{"name":"value","value":"42","type":"int","variablesReference":0},{"name":"ptr","value":"0x1000","type":"char *","memoryReference":"0x1000","variablesReference":0}]}})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    const auto locals = dap_session.getLocals({
        .thread_id   = 1,
        .frame_index = 0,
    });

    REQUIRE(locals.size() == 2);
    CHECK(locals[0].name == "value");
    CHECK(locals[0].value == "42");
    CHECK(locals[0].type == "int");
    CHECK(locals[0].memory_reference.empty());
    CHECK(locals[0].variables_reference == 0);
    CHECK(locals[1].name == "ptr");
    CHECK(locals[1].value == "0x1000");
    CHECK(locals[1].type == "char *");
    CHECK(locals[1].memory_reference == "0x1000");
    CHECK(locals[1].variables_reference == 0);
}

TEST_CASE("CDapDebugSession resolves local memoryReference through child variables") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"response","command":"stackTrace","success":true,"body":{"stackFrames":[{"id":1001,"name":"main","line":12,"column":3,"source":{"path":"/tmp/sample.cpp"}}]}})",
        R"({"type":"response","command":"scopes","success":true,"body":{"scopes":[{"name":"Locals","variablesReference":23}]}})",
        R"({"type":"response","command":"variables","success":true,"body":{"variables":[{"name":"sample_bytes","value":"{...}","type":"volatile std::array<unsigned char, 8>","variablesReference":1019}]}})",
        R"({"type":"response","command":"variables","success":true,"body":{"variables":[{"name":"_M_elems","value":"{...}","type":"unsigned char [8]","variablesReference":1020}]}})",
        R"({"type":"response","command":"variables","success":true,"body":{"variables":[{"name":"[0]","value":"'H'","type":"unsigned char","memoryReference":"0x7000","variablesReference":0}]}})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    const auto locals = dap_session.getLocals({
        .thread_id   = 1,
        .frame_index = 0,
    });

    REQUIRE(locals.size() == 1);
    CHECK(locals[0].name == "sample_bytes");
    CHECK(locals[0].type == "volatile std::array<unsigned char, 8>");
    CHECK(locals[0].memory_reference == "0x7000");
    CHECK(locals[0].variables_reference == 1019);
}

TEST_CASE("CDapDebugSession evaluates watch expressions through stackTrace and evaluate") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"response","command":"stackTrace","success":true,"body":{"stackFrames":[{"id":1014,"name":"main","line":13,"column":5,"source":{"path":"/tmp/sample.cpp"}}]}})",
        R"({"type":"response","command":"evaluate","success":true,"body":{"result":"42","type":"const int","variablesReference":0}})",
        R"({"type":"response","command":"evaluate","success":true,"body":{"result":"{_M_elems:\"Hello!\\0A\"}","type":"volatile std::array<unsigned char, 8>","variablesReference":1019}})",
    });

    CDapDebugSession dap_session(std::move(transport), {});
    dap_session.setAdapterCapabilities({
        .supports_read_memory      = false,
        .supports_write_memory     = false,
        .supports_evaluate         = true,
        .supports_disassemble      = false,
        .supports_data_breakpoints = false,
    });

    REQUIRE(dap_session.connect());
    const auto watch_results = dap_session.evaluateWatches(
        {
            .thread_id   = 19,
            .frame_index = 0,
        },
        {
            {.expression = "sample_value"},
            {.expression = "sample_bytes"},
        });

    REQUIRE(watch_results.size() == 2);
    CHECK(watch_results[0].expression == "sample_value");
    CHECK(watch_results[0].value == "42");
    CHECK(watch_results[0].type == "const int");
    CHECK(watch_results[0].error_message.empty());
    CHECK(watch_results[1].expression == "sample_bytes");
    CHECK(watch_results[1].type == "volatile std::array<unsigned char, 8>");
    CHECK(watch_results[1].error_message.empty());
}

TEST_CASE("CDapDebugSession resolves locals from the selected nonzero frame index") {
    auto transport = std::make_unique<CStubDapTransport>(true);
    transport->setReadMessages({
        R"({"type":"response","command":"stackTrace","success":true,"body":{"stackFrames":[{"id":1001,"name":"helper","line":9,"column":1,"source":{"path":"/tmp/sample.cpp"}},{"id":1008,"name":"main","line":12,"column":3,"source":{"path":"/tmp/sample.cpp"}}]}})",
        R"({"type":"response","command":"scopes","success":true,"body":{"scopes":[{"name":"Locals","variablesReference":23}]}})",
        R"({"type":"response","command":"variables","success":true,"body":{"variables":[{"name":"sample_value","value":"42","type":"int","variablesReference":0}]}})",
    });

    CDapDebugSession dap_session(std::move(transport), {});

    REQUIRE(dap_session.connect());
    const auto locals = dap_session.getLocals({
        .thread_id   = 1,
        .frame_index = 1,
    });

    REQUIRE(locals.size() == 1);
    CHECK(locals[0].name == "sample_value");
    CHECK(locals[0].value == "42");
    CHECK(locals[0].type == "int");
}
