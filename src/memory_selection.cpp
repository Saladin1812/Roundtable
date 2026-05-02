#include "memory_selection.hpp"

#include <algorithm>
#include <cctype>

std::optional<std::uint64_t> findFirstHexAddress(const std::string& text) {
    std::size_t search_position = 0;

    while (true) {
        const auto address_start = text.find("0x", search_position);
        if (address_start == std::string::npos) {
            return std::nullopt;
        }

        std::size_t address_end = address_start + 2;
        while (address_end < text.size() && std::isxdigit(static_cast<unsigned char>(text[address_end])) != 0) {
            ++address_end;
        }

        if (address_end > address_start + 2) {
            try {
                return std::stoull(text.substr(address_start, address_end - address_start), nullptr, 0);
            } catch (const std::exception&) {
            }
        }

        search_position = address_start + 2;
    }
}

SMemoryReadRequest buildMemoryReadRequest(IDebugSession& debug_session, const SDebugSelection& debug_selection, const std::vector<SLocalVariable>& locals,
                                          std::size_t selected_local_index, std::uint64_t fallback_address, const std::string& fallback_memory_reference,
                                          std::size_t byte_count, std::size_t bytes_per_row) {
    std::uint64_t start_address = fallback_address;
    std::string   memory_reference = fallback_memory_reference;

    if (!locals.empty()) {
        const auto& selected_local = locals[std::min(selected_local_index, locals.size() - 1)];

        if (selected_local.type.find('*') != std::string::npos) {
            if (const auto pointer_address = findFirstHexAddress(selected_local.value); pointer_address.has_value()) {
                start_address = pointer_address.value();
            }
        }

        const bool is_pointer_like   = selected_local.type.find('*') != std::string::npos;
        const bool is_std_array_like = selected_local.type.find("std::array") != std::string::npos;
        const bool is_array_like     = selected_local.type.find("array") != std::string::npos || selected_local.value.find('{') != std::string::npos;

        std::vector<SWatchExpression> address_expressions;
        if (is_pointer_like) {
            address_expressions.push_back({
                .expression = selected_local.name,
            });
        }
        if (is_array_like) {
            if (is_std_array_like) {
                address_expressions.push_back({
                    .expression = "&" + selected_local.name + "._M_elems[0]",
                });
            } else {
                address_expressions.push_back({
                    .expression = selected_local.name + ".data()",
                });
                address_expressions.push_back({
                    .expression = "&" + selected_local.name + "[0]",
                });
            }
        }
        address_expressions.push_back({
            .expression = "&" + selected_local.name,
        });

        const auto address_results = debug_session.evaluateWatches(debug_selection, address_expressions);
        for (const auto& address_result : address_results) {
            if (!address_result.error_message.empty()) {
                continue;
            }

            if (const auto memory_reference_address = findFirstHexAddress(address_result.memory_reference); memory_reference_address.has_value()) {
                start_address = memory_reference_address.value();
                memory_reference = address_result.memory_reference;
                break;
            }

            if (const auto evaluated_address = findFirstHexAddress(address_result.value); evaluated_address.has_value()) {
                start_address = evaluated_address.value();
                memory_reference.clear();
                break;
            }
        }
    }

    return {
        .start_address    = start_address,
        .memory_reference = memory_reference,
        .byte_count       = byte_count,
        .bytes_per_row    = bytes_per_row,
    };
}
