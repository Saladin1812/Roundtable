#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct SMemoryReadRequest {
    std::uint64_t start_address = 0;
    std::size_t   byte_count    = 0;
    std::size_t   bytes_per_row = 8;
};

struct SMemoryReadResult {
    std::uint64_t             start_address = 0;
    std::vector<std::uint8_t> memory_bytes;
    std::size_t               bytes_per_row = 8;
    std::string               error_message;
};

std::vector<std::string> generateMemoryViewRows(std::uint64_t start_address, const std::vector<std::uint8_t>& memory_bytes, std::size_t bytes_per_row = 8);
std::vector<std::string> generateMemoryViewRows(const SMemoryReadResult& memory_read_result);
