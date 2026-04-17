#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "debug_session.hpp"

enum class eDapTransportKind : std::uint8_t {
    STDIO,
    TCP,
};

struct SDapEndpointConfig {
    eDapTransportKind        transport_kind = eDapTransportKind::STDIO;
    std::string              command;
    std::vector<std::string> arguments;
    std::string              host = "127.0.0.1";
    std::uint16_t            port = 0;
};

struct SDapAdapterCapabilities {
    bool supports_read_memory      = false;
    bool supports_write_memory     = false;
    bool supports_evaluate         = false;
    bool supports_disassemble      = false;
    bool supports_data_breakpoints = false;
};

class IDapTransport {
  public:
    virtual ~IDapTransport() = default;

    virtual bool connect(const SDapEndpointConfig& endpoint_config, std::string& error_message) = 0;
    virtual bool sendMessage(const std::string& message, std::string& error_message)            = 0;
    virtual bool readMessage(std::string& message, std::string& error_message)                  = 0;
    virtual bool isConnected() const                                                            = 0;
};

class CDapDebugSession : public IDebugSession {
  public:
    explicit CDapDebugSession(std::unique_ptr<IDapTransport> transport, SDapEndpointConfig endpoint_config);

    static SDebugCapabilities            mapCapabilities(const SDapAdapterCapabilities& adapter_capabilities);

    bool                                 connect();
    bool                                 isConnected() const;
    std::string                          getLastError() const;
    void                                 setAdapterCapabilities(const SDapAdapterCapabilities& adapter_capabilities);

    SDebugCapabilities                   getCapabilities() const override;
    std::vector<SLocalVariable>          getLocals(const SDebugSelection& selection) const override;
    SMemoryReadResult                    readMemory(const SDebugSelection& selection, const SMemoryReadRequest& request) const override;
    std::vector<SWatchResult>            evaluateWatches(const SDebugSelection& selection, const std::vector<SWatchExpression>& watch_expressions) const override;
    std::vector<SDisassemblyInstruction> disassemble(const SDebugSelection& selection, std::uint64_t start_address, std::size_t instruction_count) const override;

  private:
    std::unique_ptr<IDapTransport> transport_;
    SDapEndpointConfig             endpoint_config_;
    SDapAdapterCapabilities        adapter_capabilities_;
    std::string                    last_error_;
};
