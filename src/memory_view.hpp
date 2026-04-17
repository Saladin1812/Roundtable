#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

std::vector<std::string> generateMemoryViewRows(std::uint64_t start_address, const std::vector<std::uint8_t>& memory_bytes, std::size_t bytes_per_row = 8);
