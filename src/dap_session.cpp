#include "dap_session.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

CStdioDapTransport::~CStdioDapTransport() {
    std::string error_message;
    closeProcess(error_message);
}

bool CStdioDapTransport::connect(const SDapEndpointConfig& endpoint_config, std::string& error_message) {
    if (endpoint_config.transport_kind != eDapTransportKind::STDIO) {
        error_message = "CStdioDapTransport only supports stdio endpoints";
        return false;
    }

    if (endpoint_config.command.empty()) {
        error_message = "DAP adapter command is empty";
        return false;
    }

    std::string close_error;
    closeProcess(close_error);

    int stdin_pipe[2]  = {-1, -1};
    int stdout_pipe[2] = {-1, -1};

    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
        error_message = "Failed to create stdio pipes";
        if (stdin_pipe[0] != -1) {
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
        }
        if (stdout_pipe[0] != -1) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
        }
        return false;
    }

    const pid_t child_pid = fork();
    if (child_pid < 0) {
        error_message = "Failed to fork DAP adapter process";
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return false;
    }

    if (child_pid == 0) {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);

        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);

        std::vector<char*> argv;
        argv.reserve(endpoint_config.arguments.size() + 2);
        argv.push_back(const_cast<char*>(endpoint_config.command.c_str()));
        for (const auto& argument : endpoint_config.arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);

        execvp(endpoint_config.command.c_str(), argv.data());
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    child_pid_ = child_pid;
    write_fd_  = stdin_pipe[1];
    read_fd_   = stdout_pipe[0];
    connected_ = true;
    error_message.clear();
    return true;
}

bool CStdioDapTransport::sendMessage(const std::string& message, std::string& error_message) {
    if (!connected_ || write_fd_ < 0) {
        error_message = "DAP stdio transport is not connected";
        return false;
    }

    const std::string framed_message = "Content-Length: " + std::to_string(message.size()) + "\r\n\r\n" + message;
    std::size_t       written_total  = 0;

    while (written_total < framed_message.size()) {
        const auto written_now = write(write_fd_, framed_message.data() + written_total, framed_message.size() - written_total);
        if (written_now <= 0) {
            error_message = "Failed to write DAP message to adapter";
            return false;
        }
        written_total += static_cast<std::size_t>(written_now);
    }

    error_message.clear();
    return true;
}

bool CStdioDapTransport::readMessage(std::string& message, std::string& error_message) {
    if (!connected_ || read_fd_ < 0) {
        error_message = "DAP stdio transport is not connected";
        return false;
    }

    std::string         header;
    std::array<char, 1> byte_buffer = {};

    while (header.find("\r\n\r\n") == std::string::npos) {
        const auto read_now = read(read_fd_, byte_buffer.data(), byte_buffer.size());
        if (read_now <= 0) {
            error_message = "Failed to read DAP header from adapter";
            return false;
        }

        header.append(byte_buffer.data(), static_cast<std::size_t>(read_now));
    }

    const auto content_length_prefix = header.find("Content-Length:");
    if (content_length_prefix == std::string::npos) {
        error_message = "DAP response did not include Content-Length header";
        return false;
    }

    const auto        value_start = content_length_prefix + std::string("Content-Length:").size();
    const auto        line_end    = header.find("\r\n", value_start);
    const auto        body_start  = header.find("\r\n\r\n") + 4;

    const std::size_t content_length = static_cast<std::size_t>(std::stoul(header.substr(value_start, line_end - value_start)));

    message = header.substr(body_start);
    while (message.size() < content_length) {
        std::array<char, 4096> chunk_buffer = {};
        const auto             read_now     = read(read_fd_, chunk_buffer.data(), chunk_buffer.size());
        if (read_now <= 0) {
            error_message = "Failed to read DAP body from adapter";
            return false;
        }

        message.append(chunk_buffer.data(), static_cast<std::size_t>(read_now));
    }

    message.resize(content_length);
    error_message.clear();
    return true;
}

bool CStdioDapTransport::isConnected() const {
    return connected_;
}

bool CStdioDapTransport::closeProcess(std::string& error_message) {
    if (write_fd_ >= 0) {
        close(write_fd_);
        write_fd_ = -1;
    }

    if (read_fd_ >= 0) {
        close(read_fd_);
        read_fd_ = -1;
    }

    if (child_pid_ > 0) {
        kill(child_pid_, SIGTERM);
        int wait_status = 0;
        waitpid(child_pid_, &wait_status, 0);
        child_pid_ = -1;
    }

    connected_ = false;
    error_message.clear();
    return true;
}

CDapDebugSession::CDapDebugSession(std::unique_ptr<IDapTransport> transport, SDapEndpointConfig endpoint_config) :
    transport_(std::move(transport)), endpoint_config_(std::move(endpoint_config)) {}

SDebugCapabilities CDapDebugSession::mapCapabilities(const SDapAdapterCapabilities& adapter_capabilities) {
    return {
        .supports_memory_read       = adapter_capabilities.supports_read_memory,
        .supports_memory_write      = adapter_capabilities.supports_write_memory,
        .supports_watch_expressions = adapter_capabilities.supports_evaluate,
        .supports_disassembly       = adapter_capabilities.supports_disassemble,
        .supports_data_breakpoints  = adapter_capabilities.supports_data_breakpoints,
    };
}

std::string CDapDebugSession::buildInitializeRequestMessage(const SDapInitializeRequest& initialize_request) {
    return R"({"seq":1,"type":"request","command":"initialize","arguments":{"clientID":")" + initialize_request.client_id + R"(","clientName":")" + initialize_request.client_name +
        R"(","adapterID":"codelldb","linesStartAt1":true,"columnsStartAt1":true,"pathFormat":"path"}})";
}

SDapInitializeResponse CDapDebugSession::parseInitializeResponseMessage(const std::string& response_message) {
    SDapInitializeResponse response = {};

    if (response_message.find("\"success\":true") == std::string::npos) {
        response.error_message = "DAP initialize response did not report success";
        return response;
    }

    response.success                                = true;
    response.capabilities.supports_read_memory      = response_message.find("\"supportsReadMemoryRequest\":true") != std::string::npos;
    response.capabilities.supports_write_memory     = response_message.find("\"supportsWriteMemoryRequest\":true") != std::string::npos;
    response.capabilities.supports_evaluate         = response_message.find("\"supportsEvaluateForHovers\":true") != std::string::npos;
    response.capabilities.supports_disassemble      = response_message.find("\"supportsDisassembleRequest\":true") != std::string::npos;
    response.capabilities.supports_data_breakpoints = response_message.find("\"supportsDataBreakpoints\":true") != std::string::npos;
    return response;
}

bool CDapDebugSession::connect() {
    if (!transport_) {
        last_error_ = "DAP transport is not configured";
        return false;
    }

    std::string error_message;
    if (!transport_->connect(endpoint_config_, error_message)) {
        last_error_ = error_message;
        return false;
    }

    last_error_.clear();
    return true;
}

bool CDapDebugSession::initialize() {
    if (!isConnected()) {
        last_error_ = "DAP session is not connected";
        return false;
    }

    std::string error_message;
    const auto  request_message = buildInitializeRequestMessage({});

    if (!transport_->sendMessage(request_message, error_message)) {
        last_error_ = error_message;
        return false;
    }

    std::string response_message;
    if (!transport_->readMessage(response_message, error_message)) {
        last_error_ = error_message;
        return false;
    }

    const auto response = parseInitializeResponseMessage(response_message);
    if (!response.success) {
        last_error_ = response.error_message;
        return false;
    }

    adapter_capabilities_ = response.capabilities;
    last_error_.clear();
    return true;
}

bool CDapDebugSession::isConnected() const {
    return transport_ != nullptr && transport_->isConnected();
}

std::string CDapDebugSession::getLastError() const {
    return last_error_;
}

void CDapDebugSession::setAdapterCapabilities(const SDapAdapterCapabilities& adapter_capabilities) {
    adapter_capabilities_ = adapter_capabilities;
}

SDebugCapabilities CDapDebugSession::getCapabilities() const {
    return mapCapabilities(adapter_capabilities_);
}

std::vector<SLocalVariable> CDapDebugSession::getLocals(const SDebugSelection& selection) const {
    static_cast<void>(selection);
    return {};
}

SMemoryReadResult CDapDebugSession::readMemory(const SDebugSelection& selection, const SMemoryReadRequest& request) const {
    static_cast<void>(selection);

    if (!isConnected()) {
        return {
            .start_address = request.start_address,
            .memory_bytes  = {},
            .bytes_per_row = request.bytes_per_row,
            .error_message = "DAP session is not connected",
        };
    }

    return {
        .start_address = request.start_address,
        .memory_bytes  = {},
        .bytes_per_row = request.bytes_per_row,
        .error_message = "DAP readMemory is not implemented yet",
    };
}

std::vector<SWatchResult> CDapDebugSession::evaluateWatches(const SDebugSelection& selection, const std::vector<SWatchExpression>& watch_expressions) const {
    static_cast<void>(selection);

    std::vector<SWatchResult> watch_results;
    watch_results.reserve(watch_expressions.size());

    const std::string error_message = isConnected() ? "DAP watch evaluation is not implemented yet" : "DAP session is not connected";

    for (const auto& watch_expression : watch_expressions) {
        watch_results.push_back({
            .expression    = watch_expression.expression,
            .value         = "",
            .type          = "",
            .error_message = error_message,
        });
    }

    return watch_results;
}

std::vector<SDisassemblyInstruction> CDapDebugSession::disassemble(const SDebugSelection& selection, std::uint64_t start_address, std::size_t instruction_count) const {
    static_cast<void>(selection);
    static_cast<void>(start_address);
    static_cast<void>(instruction_count);

    return {};
}
