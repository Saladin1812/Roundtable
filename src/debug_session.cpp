#include "debug_session.hpp"

#include <algorithm>
#include <vector>

SDebugCapabilities CMockDebugSession::getCapabilities() const {
    return {
        .supports_memory_read       = true,
        .supports_memory_write      = false,
        .supports_watch_expressions = false,
        .supports_disassembly       = false,
        .supports_data_breakpoints  = false,
    };
}

SMemoryReadResult CMockDebugSession::readMemory(const SDebugSelection& selection, const SMemoryReadRequest& request) const {
    static_cast<void>(selection);

    static const std::vector<std::uint8_t> mock_memory_bytes = {
        0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x57, 0x6F, 0x72, 0x6C, 0x64, 0x21, 0x00, 0x41, 0x42, 0x43, 0xDE, 0xAD, 0xBE, 0xEF,
        0x10, 0x20, 0x30, 0x40, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
    };

    const std::size_t clamped_byte_count = std::min(request.byte_count, mock_memory_bytes.size());

    return {
        .start_address = request.start_address,
        .memory_bytes  = std::vector<std::uint8_t>(mock_memory_bytes.begin(), mock_memory_bytes.begin() + clamped_byte_count),
        .bytes_per_row = request.bytes_per_row,
        .error_message = "",
    };
}
