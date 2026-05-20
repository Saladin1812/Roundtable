#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "debugger_controller.hpp"

class CControllerStubDapTransport : public IDapTransport {
  public:
    bool connect(const SDapEndpointConfig&, std::string& error_message) override {
        connected_ = true;
        error_message.clear();
        return true;
    }

    bool sendMessage(const std::string&, std::string& error_message) override {
        error_message.clear();
        return true;
    }

    bool readMessage(std::string& message, std::string& error_message) override {
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

    void setReadMessages(std::vector<std::string> read_messages) {
        read_messages_ = std::move(read_messages);
    }

  private:
    bool                     connected_ = false;
    std::vector<std::string> read_messages_;
};

TEST_CASE("configureDapBreakpoints stores adapter verification status on enabled breakpoints") {
    auto  transport     = std::make_unique<CControllerStubDapTransport>();
    auto* raw_transport = transport.get();
    raw_transport->setReadMessages({
        R"({"type":"response","command":"setBreakpoints","success":true,"body":{"breakpoints":[{"verified":true,"line":42},{"verified":false,"line":51,"message":"No source found"}]}})",
    });

    CDapDebugSession dap_session(std::move(transport), {});
    REQUIRE(dap_session.connect());

    std::vector<SSourceBreakpointConfig> breakpoints = {
        {.source_path = "sample.cpp", .line = 42, .enabled = true, .adapter_status_known = false, .adapter_verified = false, .adapter_line = 0, .adapter_message = ""},
        {.source_path = "sample.cpp", .line = 45, .enabled = false, .adapter_status_known = false, .adapter_verified = false, .adapter_line = 0, .adapter_message = ""},
        {.source_path = "sample.cpp", .line = 51, .enabled = true, .adapter_status_known = false, .adapter_verified = false, .adapter_line = 0, .adapter_message = ""},
    };
    std::string error_message;

    REQUIRE(configureDapBreakpoints(dap_session, breakpoints, error_message));
    CHECK(error_message.empty());

    CHECK(breakpoints[0].adapter_status_known);
    CHECK(breakpoints[0].adapter_verified);
    CHECK(breakpoints[0].adapter_line == 42);

    CHECK_FALSE(breakpoints[1].adapter_status_known);
    CHECK_FALSE(breakpoints[1].adapter_verified);

    CHECK(breakpoints[2].adapter_status_known);
    CHECK_FALSE(breakpoints[2].adapter_verified);
    CHECK(breakpoints[2].adapter_line == 51);
    CHECK(breakpoints[2].adapter_message == "No source found");
}

TEST_CASE("updateDapStoppedContext preserves the selected thread when it is still available") {
    auto  transport     = std::make_unique<CControllerStubDapTransport>();
    auto* raw_transport = transport.get();
    raw_transport->setReadMessages({
        R"({"type":"response","command":"threads","success":true,"body":{"threads":[{"id":1,"name":"main"},{"id":2,"name":"worker"}]}})",
        R"({"type":"response","command":"stackTrace","success":true,"body":{"stackFrames":[{"id":2001,"name":"worker_loop","line":9,"column":1,"source":{"path":"/tmp/worker.cpp"}}]}})",
    });

    CDapDebugSession dap_session(std::move(transport), {});
    REQUIRE(dap_session.connect());

    SDebugSelection selection = {
        .thread_id   = 2,
        .frame_index = 0,
    };
    std::uint64_t         disassembly_start_address = 0;
    std::string           disassembly_memory_reference;

    const SStoppedContext stopped_context = updateDapStoppedContext(dap_session, selection, disassembly_start_address, disassembly_memory_reference);

    CHECK(selection.thread_id == 2);
    REQUIRE(stopped_context.threads.size() == 2);
    CHECK(stopped_context.threads[1].id == 2);
    CHECK(stopped_context.threads[1].name == "worker");
    REQUIRE(stopped_context.stack_frames.size() == 1);
    CHECK(stopped_context.stack_frames[0].function_name == "worker_loop");
}

TEST_CASE("bootstrapSession reports actionable DAP launch configuration guidance") {
    SAppConfig app_config                   = {};
    app_config.session_mode                 = eSessionMode::DAP_LAUNCH;
    app_config.codelldb_auto_detect.enabled = false;

    const SSessionBootstrapResult result = bootstrapSession(app_config);

    CHECK(result.state == eDebuggerSessionState::ERROR);
    CHECK(result.status_message.find("DAP launch config incomplete") != std::string::npos);
    CHECK(result.status_message.find("[dap_launch].program") != std::string::npos);
    CHECK(result.status_message.find("[dap_launch].command") != std::string::npos);
    CHECK(result.status_message.find("[dap_launch].liblldb_path") != std::string::npos);
}
