#include "memory_selection.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <iomanip>
#include <sstream>

namespace {

    std::optional<std::vector<std::uint8_t>> parseQuotedBytes(const std::string& text) {
        const auto quote_start = text.find('"');
        if (quote_start == std::string::npos) {
            return std::nullopt;
        }

        std::vector<std::uint8_t> bytes;

        for (std::size_t index = quote_start + 1; index < text.size(); ++index) {
            const char current_character = text[index];

            if (current_character == '\\') {
                if (index + 1 >= text.size()) {
                    break;
                }

                const char escaped_character = text[++index];
                switch (escaped_character) {
                    case '0': bytes.push_back(0x00); break;
                    case 'n': bytes.push_back(static_cast<std::uint8_t>('\n')); break;
                    case 'r': bytes.push_back(static_cast<std::uint8_t>('\r')); break;
                    case 't': bytes.push_back(static_cast<std::uint8_t>('\t')); break;
                    case '\\': bytes.push_back(static_cast<std::uint8_t>('\\')); break;
                    case '"': bytes.push_back(static_cast<std::uint8_t>('"')); break;
                    default: bytes.push_back(static_cast<std::uint8_t>(escaped_character)); break;
                }
                continue;
            }

            if (current_character == '"') {
                return bytes.empty() ? std::nullopt : std::optional<std::vector<std::uint8_t>>(bytes);
            }

            bytes.push_back(static_cast<std::uint8_t>(current_character));
        }

        return std::nullopt;
    }

    std::vector<std::string> formatSyntheticMemoryRows(const std::vector<std::uint8_t>& memory_bytes, std::size_t bytes_per_row) {
        std::vector<std::string> rows;
        if (bytes_per_row == 0 || memory_bytes.empty()) {
            return rows;
        }

        rows.reserve((memory_bytes.size() + bytes_per_row - 1) / bytes_per_row);

        for (std::size_t offset = 0; offset < memory_bytes.size(); offset += bytes_per_row) {
            const std::size_t  row_size = std::min(bytes_per_row, memory_bytes.size() - offset);
            std::ostringstream row_stream;
            row_stream << "<value>  ";

            for (std::size_t index = 0; index < row_size; ++index) {
                row_stream << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(memory_bytes[offset + index]);
                if (index + 1 < row_size) {
                    row_stream << ' ';
                }
            }

            row_stream << "  ";
            for (std::size_t index = 0; index < row_size; ++index) {
                const auto byte = static_cast<unsigned char>(memory_bytes[offset + index]);
                row_stream << (std::isprint(byte) != 0 ? static_cast<char>(byte) : '.');
            }

            rows.push_back(row_stream.str());
        }

        return rows;
    }

    template <typename TItem>
    std::optional<std::vector<std::string>> buildSyntheticMemoryRowsFromItems(const std::vector<TItem>& items, std::size_t selected_index, std::size_t bytes_per_row) {
        if (items.empty()) {
            return std::nullopt;
        }

        const auto& selected_item = items[std::min(selected_index, items.size() - 1)];
        const bool  is_array_like = selected_item.type.find("array") != std::string::npos || selected_item.value.find('{') != std::string::npos;
        if (!is_array_like) {
            return std::nullopt;
        }

        const auto parsed_bytes = parseQuotedBytes(selected_item.value);
        if (!parsed_bytes.has_value()) {
            return std::nullopt;
        }

        return formatSyntheticMemoryRows(parsed_bytes.value(), bytes_per_row);
    }

} // namespace

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
            std::uint64_t parsed_address = 0;
            const auto    parse_result   = std::from_chars(text.data() + address_start + 2, text.data() + address_end, parsed_address, 16);
            if (parse_result.ec == std::errc{}) {
                return parsed_address;
            }
        }

        search_position = address_start + 2;
    }
}

SMemoryReadRequest buildMemoryReadRequest(IDebugSession& debug_session, const SDebugSelection& debug_selection, const std::vector<SLocalVariable>& locals,
                                          std::size_t selected_local_index, std::uint64_t fallback_address, const std::string& fallback_memory_reference, std::size_t byte_count,
                                          std::size_t bytes_per_row) {
    std::uint64_t start_address    = fallback_address;
    std::string   memory_reference = fallback_memory_reference;

    if (!locals.empty()) {
        const auto& selected_local = locals[std::min(selected_local_index, locals.size() - 1)];

        if (const auto local_memory_reference_address = findFirstHexAddress(selected_local.memory_reference); local_memory_reference_address.has_value()) {
            start_address    = local_memory_reference_address.value();
            memory_reference = selected_local.memory_reference;
        }

        if (selected_local.type.find('*') != std::string::npos) {
            if (const auto pointer_address = findFirstHexAddress(selected_local.value); pointer_address.has_value()) {
                start_address = pointer_address.value();
            }
        }

        const bool                    is_pointer_like   = selected_local.type.find('*') != std::string::npos;
        const bool                    is_std_array_like = selected_local.type.find("std::array") != std::string::npos;
        const bool                    is_array_like     = selected_local.type.find("array") != std::string::npos || selected_local.value.find('{') != std::string::npos;

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
                start_address    = memory_reference_address.value();
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

SMemoryReadRequest buildMemoryReadRequest(const std::vector<SWatchResult>& watch_results, std::size_t selected_watch_index, std::uint64_t fallback_address,
                                          const std::string& fallback_memory_reference, std::size_t byte_count, std::size_t bytes_per_row) {
    std::uint64_t start_address    = fallback_address;
    std::string   memory_reference = fallback_memory_reference;

    if (!watch_results.empty()) {
        const auto& selected_watch = watch_results[std::min(selected_watch_index, watch_results.size() - 1)];

        if (const auto watch_memory_reference_address = findFirstHexAddress(selected_watch.memory_reference); watch_memory_reference_address.has_value()) {
            start_address    = watch_memory_reference_address.value();
            memory_reference = selected_watch.memory_reference;
        } else if (const auto watch_value_address = findFirstHexAddress(selected_watch.value); watch_value_address.has_value()) {
            start_address = watch_value_address.value();
            memory_reference.clear();
        }
    }

    return {
        .start_address    = start_address,
        .memory_reference = memory_reference,
        .byte_count       = byte_count,
        .bytes_per_row    = bytes_per_row,
    };
}

std::optional<std::vector<std::string>> buildSyntheticMemoryRows(const std::vector<SLocalVariable>& locals, std::size_t selected_local_index, std::size_t bytes_per_row) {
    return buildSyntheticMemoryRowsFromItems(locals, selected_local_index, bytes_per_row);
}

std::optional<std::vector<std::string>> buildSyntheticMemoryRows(const std::vector<SWatchResult>& watch_results, std::size_t selected_watch_index, std::size_t bytes_per_row) {
    return buildSyntheticMemoryRowsFromItems(watch_results, selected_watch_index, bytes_per_row);
}
