#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app_config.hpp"
#include "dap_session.hpp"
#include "debug_session.hpp"

enum class eDebuggerSessionState : std::uint8_t {
    MOCK,
    LAUNCHING,
    STOPPED,
    RUNNING,
    TERMINATING,
    TERMINATED,
    ERROR,
};

struct SStoppedLocation {
    std::string function_name;
    std::string source_path;
    int         line   = 0;
    int         column = 0;
};

struct SSessionBootstrapResult {
    std::unique_ptr<IDebugSession> session;
    SDebugSelection                selection;
    std::uint64_t                  disassembly_start_address = 0x401000;
    std::string                    disassembly_memory_reference;
    SStoppedLocation               stopped_location;
    std::string                    status_message;
    eDebuggerSessionState          state = eDebuggerSessionState::ERROR;
};

bool                    configureDapBreakpoints(CDapDebugSession& dap_session, const std::vector<SSourceBreakpointConfig>& breakpoints, std::string& error_message);
SStoppedLocation        updateDapStoppedContext(CDapDebugSession& dap_session, SDebugSelection& selection, std::uint64_t& disassembly_start_address,
                                                std::string& disassembly_memory_reference);
SSessionBootstrapResult bootstrapSession(const SAppConfig& app_config);
