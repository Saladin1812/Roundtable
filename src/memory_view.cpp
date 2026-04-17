#include "memory_view.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

SMemoryReadResult CMockMemoryDataProvider::readMemory(const SMemoryReadRequest& request) const {
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

std::vector<std::string> generateMemoryViewRows(std::uint64_t start_address, const std::vector<std::uint8_t>& memory_bytes, std::size_t bytes_per_row) {
    std::vector<std::string> rows;

    if (bytes_per_row == 0) {
        return rows;
    }

    rows.reserve((memory_bytes.size() + bytes_per_row - 1) / bytes_per_row);

    for (std::size_t offset = 0; offset < memory_bytes.size(); offset += bytes_per_row) {
        const std::size_t  row_size = std::min(bytes_per_row, memory_bytes.size() - offset);

        std::ostringstream row_stream;
        row_stream << "0x" << std::uppercase << std::hex << (start_address + offset) << "  ";

        for (std::size_t i = 0; i < row_size; ++i) {
            row_stream << std::setw(2) << std::setfill('0') << static_cast<int>(memory_bytes[offset + i]);
            if (i + 1 < row_size) {
                row_stream << ' ';
            }
        }

        row_stream << "  ";

        for (std::size_t i = 0; i < row_size; ++i) {
            const auto byte_value = memory_bytes[offset + i];
            const auto character  = static_cast<unsigned char>(byte_value);
            row_stream << (std::isprint(character) != 0 ? static_cast<char>(character) : '.');
        }

        rows.push_back(row_stream.str());
    }

    return rows;
}

std::vector<std::string> generateMemoryViewRows(const SMemoryReadResult& memory_read_result) {
    if (!memory_read_result.error_message.empty()) {
        return {memory_read_result.error_message};
    }

    return generateMemoryViewRows(memory_read_result.start_address, memory_read_result.memory_bytes, memory_read_result.bytes_per_row);
}
