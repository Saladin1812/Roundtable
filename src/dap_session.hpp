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

struct SDapThread {
    int         id = 0;
    std::string name;
};

struct SDapThreadsResponse {
    bool                    success = false;
    std::vector<SDapThread> threads;
    std::string             error_message;
};

struct SDapStackTraceRequest {
    int         thread_id   = 0;
    std::size_t start_frame = 0;
    std::size_t levels      = 20;
};

struct SDapStackFrame {
    int         id = 0;
    std::string name;
    std::string source_path;
    std::string instruction_pointer_reference;
    int         line   = 0;
    int         column = 0;
};

struct SDapStackTraceResponse {
    bool                        success = false;
    std::vector<SDapStackFrame> stack_frames;
    std::string                 error_message;
};

struct SDapScopesRequest {
    int frame_id = 0;
};

struct SDapScope {
    std::string name;
    int         variables_reference = 0;
};

struct SDapScopesResponse {
    bool                   success = false;
    std::vector<SDapScope> scopes;
    std::string            error_message;
};

struct SDapVariablesRequest {
    int variables_reference = 0;
};

struct SDapVariable {
    std::string name;
    std::string value;
    std::string type;
    int         variables_reference = 0;
};

struct SDapVariablesResponse {
    bool                      success = false;
    std::vector<SDapVariable> variables;
    std::string               error_message;
};

struct SDapContinueRequest {
    int thread_id = 0;
};

struct SDapContinueResponse {
    bool        success = false;
    std::string error_message;
};

struct SDapEvaluateRequest {
    std::string expression;
    int         frame_id = 0;
    std::string context  = "watch";
};

struct SDapEvaluateResponse {
    bool        success = false;
    std::string result;
    std::string type;
    std::string error_message;
};

struct SDapDisassembleRequest {
    std::string memory_reference;
    std::size_t instruction_offset = 0;
    std::size_t instruction_count  = 0;
};

struct SDapDisassembledInstruction {
    std::string address;
    std::string instruction;
    std::string instruction_bytes;
};

struct SDapDisassembleResponse {
    bool                                     success = false;
    std::vector<SDapDisassembledInstruction> instructions;
    std::string                              error_message;
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
    bool        closeProcess(std::string& error_message);

    bool        connected_ = false;
    int         child_pid_ = -1;
    int         read_fd_   = -1;
    int         write_fd_  = -1;
    std::string pending_read_buffer_;
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
    bool        closeConnection(std::string& error_message);

    bool        connected_        = false;
    int         child_pid_        = -1;
    int         listen_socket_fd_ = -1;
    int         socket_fd_        = -1;
    std::string pending_read_buffer_;
};

class CDapDebugSession : public IDebugSession {
  public:
    explicit CDapDebugSession(std::unique_ptr<IDapTransport> transport, SDapEndpointConfig endpoint_config);

    static SDebugCapabilities            mapCapabilities(const SDapAdapterCapabilities& adapter_capabilities);
    static std::string                   buildInitializeRequestMessage(const SDapInitializeRequest& initialize_request);
    static SDapInitializeResponse        parseInitializeResponseMessage(const std::string& response_message);
    static std::string                   buildReadMemoryRequestMessage(int sequence_number, const SDapReadMemoryRequest& read_memory_request);
    static SDapReadMemoryResponse        parseReadMemoryResponseMessage(const std::string& response_message);
    static std::string                   buildThreadsRequestMessage(int sequence_number);
    static SDapThreadsResponse           parseThreadsResponseMessage(const std::string& response_message);
    static std::string                   buildStackTraceRequestMessage(int sequence_number, const SDapStackTraceRequest& stack_trace_request);
    static SDapStackTraceResponse        parseStackTraceResponseMessage(const std::string& response_message);
    static std::string                   buildScopesRequestMessage(int sequence_number, const SDapScopesRequest& scopes_request);
    static SDapScopesResponse            parseScopesResponseMessage(const std::string& response_message);
    static std::string                   buildVariablesRequestMessage(int sequence_number, const SDapVariablesRequest& variables_request);
    static SDapVariablesResponse         parseVariablesResponseMessage(const std::string& response_message);
    static std::string                   buildContinueRequestMessage(int sequence_number, const SDapContinueRequest& continue_request);
    static SDapContinueResponse          parseContinueResponseMessage(const std::string& response_message);
    static std::string                   buildEvaluateRequestMessage(int sequence_number, const SDapEvaluateRequest& evaluate_request);
    static SDapEvaluateResponse          parseEvaluateResponseMessage(const std::string& response_message);
    static std::string                   buildDisassembleRequestMessage(int sequence_number, const SDapDisassembleRequest& disassemble_request);
    static SDapDisassembleResponse       parseDisassembleResponseMessage(const std::string& response_message);
    static std::string                   buildLaunchRequestMessage(int sequence_number, const SDapLaunchRequest& launch_request);
    static std::string                   buildAttachRequestMessage(int sequence_number, const SDapAttachRequest& attach_request);
    static std::string                   buildConfigurationDoneRequestMessage(int sequence_number);
    static SDapProtocolMessage           parseProtocolMessage(const std::string& response_message);

    bool                                 connect();
    bool                                 initialize();
    bool                                 launch(const SDapLaunchRequest& launch_request);
    bool                                 attach(const SDapAttachRequest& attach_request);
    bool                                 configurationDone();
    bool                                 sendConfigurationDoneRequest();
    bool                                 waitForStoppedEvent();
    SDapThreadsResponse                  getThreads();
    SDapStackTraceResponse               getStackTrace(const SDapStackTraceRequest& stack_trace_request);
    SDapScopesResponse                   getScopes(const SDapScopesRequest& scopes_request);
    SDapVariablesResponse                getVariables(const SDapVariablesRequest& variables_request);
    SDapContinueResponse                 continueExecution(const SDapContinueRequest& continue_request);
    SDapEvaluateResponse                 evaluate(const SDapEvaluateRequest& evaluate_request);
    SDapDisassembleResponse              disassembleInstructions(const SDapDisassembleRequest& disassemble_request);
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
