#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "debug_session.hpp"
#include "memory_view.hpp"

std::optional<std::uint64_t> findFirstHexAddress(const std::string& text);
SMemoryReadRequest           buildMemoryReadRequest(IDebugSession& debug_session, const SDebugSelection& debug_selection, const std::vector<SLocalVariable>& locals,
                                                    std::size_t selected_local_index, std::uint64_t fallback_address, const std::string& fallback_memory_reference = "",
                                                    std::size_t byte_count = 40,
                                                    std::size_t bytes_per_row = 8);
