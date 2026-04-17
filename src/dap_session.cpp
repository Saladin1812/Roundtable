#include "dap_session.hpp"

#include <string>

bool CStdioDapTransport::connect(const SDapEndpointConfig& endpoint_config, std::string& error_message) {
    static_cast<void>(endpoint_config);
    error_message = "DAP stdio transport is not implemented yet";
    connected_    = false;
    return false;
}

bool CStdioDapTransport::sendMessage(const std::string& message, std::string& error_message) {
    static_cast<void>(message);
    error_message = "DAP stdio transport is not implemented yet";
    return false;
}

bool CStdioDapTransport::readMessage(std::string& message, std::string& error_message) {
    message.clear();
    error_message = "DAP stdio transport is not implemented yet";
    return false;
}

bool CStdioDapTransport::isConnected() const {
    return connected_;
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
    return "{\"command\":\"initialize\",\"arguments\":{\"clientID\":\"" + initialize_request.client_id + "\",\"clientName\":\"" + initialize_request.client_name + "\"}}";
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
