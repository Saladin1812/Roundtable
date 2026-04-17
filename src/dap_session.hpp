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
    std::string              auth_token;
};

struct SDapAdapterCapabilities {
    bool supports_read_memory      = false;
    bool supports_write_memory     = false;
    bool supports_evaluate         = false;
    bool supports_disassemble      = false;
    bool supports_data_breakpoints = false;
};

struct SDapInitializeRequest {
    std::string client_id   = "roundtable";
    std::string client_name = "Roundtable";
};

struct SDapInitializeResponse {
    bool                    success = false;
    SDapAdapterCapabilities capabilities;
    std::string             error_message;
};

struct SDapReadMemoryRequest {
    std::string memory_reference;
    std::size_t offset = 0;
    std::size_t count  = 0;
};

struct SDapReadMemoryResponse {
    bool                      success = false;
    std::vector<std::uint8_t> memory_bytes;
    std::string               error_message;
};

struct SDapLaunchRequest {
    std::string              program;
    std::vector<std::string> arguments;
    std::string              working_directory;
    bool                     stop_on_entry = true;
};

struct SDapAttachRequest {
    std::int64_t process_id    = 0;
    bool         stop_on_entry = true;
};

struct SDapProtocolMessage {
    std::string type;
    std::string event_name;
    std::string command_name;
    bool        success = false;
};

class IDapTransport {
  public:
    virtual ~IDapTransport() = default;

    virtual bool connect(const SDapEndpointConfig& endpoint_config, std::string& error_message) = 0;
    virtual bool sendMessage(const std::string& message, std::string& error_message)            = 0;
    virtual bool readMessage(std::string& message, std::string& error_message)                  = 0;
    virtual bool isConnected() const                                                            = 0;
};

class CStdioDapTransport : public IDapTransport {
  public:
    CStdioDapTransport() = default;
    ~CStdioDapTransport() override;

    bool connect(const SDapEndpointConfig& endpoint_config, std::string& error_message) override;
    bool sendMessage(const std::string& message, std::string& error_message) override;
    bool readMessage(std::string& message, std::string& error_message) override;
    bool isConnected() const override;

  private:
    bool closeProcess(std::string& error_message);

    bool connected_ = false;
    int  child_pid_ = -1;
    int  read_fd_   = -1;
    int  write_fd_  = -1;
};

class CTcpDapTransport : public IDapTransport {
  public:
    CTcpDapTransport() = default;
    ~CTcpDapTransport() override;

    bool connect(const SDapEndpointConfig& endpoint_config, std::string& error_message) override;
    bool sendMessage(const std::string& message, std::string& error_message) override;
    bool readMessage(std::string& message, std::string& error_message) override;
    bool isConnected() const override;

  private:
    bool closeConnection(std::string& error_message);

    bool connected_        = false;
    int  child_pid_        = -1;
    int  listen_socket_fd_ = -1;
    int  socket_fd_        = -1;
};

class CDapDebugSession : public IDebugSession {
  public:
    explicit CDapDebugSession(std::unique_ptr<IDapTransport> transport, SDapEndpointConfig endpoint_config);

    static SDebugCapabilities            mapCapabilities(const SDapAdapterCapabilities& adapter_capabilities);
    static std::string                   buildInitializeRequestMessage(const SDapInitializeRequest& initialize_request);
    static SDapInitializeResponse        parseInitializeResponseMessage(const std::string& response_message);
    static std::string                   buildReadMemoryRequestMessage(int sequence_number, const SDapReadMemoryRequest& read_memory_request);
    static SDapReadMemoryResponse        parseReadMemoryResponseMessage(const std::string& response_message);
    static std::string                   buildLaunchRequestMessage(int sequence_number, const SDapLaunchRequest& launch_request);
    static std::string                   buildAttachRequestMessage(int sequence_number, const SDapAttachRequest& attach_request);
    static std::string                   buildConfigurationDoneRequestMessage(int sequence_number);
    static SDapProtocolMessage           parseProtocolMessage(const std::string& response_message);

    bool                                 connect();
    bool                                 initialize();
    bool                                 launch(const SDapLaunchRequest& launch_request);
    bool                                 attach(const SDapAttachRequest& attach_request);
    bool                                 configurationDone();
    bool                                 waitForStoppedEvent();
    bool                                 isConnected() const;
    std::string                          getLastError() const;
    void                                 setAdapterCapabilities(const SDapAdapterCapabilities& adapter_capabilities);

    SDebugCapabilities                   getCapabilities() override;
    std::vector<SLocalVariable>          getLocals(const SDebugSelection& selection) override;
    SMemoryReadResult                    readMemory(const SDebugSelection& selection, const SMemoryReadRequest& request) override;
    std::vector<SWatchResult>            evaluateWatches(const SDebugSelection& selection, const std::vector<SWatchExpression>& watch_expressions) override;
    std::vector<SDisassemblyInstruction> disassemble(const SDebugSelection& selection, std::uint64_t start_address, std::size_t instruction_count) override;

  private:
    std::unique_ptr<IDapTransport> transport_;
    SDapEndpointConfig             endpoint_config_;
    SDapAdapterCapabilities        adapter_capabilities_;
    std::string                    last_error_;
    int                            next_sequence_number_ = 1;
};
