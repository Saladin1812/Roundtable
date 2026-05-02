#include "dap_session.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

    constexpr bool kEnableDapLogging = false;

    void           logDapMessage(const char* prefix, const std::string& message) {
        if (!kEnableDapLogging) {
            return;
        }

        std::cerr << prefix << message << '\n';
    }

    bool readFramedMessageFromFileDescriptor(int file_descriptor, std::string& pending_buffer, std::string& message, std::string& error_message);

} // namespace

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

        std::vector<std::string> argument_strings;
        argument_strings.reserve(endpoint_config.arguments.size() + 1);
        argument_strings.push_back(endpoint_config.command);
        for (const auto& argument : endpoint_config.arguments) {
            argument_strings.push_back(argument);
        }

        std::vector<std::vector<char>> argument_buffers;
        argument_buffers.reserve(argument_strings.size());
        for (const auto& argument_string : argument_strings) {
            argument_buffers.emplace_back(argument_string.begin(), argument_string.end());
            argument_buffers.back().push_back('\0');
        }

        std::vector<char*> argv;
        argv.reserve(argument_buffers.size() + 1);
        for (auto& argument_buffer : argument_buffers) {
            argv.push_back(argument_buffer.data());
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    child_pid_ = child_pid;
    write_fd_  = stdin_pipe[1];
    read_fd_   = stdout_pipe[0];
    connected_ = true;
    pending_read_buffer_.clear();
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

    return readFramedMessageFromFileDescriptor(read_fd_, pending_read_buffer_, message, error_message);
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
    pending_read_buffer_.clear();
    error_message.clear();
    return true;
}

CTcpDapTransport::~CTcpDapTransport() {
    std::string error_message;
    closeConnection(error_message);
}

namespace {

    bool writeAllToFileDescriptor(int file_descriptor, const std::string& data, std::string& error_message) {
        std::size_t written_total = 0;
        while (written_total < data.size()) {
            const auto written_now = write(file_descriptor, data.data() + written_total, data.size() - written_total);
            if (written_now <= 0) {
                error_message = "Failed to write DAP message";
                return false;
            }
            written_total += static_cast<std::size_t>(written_now);
        }

        error_message.clear();
        return true;
    }

    bool readFramedMessageFromFileDescriptor(int file_descriptor, std::string& pending_buffer, std::string& message, std::string& error_message) {
        while (true) {
            const auto header_end = pending_buffer.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                const auto content_length_prefix = pending_buffer.find("Content-Length:");
                if (content_length_prefix == std::string::npos || content_length_prefix > header_end) {
                    error_message = "DAP response did not include Content-Length header";
                    return false;
                }

                const auto        value_start    = content_length_prefix + std::string("Content-Length:").size();
                const auto        line_end       = pending_buffer.find("\r\n", value_start);
                const std::size_t content_length = static_cast<std::size_t>(std::stoul(pending_buffer.substr(value_start, line_end - value_start)));
                const auto        body_start     = header_end + 4;

                if (pending_buffer.size() >= body_start + content_length) {
                    message = pending_buffer.substr(body_start, content_length);
                    pending_buffer.erase(0, body_start + content_length);
                    error_message.clear();
                    return true;
                }
            }

            std::array<char, 4096> chunk_buffer = {};
            const auto             read_now     = read(file_descriptor, chunk_buffer.data(), chunk_buffer.size());
            if (read_now <= 0) {
                error_message = header_end == std::string::npos ? "Failed to read DAP header" : "Failed to read DAP body";
                return false;
            }

            pending_buffer.append(chunk_buffer.data(), static_cast<std::size_t>(read_now));
        }
    }

} // namespace

bool CTcpDapTransport::connect(const SDapEndpointConfig& endpoint_config, std::string& error_message) {
    if (endpoint_config.transport_kind != eDapTransportKind::TCP) {
        error_message = "CTcpDapTransport only supports TCP endpoints";
        return false;
    }

    if (endpoint_config.command.empty()) {
        error_message = "DAP adapter command is empty";
        return false;
    }

    std::string close_error;
    closeConnection(close_error);

    int port_probe_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (port_probe_socket < 0) {
        error_message = "Failed to create TCP probe socket";
        return false;
    }

    sockaddr_in probe_address     = {};
    probe_address.sin_family      = AF_INET;
    probe_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    probe_address.sin_port        = htons(endpoint_config.port);

    if (bind(port_probe_socket, reinterpret_cast<sockaddr*>(&probe_address), sizeof(probe_address)) != 0) {
        error_message = "Failed to bind TCP probe socket";
        close(port_probe_socket);
        return false;
    }

    sockaddr_in bound_address = {};
    socklen_t   bound_length  = sizeof(bound_address);
    if (getsockname(port_probe_socket, reinterpret_cast<sockaddr*>(&bound_address), &bound_length) != 0) {
        error_message = "Failed to query TCP probe socket address";
        close(port_probe_socket);
        return false;
    }

    const auto bound_port = ntohs(bound_address.sin_port);
    close(port_probe_socket);
    const pid_t child_pid = fork();
    if (child_pid < 0) {
        error_message = "Failed to fork DAP adapter process";
        closeConnection(close_error);
        return false;
    }

    if (child_pid == 0) {
        std::vector<std::string> argument_strings;
        argument_strings.reserve(endpoint_config.arguments.size() + 5);
        argument_strings.push_back(endpoint_config.command);
        for (const auto& argument : endpoint_config.arguments) {
            argument_strings.push_back(argument);
        }
        argument_strings.emplace_back("--port");
        argument_strings.push_back(std::to_string(bound_port));
        if (!endpoint_config.auth_token.empty()) {
            argument_strings.emplace_back("--auth-token");
            argument_strings.push_back(endpoint_config.auth_token);
        }

        std::vector<std::vector<char>> argument_buffers;
        argument_buffers.reserve(argument_strings.size());
        for (const auto& argument_string : argument_strings) {
            argument_buffers.emplace_back(argument_string.begin(), argument_string.end());
            argument_buffers.back().push_back('\0');
        }

        std::vector<char*> argv;
        argv.reserve(argument_buffers.size() + 1);
        for (auto& argument_buffer : argument_buffers) {
            argv.push_back(argument_buffer.data());
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    child_pid_                 = child_pid;
    sockaddr_in server_address = {};
    server_address.sin_family  = AF_INET;
    server_address.sin_port    = htons(bound_port);
    inet_pton(AF_INET, endpoint_config.host.c_str(), &server_address.sin_addr);

    for (int attempt = 0; attempt < 50; ++attempt) {
        socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd_ < 0) {
            error_message = "Failed to create TCP client socket";
            closeConnection(close_error);
            return false;
        }

        if (::connect(socket_fd_, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) == 0) {
            connected_ = true;
            pending_read_buffer_.clear();
            error_message.clear();
            return true;
        }

        close(socket_fd_);
        socket_fd_ = -1;
        usleep(100000);
    }

    error_message = "Failed to connect to DAP adapter TCP port";
    closeConnection(close_error);
    return false;
}

bool CTcpDapTransport::sendMessage(const std::string& message, std::string& error_message) {
    if (!connected_ || socket_fd_ < 0) {
        error_message = "DAP TCP transport is not connected";
        return false;
    }

    const std::string framed_message = "Content-Length: " + std::to_string(message.size()) + "\r\n\r\n" + message;
    return writeAllToFileDescriptor(socket_fd_, framed_message, error_message);
}

bool CTcpDapTransport::readMessage(std::string& message, std::string& error_message) {
    if (!connected_ || socket_fd_ < 0) {
        error_message = "DAP TCP transport is not connected";
        return false;
    }

    return readFramedMessageFromFileDescriptor(socket_fd_, pending_read_buffer_, message, error_message);
}

bool CTcpDapTransport::isConnected() const {
    return connected_;
}

bool CTcpDapTransport::closeConnection(std::string& error_message) {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }

    if (listen_socket_fd_ >= 0) {
        close(listen_socket_fd_);
        listen_socket_fd_ = -1;
    }

    if (child_pid_ > 0) {
        kill(child_pid_, SIGTERM);
        int wait_status = 0;
        waitpid(child_pid_, &wait_status, 0);
        child_pid_ = -1;
    }

    connected_ = false;
    pending_read_buffer_.clear();
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

std::string CDapDebugSession::buildReadMemoryRequestMessage(int sequence_number, const SDapReadMemoryRequest& read_memory_request) {
    return "{\"seq\":" + std::to_string(sequence_number) + R"(,"type":"request","command":"readMemory","arguments":{"memoryReference":")" + read_memory_request.memory_reference +
        R"(","offset":)" + std::to_string(read_memory_request.offset) + ",\"count\":" + std::to_string(read_memory_request.count) + "}}";
}

std::string CDapDebugSession::buildThreadsRequestMessage(int sequence_number) {
    return "{\"seq\":" + std::to_string(sequence_number) + R"(,"type":"request","command":"threads","arguments":{}})";
}

std::string CDapDebugSession::buildStackTraceRequestMessage(int sequence_number, const SDapStackTraceRequest& stack_trace_request) {
    return "{\"seq\":" + std::to_string(sequence_number) + R"(,"type":"request","command":"stackTrace","arguments":{"threadId":)" + std::to_string(stack_trace_request.thread_id) +
        R"(,"startFrame":)" + std::to_string(stack_trace_request.start_frame) + R"(,"levels":)" + std::to_string(stack_trace_request.levels) + "}}";
}

std::string CDapDebugSession::buildScopesRequestMessage(int sequence_number, const SDapScopesRequest& scopes_request) {
    return "{\"seq\":" + std::to_string(sequence_number) + R"(,"type":"request","command":"scopes","arguments":{"frameId":)" + std::to_string(scopes_request.frame_id) + "}}";
}

std::string CDapDebugSession::buildVariablesRequestMessage(int sequence_number, const SDapVariablesRequest& variables_request) {
    return "{\"seq\":" + std::to_string(sequence_number) + R"(,"type":"request","command":"variables","arguments":{"variablesReference":)" +
        std::to_string(variables_request.variables_reference) + "}}";
}

std::string CDapDebugSession::buildContinueRequestMessage(int sequence_number, const SDapContinueRequest& continue_request) {
    return "{\"seq\":" + std::to_string(sequence_number) + R"(,"type":"request","command":"continue","arguments":{"threadId":)" + std::to_string(continue_request.thread_id) + "}}";
}

std::string CDapDebugSession::buildEvaluateRequestMessage(int sequence_number, const SDapEvaluateRequest& evaluate_request) {
    return "{\"seq\":" + std::to_string(sequence_number) + R"(,"type":"request","command":"evaluate","arguments":{"expression":")" + evaluate_request.expression +
        R"(","frameId":)" + std::to_string(evaluate_request.frame_id) + R"(,"context":")" + evaluate_request.context + R"("}})";
}

std::string CDapDebugSession::buildDisassembleRequestMessage(int sequence_number, const SDapDisassembleRequest& disassemble_request) {
    return "{\"seq\":" + std::to_string(sequence_number) + R"(,"type":"request","command":"disassemble","arguments":{"memoryReference":")" + disassemble_request.memory_reference +
        R"(","instructionOffset":)" + std::to_string(disassemble_request.instruction_offset) + R"(,"instructionCount":)" + std::to_string(disassemble_request.instruction_count) +
        "}}";
}

namespace {

    std::optional<std::string> extractJsonStringField(const std::string& response_message, const std::string& field_name) {
        const auto field_pattern = "\"" + field_name + "\":\"";
        const auto field_start   = response_message.find(field_pattern);
        if (field_start == std::string::npos) {
            return std::nullopt;
        }

        const auto value_start = field_start + field_pattern.size();
        const auto value_end   = response_message.find('"', value_start);
        if (value_end == std::string::npos) {
            return std::nullopt;
        }

        return response_message.substr(value_start, value_end - value_start);
    }

    std::optional<int> extractJsonIntegerField(const std::string& response_message, const std::string& field_name) {
        const auto field_pattern = "\"" + field_name + "\":";
        const auto field_start   = response_message.find(field_pattern);
        if (field_start == std::string::npos) {
            return std::nullopt;
        }

        const auto value_start = field_start + field_pattern.size();
        const auto value_end   = response_message.find_first_not_of("0123456789", value_start);
        if (value_end == value_start) {
            return std::nullopt;
        }

        return std::stoi(response_message.substr(value_start, value_end - value_start));
    }

    std::vector<std::string> extractTopLevelObjectsFromArray(const std::string& response_message, const std::string& array_name) {
        std::vector<std::string> objects;

        const auto               array_position = response_message.find("\"" + array_name + "\":[");
        if (array_position == std::string::npos) {
            return objects;
        }

        const auto array_start = response_message.find('[', array_position);
        if (array_start == std::string::npos) {
            return objects;
        }

        std::size_t search_position = array_start + 1;
        int         object_depth    = 0;
        std::size_t object_start    = std::string::npos;

        while (search_position < response_message.size()) {
            const char current_character = response_message[search_position];

            if (current_character == '{') {
                if (object_depth == 0) {
                    object_start = search_position;
                }
                ++object_depth;
            } else if (current_character == '}') {
                --object_depth;
                if (object_depth == 0 && object_start != std::string::npos) {
                    objects.push_back(response_message.substr(object_start, search_position - object_start + 1));
                    object_start = std::string::npos;
                }
            } else if (current_character == ']' && object_depth == 0) {
                break;
            }

            ++search_position;
        }

        return objects;
    }

    int decodeBase64Value(char character) {
        if (character >= 'A' && character <= 'Z') {
            return character - 'A';
        }
        if (character >= 'a' && character <= 'z') {
            return character - 'a' + 26;
        }
        if (character >= '0' && character <= '9') {
            return character - '0' + 52;
        }
        if (character == '+') {
            return 62;
        }
        if (character == '/') {
            return 63;
        }
        return -1;
    }

    std::vector<std::uint8_t> decodeBase64(const std::string& encoded_bytes) {
        std::vector<std::uint8_t> decoded_bytes;
        int                       buffer         = 0;
        int                       bits_in_buffer = 0;

        for (const char character : encoded_bytes) {
            if (character == '=') {
                break;
            }

            const int decoded_value = decodeBase64Value(character);
            if (decoded_value < 0) {
                continue;
            }

            buffer = (buffer << 6) | decoded_value;
            bits_in_buffer += 6;

            while (bits_in_buffer >= 8) {
                bits_in_buffer -= 8;
                decoded_bytes.push_back(static_cast<std::uint8_t>((buffer >> bits_in_buffer) & 0xFF));
            }
        }

        return decoded_bytes;
    }

    std::string formatMemoryReference(std::uint64_t start_address) {
        std::ostringstream address_stream;
        address_stream << "0x" << std::hex << std::uppercase << start_address;
        return address_stream.str();
    }

    std::uint64_t parseAddressString(const std::string& address) {
        std::size_t parsed_characters = 0;
        return std::stoull(address, &parsed_characters, 0);
    }

} // namespace

SDapReadMemoryResponse CDapDebugSession::parseReadMemoryResponseMessage(const std::string& response_message) {
    SDapReadMemoryResponse response = {};

    if (response_message.find("\"success\":true") == std::string::npos) {
        response.error_message = "DAP readMemory response did not report success";
        return response;
    }

    const auto encoded_bytes = extractJsonStringField(response_message, "data");
    if (!encoded_bytes.has_value()) {
        response.error_message = "DAP readMemory response did not include data";
        return response;
    }

    response.success      = true;
    response.memory_bytes = decodeBase64(encoded_bytes.value());
    return response;
}

SDapThreadsResponse CDapDebugSession::parseThreadsResponseMessage(const std::string& response_message) {
    SDapThreadsResponse response = {};

    if (response_message.find("\"success\":true") == std::string::npos) {
        response.error_message = "DAP threads response did not report success";
        return response;
    }

    response.success = true;

    std::size_t search_position = 0;
    while (true) {
        const auto id_position = response_message.find("\"id\":", search_position);
        if (id_position == std::string::npos) {
            break;
        }

        const auto id_value_start = id_position + std::string("\"id\":").size();
        const auto id_value_end   = response_message.find_first_not_of("0123456789", id_value_start);

        SDapThread thread = {};
        thread.id         = std::stoi(response_message.substr(id_value_start, id_value_end - id_value_start));

        const auto name_position = response_message.find(R"("name":")", id_value_end);
        if (name_position != std::string::npos) {
            const auto name_value_start = name_position + std::string(R"("name":")").size();
            const auto name_value_end   = response_message.find('"', name_value_start);
            if (name_value_end != std::string::npos) {
                thread.name     = response_message.substr(name_value_start, name_value_end - name_value_start);
                search_position = name_value_end;
            } else {
                search_position = id_value_end;
            }
        } else {
            search_position = id_value_end;
        }

        response.threads.push_back(std::move(thread));
    }

    return response;
}

SDapStackTraceResponse CDapDebugSession::parseStackTraceResponseMessage(const std::string& response_message) {
    SDapStackTraceResponse response = {};

    if (response_message.find("\"success\":true") == std::string::npos) {
        response.error_message = "DAP stackTrace response did not report success";
        return response;
    }

    response.success = true;

    const auto stack_frames_position = response_message.find(R"("stackFrames":[)");
    if (stack_frames_position == std::string::npos) {
        return response;
    }

    const auto stack_frames_start = response_message.find('[', stack_frames_position);
    if (stack_frames_start == std::string::npos) {
        return response;
    }

    std::size_t search_position = stack_frames_start + 1;
    int         object_depth    = 0;
    std::size_t object_start    = std::string::npos;

    while (search_position < response_message.size()) {
        const char current_character = response_message[search_position];

        if (current_character == '{') {
            if (object_depth == 0) {
                object_start = search_position;
            }
            ++object_depth;
        } else if (current_character == '}') {
            --object_depth;
            if (object_depth == 0 && object_start != std::string::npos) {
                const std::string frame_message = response_message.substr(object_start, search_position - object_start + 1);
                SDapStackFrame    stack_frame   = {};

                const auto        id_position = frame_message.find("\"id\":");
                if (id_position != std::string::npos) {
                    const auto id_value_start = id_position + std::string("\"id\":").size();
                    const auto id_value_end   = frame_message.find_first_not_of("0123456789", id_value_start);
                    stack_frame.id            = std::stoi(frame_message.substr(id_value_start, id_value_end - id_value_start));
                }

                const auto name_position = frame_message.find(R"("name":")");
                if (name_position != std::string::npos) {
                    const auto name_value_start = name_position + std::string(R"("name":")").size();
                    const auto name_value_end   = frame_message.find('"', name_value_start);
                    if (name_value_end != std::string::npos) {
                        stack_frame.name = frame_message.substr(name_value_start, name_value_end - name_value_start);
                    }
                }

                const auto instruction_pointer_reference_position = frame_message.find(R"("instructionPointerReference":")");
                if (instruction_pointer_reference_position != std::string::npos) {
                    const auto value_start = instruction_pointer_reference_position + std::string(R"("instructionPointerReference":")").size();
                    const auto value_end   = frame_message.find('"', value_start);
                    if (value_end != std::string::npos) {
                        stack_frame.instruction_pointer_reference = frame_message.substr(value_start, value_end - value_start);
                    }
                }

                const auto line_position = frame_message.find("\"line\":");
                if (line_position != std::string::npos) {
                    const auto line_value_start = line_position + std::string("\"line\":").size();
                    const auto line_value_end   = frame_message.find_first_not_of("0123456789", line_value_start);
                    stack_frame.line            = std::stoi(frame_message.substr(line_value_start, line_value_end - line_value_start));
                }

                const auto column_position = frame_message.find("\"column\":");
                if (column_position != std::string::npos) {
                    const auto column_value_start = column_position + std::string("\"column\":").size();
                    const auto column_value_end   = frame_message.find_first_not_of("0123456789", column_value_start);
                    stack_frame.column            = std::stoi(frame_message.substr(column_value_start, column_value_end - column_value_start));
                }

                const auto path_position = frame_message.find(R"("path":")");
                if (path_position != std::string::npos) {
                    const auto path_value_start = path_position + std::string(R"("path":")").size();
                    const auto path_value_end   = frame_message.find('"', path_value_start);
                    if (path_value_end != std::string::npos) {
                        stack_frame.source_path = frame_message.substr(path_value_start, path_value_end - path_value_start);
                    }
                }

                if (stack_frame.id != 0 || !stack_frame.name.empty()) {
                    response.stack_frames.push_back(std::move(stack_frame));
                }

                object_start = std::string::npos;
            }
        } else if (current_character == ']' && object_depth == 0) {
            break;
        }

        ++search_position;
    }

    return response;
}

SDapScopesResponse CDapDebugSession::parseScopesResponseMessage(const std::string& response_message) {
    SDapScopesResponse response = {};

    if (response_message.find("\"success\":true") == std::string::npos) {
        response.error_message = "DAP scopes response did not report success";
        return response;
    }

    response.success = true;

    for (const auto& scope_message : extractTopLevelObjectsFromArray(response_message, "scopes")) {
        SDapScope  scope = {};

        const auto name = extractJsonStringField(scope_message, "name");
        if (name.has_value()) {
            scope.name = name.value();
        }

        const auto variables_reference = extractJsonIntegerField(scope_message, "variablesReference");
        if (variables_reference.has_value()) {
            scope.variables_reference = variables_reference.value();
        }

        if (!scope.name.empty() || scope.variables_reference != 0) {
            response.scopes.push_back(std::move(scope));
        }
    }

    return response;
}

SDapVariablesResponse CDapDebugSession::parseVariablesResponseMessage(const std::string& response_message) {
    SDapVariablesResponse response = {};

    if (response_message.find("\"success\":true") == std::string::npos) {
        response.error_message = "DAP variables response did not report success";
        return response;
    }

    response.success = true;

    for (const auto& variable_message : extractTopLevelObjectsFromArray(response_message, "variables")) {
        SDapVariable variable = {};

        const auto   name = extractJsonStringField(variable_message, "name");
        if (name.has_value()) {
            variable.name = name.value();
        }

        const auto value = extractJsonStringField(variable_message, "value");
        if (value.has_value()) {
            variable.value = value.value();
        }

        const auto type = extractJsonStringField(variable_message, "type");
        if (type.has_value()) {
            variable.type = type.value();
        }

        const auto variables_reference = extractJsonIntegerField(variable_message, "variablesReference");
        if (variables_reference.has_value()) {
            variable.variables_reference = variables_reference.value();
        }

        if (!variable.name.empty()) {
            response.variables.push_back(std::move(variable));
        }
    }

    return response;
}

SDapContinueResponse CDapDebugSession::parseContinueResponseMessage(const std::string& response_message) {
    SDapContinueResponse response = {};

    if (response_message.find("\"success\":true") == std::string::npos) {
        response.error_message = "DAP continue response did not report success";
        return response;
    }

    response.success = true;
    return response;
}

SDapEvaluateResponse CDapDebugSession::parseEvaluateResponseMessage(const std::string& response_message) {
    SDapEvaluateResponse response = {};

    if (response_message.find("\"success\":true") == std::string::npos) {
        const auto adapter_message = extractJsonStringField(response_message, "message");
        response.error_message     = adapter_message.has_value() ? adapter_message.value() : "DAP evaluate response did not report success";
        return response;
    }

    const auto body_position = response_message.find(R"("body":{)");
    const auto body_message  = body_position == std::string::npos ? response_message : response_message.substr(body_position);

    const auto result = extractJsonStringField(body_message, "result");
    if (!result.has_value()) {
        response.error_message = "DAP evaluate response did not include result";
        return response;
    }

    response.success = true;
    response.result  = result.value();

    const auto type = extractJsonStringField(body_message, "type");
    if (type.has_value()) {
        response.type = type.value();
    }

    return response;
}

SDapDisassembleResponse CDapDebugSession::parseDisassembleResponseMessage(const std::string& response_message) {
    SDapDisassembleResponse response = {};

    if (response_message.find("\"success\":true") == std::string::npos) {
        response.error_message = "DAP disassemble response did not report success";
        return response;
    }

    response.success = true;

    for (const auto& instruction_message : extractTopLevelObjectsFromArray(response_message, "instructions")) {
        SDapDisassembledInstruction instruction = {};

        if (const auto address = extractJsonStringField(instruction_message, "address"); address.has_value()) {
            instruction.address = address.value();
        }
        if (const auto text = extractJsonStringField(instruction_message, "instruction"); text.has_value()) {
            instruction.instruction = text.value();
        }
        if (const auto bytes = extractJsonStringField(instruction_message, "instructionBytes"); bytes.has_value()) {
            instruction.instruction_bytes = bytes.value();
        }

        if (!instruction.address.empty() || !instruction.instruction.empty()) {
            response.instructions.push_back(std::move(instruction));
        }
    }

    return response;
}

std::string CDapDebugSession::buildLaunchRequestMessage(int sequence_number, const SDapLaunchRequest& launch_request) {
    std::string arguments_json = "[";
    for (std::size_t i = 0; i < launch_request.arguments.size(); ++i) {
        if (i > 0) {
            arguments_json += ",";
        }
        arguments_json += "\"" + launch_request.arguments[i] + "\"";
    }
    arguments_json += "]";

    return "{\"seq\":" + std::to_string(sequence_number) + R"(,"type":"request","command":"launch","arguments":{"program":")" + launch_request.program + R"(","args":)" +
        arguments_json + R"(,"cwd":")" + launch_request.working_directory + R"(","stopOnEntry":)" + std::string(launch_request.stop_on_entry ? "true" : "false") +
        R"(,"terminal":"console"}})";
}

std::string CDapDebugSession::buildAttachRequestMessage(int sequence_number, const SDapAttachRequest& attach_request) {
    return "{\"seq\":" + std::to_string(sequence_number) + R"(,"type":"request","command":"attach","arguments":{"pid":)" + std::to_string(attach_request.process_id) +
        ",\"stopOnEntry\":" + std::string(attach_request.stop_on_entry ? "true" : "false") + "}}";
}

std::string CDapDebugSession::buildConfigurationDoneRequestMessage(int sequence_number) {
    return "{\"seq\":" + std::to_string(sequence_number) + R"(,"type":"request","command":"configurationDone","arguments":{}})";
}

SDapProtocolMessage CDapDebugSession::parseProtocolMessage(const std::string& response_message) {
    SDapProtocolMessage message = {};

    if (response_message.find(R"("type":"event")") != std::string::npos) {
        message.type = "event";
        if (const auto event_name = extractJsonStringField(response_message, "event"); event_name.has_value()) {
            message.event_name = event_name.value();
        }
        return message;
    }

    if (response_message.find(R"("type":"response")") != std::string::npos) {
        message.type = "response";
        if (const auto command_name = extractJsonStringField(response_message, "command"); command_name.has_value()) {
            message.command_name = command_name.value();
        }
        message.success = response_message.find("\"success\":true") != std::string::npos;
        return message;
    }

    return message;
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
    next_sequence_number_ = 2;
    last_error_.clear();
    return true;
}

bool CDapDebugSession::launch(const SDapLaunchRequest& launch_request) {
    if (!isConnected()) {
        last_error_ = "DAP session is not connected";
        return false;
    }

    std::string error_message;
    const auto  request_message = buildLaunchRequestMessage(next_sequence_number_++, launch_request);

    if (!transport_->sendMessage(request_message, error_message)) {
        last_error_ = error_message;
        return false;
    }

    while (true) {
        std::string response_message;
        if (!transport_->readMessage(response_message, error_message)) {
            last_error_ = error_message;
            return false;
        }

        logDapMessage("dap launch message: ", response_message);
        const auto message = parseProtocolMessage(response_message);
        if (message.type == "event" && message.event_name == "initialized") {
            last_error_.clear();
            return true;
        }
    }
}

bool CDapDebugSession::attach(const SDapAttachRequest& attach_request) {
    if (!isConnected()) {
        last_error_ = "DAP session is not connected";
        return false;
    }

    std::string error_message;
    const auto  request_message = buildAttachRequestMessage(next_sequence_number_++, attach_request);

    if (!transport_->sendMessage(request_message, error_message)) {
        last_error_ = error_message;
        return false;
    }

    while (true) {
        std::string response_message;
        if (!transport_->readMessage(response_message, error_message)) {
            last_error_ = error_message;
            return false;
        }

        logDapMessage("dap attach message: ", response_message);
        const auto message = parseProtocolMessage(response_message);

        if (message.type == "event" && message.event_name == "initialized") {
            last_error_.clear();
            return true;
        }

        if (message.type == "response" && message.command_name == "attach" && !message.success) {
            last_error_ = "DAP attach response did not report success";
            return false;
        }
    }
}

bool CDapDebugSession::configurationDone() {
    if (!isConnected()) {
        last_error_ = "DAP session is not connected";
        return false;
    }

    if (!sendConfigurationDoneRequest()) {
        return false;
    }

    std::string error_message;
    while (true) {
        std::string response_message;
        if (!transport_->readMessage(response_message, error_message)) {
            last_error_ = error_message;
            return false;
        }

        logDapMessage("dap configurationDone message: ", response_message);
        const auto message = parseProtocolMessage(response_message);
        if (message.type == "response" && message.command_name == "launch") {
            if (!message.success) {
                last_error_ = "DAP launch response did not report success";
                return false;
            }
            continue;
        }

        if (message.type == "response" && message.command_name == "configurationDone") {
            if (!message.success) {
                last_error_ = "DAP configurationDone response did not report success";
                return false;
            }

            last_error_.clear();
            return true;
        }
    }
}

bool CDapDebugSession::sendConfigurationDoneRequest() {
    if (!isConnected()) {
        last_error_ = "DAP session is not connected";
        return false;
    }

    std::string error_message;
    const auto  request_message = buildConfigurationDoneRequestMessage(next_sequence_number_++);

    if (!transport_->sendMessage(request_message, error_message)) {
        last_error_ = error_message;
        return false;
    }

    last_error_.clear();
    return true;
}

bool CDapDebugSession::waitForStoppedEvent() {
    if (!isConnected()) {
        last_error_ = "DAP session is not connected";
        return false;
    }

    std::string error_message;
    while (true) {
        std::string response_message;
        if (!transport_->readMessage(response_message, error_message)) {
            last_error_ = error_message;
            return false;
        }

        logDapMessage("dap waitForStoppedEvent message: ", response_message);
        const auto message = parseProtocolMessage(response_message);
        if (message.type == "response" && message.command_name == "launch") {
            if (!message.success) {
                last_error_ = "DAP launch response did not report success";
                return false;
            }
            continue;
        }

        if (message.type == "response" && message.command_name == "configurationDone") {
            if (!message.success) {
                last_error_ = "DAP configurationDone response did not report success";
                return false;
            }
            continue;
        }

        if (message.type == "event" && message.event_name == "stopped") {
            last_error_.clear();
            return true;
        }
    }
}

SDapThreadsResponse CDapDebugSession::getThreads() {
    if (!isConnected()) {
        return {
            .success       = false,
            .threads       = {},
            .error_message = "DAP session is not connected",
        };
    }

    std::string error_message;
    const auto  request_message = buildThreadsRequestMessage(next_sequence_number_++);

    if (!transport_->sendMessage(request_message, error_message)) {
        return {
            .success       = false,
            .threads       = {},
            .error_message = error_message,
        };
    }

    while (true) {
        std::string response_message;
        if (!transport_->readMessage(response_message, error_message)) {
            return {
                .success       = false,
                .threads       = {},
                .error_message = error_message,
            };
        }

        logDapMessage("dap threads message: ", response_message);
        const auto message = parseProtocolMessage(response_message);

        if (message.type == "event") {
            continue;
        }

        if (message.type == "response" && message.command_name == "threads") {
            return parseThreadsResponseMessage(response_message);
        }
    }
}

SDapStackTraceResponse CDapDebugSession::getStackTrace(const SDapStackTraceRequest& stack_trace_request) {
    if (!isConnected()) {
        return {
            .success       = false,
            .stack_frames  = {},
            .error_message = "DAP session is not connected",
        };
    }

    std::string error_message;
    const auto  request_message = buildStackTraceRequestMessage(next_sequence_number_++, stack_trace_request);

    if (!transport_->sendMessage(request_message, error_message)) {
        return {
            .success       = false,
            .stack_frames  = {},
            .error_message = error_message,
        };
    }

    while (true) {
        std::string response_message;
        if (!transport_->readMessage(response_message, error_message)) {
            return {
                .success       = false,
                .stack_frames  = {},
                .error_message = error_message,
            };
        }

        logDapMessage("dap stackTrace message: ", response_message);
        const auto message = parseProtocolMessage(response_message);

        if (message.type == "event") {
            continue;
        }

        if (message.type == "response" && message.command_name == "stackTrace") {
            return parseStackTraceResponseMessage(response_message);
        }
    }
}

SDapScopesResponse CDapDebugSession::getScopes(const SDapScopesRequest& scopes_request) {
    if (!isConnected()) {
        return {
            .success       = false,
            .scopes        = {},
            .error_message = "DAP session is not connected",
        };
    }

    std::string error_message;
    const auto  request_message = buildScopesRequestMessage(next_sequence_number_++, scopes_request);

    if (!transport_->sendMessage(request_message, error_message)) {
        return {
            .success       = false,
            .scopes        = {},
            .error_message = error_message,
        };
    }

    while (true) {
        std::string response_message;
        if (!transport_->readMessage(response_message, error_message)) {
            return {
                .success       = false,
                .scopes        = {},
                .error_message = error_message,
            };
        }

        logDapMessage("dap scopes message: ", response_message);
        const auto message = parseProtocolMessage(response_message);

        if (message.type == "event") {
            continue;
        }

        if (message.type == "response" && message.command_name == "scopes") {
            return parseScopesResponseMessage(response_message);
        }
    }
}

SDapVariablesResponse CDapDebugSession::getVariables(const SDapVariablesRequest& variables_request) {
    if (!isConnected()) {
        return {
            .success       = false,
            .variables     = {},
            .error_message = "DAP session is not connected",
        };
    }

    std::string error_message;
    const auto  request_message = buildVariablesRequestMessage(next_sequence_number_++, variables_request);

    if (!transport_->sendMessage(request_message, error_message)) {
        return {
            .success       = false,
            .variables     = {},
            .error_message = error_message,
        };
    }

    while (true) {
        std::string response_message;
        if (!transport_->readMessage(response_message, error_message)) {
            return {
                .success       = false,
                .variables     = {},
                .error_message = error_message,
            };
        }

        logDapMessage("dap variables message: ", response_message);
        const auto message = parseProtocolMessage(response_message);

        if (message.type == "event") {
            continue;
        }

        if (message.type == "response" && message.command_name == "variables") {
            return parseVariablesResponseMessage(response_message);
        }
    }
}

SDapContinueResponse CDapDebugSession::continueExecution(const SDapContinueRequest& continue_request) {
    if (!isConnected()) {
        return {
            .success       = false,
            .error_message = "DAP session is not connected",
        };
    }

    std::string error_message;
    const auto  request_message = buildContinueRequestMessage(next_sequence_number_++, continue_request);

    if (!transport_->sendMessage(request_message, error_message)) {
        return {
            .success       = false,
            .error_message = error_message,
        };
    }

    while (true) {
        std::string response_message;
        if (!transport_->readMessage(response_message, error_message)) {
            return {
                .success       = false,
                .error_message = error_message,
            };
        }

        logDapMessage("dap continue message: ", response_message);
        const auto message = parseProtocolMessage(response_message);

        if (message.type == "event") {
            continue;
        }

        if (message.type == "response" && message.command_name == "continue") {
            return parseContinueResponseMessage(response_message);
        }
    }
}

SDapEvaluateResponse CDapDebugSession::evaluate(const SDapEvaluateRequest& evaluate_request) {
    if (!isConnected()) {
        return {
            .success       = false,
            .result        = "",
            .type          = "",
            .error_message = "DAP session is not connected",
        };
    }

    if (!adapter_capabilities_.supports_evaluate) {
        return {
            .success       = false,
            .result        = "",
            .type          = "",
            .error_message = "DAP adapter does not support evaluate",
        };
    }

    std::string error_message;
    const auto  request_message = buildEvaluateRequestMessage(next_sequence_number_++, evaluate_request);

    if (!transport_->sendMessage(request_message, error_message)) {
        return {
            .success       = false,
            .result        = "",
            .type          = "",
            .error_message = error_message,
        };
    }

    while (true) {
        std::string response_message;
        if (!transport_->readMessage(response_message, error_message)) {
            return {
                .success       = false,
                .result        = "",
                .type          = "",
                .error_message = error_message,
            };
        }

        logDapMessage("dap evaluate message: ", response_message);
        const auto message = parseProtocolMessage(response_message);

        if (message.type == "event") {
            continue;
        }

        if (message.type == "response" && (message.command_name == "evaluate" || message.command_name.empty())) {
            return parseEvaluateResponseMessage(response_message);
        }
    }
}

SDapDisassembleResponse CDapDebugSession::disassembleInstructions(const SDapDisassembleRequest& disassemble_request) {
    if (!isConnected()) {
        return {
            .success       = false,
            .instructions  = {},
            .error_message = "DAP session is not connected",
        };
    }

    if (!adapter_capabilities_.supports_disassemble) {
        return {
            .success       = false,
            .instructions  = {},
            .error_message = "DAP adapter does not support disassemble",
        };
    }

    std::string error_message;
    const auto  request_message = buildDisassembleRequestMessage(next_sequence_number_++, disassemble_request);

    if (!transport_->sendMessage(request_message, error_message)) {
        return {
            .success       = false,
            .instructions  = {},
            .error_message = error_message,
        };
    }

    while (true) {
        std::string response_message;
        if (!transport_->readMessage(response_message, error_message)) {
            return {
                .success       = false,
                .instructions  = {},
                .error_message = error_message,
            };
        }

        logDapMessage("dap disassemble message: ", response_message);
        const auto message = parseProtocolMessage(response_message);

        if (message.type == "event") {
            continue;
        }

        if (message.type == "response" && message.command_name == "disassemble") {
            return parseDisassembleResponseMessage(response_message);
        }
    }
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

SDebugCapabilities CDapDebugSession::getCapabilities() {
    return mapCapabilities(adapter_capabilities_);
}

std::vector<SLocalVariable> CDapDebugSession::getLocals(const SDebugSelection& selection) {
    if (!isConnected()) {
        return {};
    }

    const auto stack_trace_response = getStackTrace({
        .thread_id   = static_cast<int>(selection.thread_id),
        .start_frame = 0,
        .levels      = selection.frame_index + 1,
    });

    if (!stack_trace_response.success || stack_trace_response.stack_frames.size() <= selection.frame_index) {
        return {};
    }

    const auto scopes_response = getScopes({
        .frame_id = stack_trace_response.stack_frames[selection.frame_index].id,
    });

    if (!scopes_response.success || scopes_response.scopes.empty()) {
        return {};
    }

    const auto  locals_scope_iterator = std::ranges::find_if(scopes_response.scopes, [](const SDapScope& scope) { return scope.name == "Locals"; });

    const auto& selected_scope = locals_scope_iterator != scopes_response.scopes.end() ? *locals_scope_iterator : scopes_response.scopes.front();
    if (selected_scope.variables_reference == 0) {
        return {};
    }

    const auto variables_response = getVariables({
        .variables_reference = selected_scope.variables_reference,
    });

    if (!variables_response.success) {
        return {};
    }

    std::vector<SLocalVariable> locals;
    locals.reserve(variables_response.variables.size());
    for (const auto& variable : variables_response.variables) {
        locals.push_back({
            .name  = variable.name,
            .value = variable.value,
            .type  = variable.type,
        });
    }

    return locals;
}

SMemoryReadResult CDapDebugSession::readMemory(const SDebugSelection& selection, const SMemoryReadRequest& request) {
    static_cast<void>(selection);

    if (!isConnected()) {
        return {
            .start_address = request.start_address,
            .memory_bytes  = {},
            .bytes_per_row = request.bytes_per_row,
            .error_message = "DAP session is not connected",
        };
    }

    if (!adapter_capabilities_.supports_read_memory) {
        return {
            .start_address = request.start_address,
            .memory_bytes  = {},
            .bytes_per_row = request.bytes_per_row,
            .error_message = "DAP adapter does not support readMemory",
        };
    }

    std::string error_message;
    const auto  request_message = buildReadMemoryRequestMessage(next_sequence_number_++,
                                                                {
                                                                    .memory_reference = formatMemoryReference(request.start_address),
                                                                    .offset           = 0,
                                                                    .count            = request.byte_count,
                                                               });

    if (!transport_->sendMessage(request_message, error_message)) {
        last_error_ = error_message;
        return {
            .start_address = request.start_address,
            .memory_bytes  = {},
            .bytes_per_row = request.bytes_per_row,
            .error_message = error_message,
        };
    }

    while (true) {
        std::string response_message;
        if (!transport_->readMessage(response_message, error_message)) {
            last_error_ = error_message;
            return {
                .start_address = request.start_address,
                .memory_bytes  = {},
                .bytes_per_row = request.bytes_per_row,
                .error_message = error_message,
            };
        }

        logDapMessage("dap readMemory message: ", response_message);
        const auto message = parseProtocolMessage(response_message);
        if (message.type == "event") {
            continue;
        }
        if (!(message.type == "response" && message.command_name == "readMemory")) {
            continue;
        }

        const auto response = parseReadMemoryResponseMessage(response_message);
        if (!response.success) {
            last_error_ = response.error_message;
            return {
                .start_address = request.start_address,
                .memory_bytes  = {},
                .bytes_per_row = request.bytes_per_row,
                .error_message = response.error_message,
            };
        }

        last_error_.clear();
        return {
            .start_address = request.start_address,
            .memory_bytes  = response.memory_bytes,
            .bytes_per_row = request.bytes_per_row,
            .error_message = "",
        };
    }
}

std::vector<SWatchResult> CDapDebugSession::evaluateWatches(const SDebugSelection& selection, const std::vector<SWatchExpression>& watch_expressions) {
    std::vector<SWatchResult> watch_results;
    watch_results.reserve(watch_expressions.size());

    if (!isConnected()) {
        for (const auto& watch_expression : watch_expressions) {
            watch_results.push_back({
                .expression    = watch_expression.expression,
                .value         = "",
                .type          = "",
                .error_message = "DAP session is not connected",
            });
        }

        return watch_results;
    }

    if (!adapter_capabilities_.supports_evaluate) {
        for (const auto& watch_expression : watch_expressions) {
            watch_results.push_back({
                .expression    = watch_expression.expression,
                .value         = "",
                .type          = "",
                .error_message = "DAP adapter does not support evaluate",
            });
        }

        return watch_results;
    }

    const auto stack_trace_response = getStackTrace({
        .thread_id   = static_cast<int>(selection.thread_id),
        .start_frame = 0,
        .levels      = selection.frame_index + 1,
    });

    if (!stack_trace_response.success || stack_trace_response.stack_frames.size() <= selection.frame_index) {
        for (const auto& watch_expression : watch_expressions) {
            watch_results.push_back({
                .expression    = watch_expression.expression,
                .value         = "",
                .type          = "",
                .error_message = stack_trace_response.success ? "DAP stackTrace did not include requested frame" : stack_trace_response.error_message,
            });
        }

        return watch_results;
    }

    const int frame_id = stack_trace_response.stack_frames[selection.frame_index].id;

    for (const auto& watch_expression : watch_expressions) {
        const auto evaluate_response = evaluate({
            .expression = watch_expression.expression,
            .frame_id   = frame_id,
            .context    = "watch",
        });

        watch_results.push_back({
            .expression    = watch_expression.expression,
            .value         = evaluate_response.result,
            .type          = evaluate_response.type,
            .error_message = evaluate_response.success ? "" : evaluate_response.error_message,
        });
    }

    return watch_results;
}

std::vector<SDisassemblyInstruction> CDapDebugSession::disassemble(const SDebugSelection& selection, std::uint64_t start_address, std::size_t instruction_count) {
    static_cast<void>(selection);

    const auto response = disassembleInstructions({
        .memory_reference   = formatMemoryReference(start_address),
        .instruction_offset = 0,
        .instruction_count  = instruction_count,
    });

    if (!response.success) {
        return {};
    }

    std::vector<SDisassemblyInstruction> instructions;
    instructions.reserve(response.instructions.size());

    for (const auto& dap_instruction : response.instructions) {
        SDisassemblyInstruction instruction = {};

        if (!dap_instruction.address.empty()) {
            try {
                instruction.address = parseAddressString(dap_instruction.address);
            } catch (const std::exception&) { instruction.address = start_address; }
        } else {
            instruction.address = start_address;
        }

        const auto operand_separator = dap_instruction.instruction.find_first_of(" \t");
        if (operand_separator == std::string::npos) {
            instruction.mnemonic = dap_instruction.instruction;
        } else {
            instruction.mnemonic      = dap_instruction.instruction.substr(0, operand_separator);
            const auto operands_start = dap_instruction.instruction.find_first_not_of(" \t", operand_separator);
            if (operands_start != std::string::npos) {
                instruction.operands = dap_instruction.instruction.substr(operands_start);
            }
        }

        if (!dap_instruction.instruction_bytes.empty()) {
            instruction.comment = dap_instruction.instruction_bytes;
        }

        instructions.push_back(std::move(instruction));
    }

    return instructions;
}
