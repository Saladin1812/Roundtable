#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "debug_session.hpp"
#include "memory_view.hpp"

struct SMemoryByteHighlight {
    std::uint64_t start_address = 0;
    std::size_t   start_offset  = 0;
    std::size_t   byte_count    = 0;
    std::size_t   row_stride    = 8;
    bool          synthetic     = false;
};

std::optional<std::uint64_t>            findFirstHexAddress(const std::string& text);
SMemoryReadRequest                      buildMemoryReadRequest(IDebugSession& debug_session, const SDebugSelection& debug_selection, const std::vector<SLocalVariable>& locals,
                                                               std::size_t selected_local_index, std::uint64_t fallback_address, const std::string& fallback_memory_reference = "",
                                                               std::size_t byte_count = 40, std::size_t bytes_per_row = 8);
SMemoryReadRequest                      buildMemoryReadRequest(const std::vector<SWatchResult>& watch_results, std::size_t selected_watch_index, std::uint64_t fallback_address,
                                                               const std::string& fallback_memory_reference = "", std::size_t byte_count = 40, std::size_t bytes_per_row = 8);
std::optional<std::vector<std::string>> buildSyntheticMemoryRows(const std::vector<SLocalVariable>& locals, std::size_t selected_local_index, std::size_t bytes_per_row = 8);
std::optional<std::vector<std::string>> buildSyntheticMemoryRows(const std::vector<SWatchResult>& watch_results, std::size_t selected_watch_index, std::size_t bytes_per_row = 8);
std::optional<SMemoryByteHighlight>     buildMemoryByteHighlight(const std::vector<SLocalVariable>& locals, std::size_t selected_local_index,
                                                                 const SMemoryReadRequest& memory_read_request, bool use_synthetic_rows);
std::optional<SMemoryByteHighlight>     buildMemoryByteHighlight(const std::vector<SWatchResult>& watch_results, std::size_t selected_watch_index,
                                                                 const SMemoryReadRequest& memory_read_request, bool use_synthetic_rows);
SMemoryReadRequest buildContextualMemoryReadRequest(const SMemoryReadRequest& memory_read_request, std::size_t context_rows_before = 2, std::int64_t navigation_byte_offset = 0);
