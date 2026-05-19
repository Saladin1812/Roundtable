#include "pane_rows.hpp"

#include <optional>
#include <sstream>

namespace {

    std::optional<std::string> formatScopedWatchError(const std::string& error_message) {
        constexpr std::string_view prefix           = "not available in frame #";
        constexpr std::string_view thread_marker    = " T:";
        constexpr std::string_view detail_separator = ": ";

        if (!error_message.starts_with(prefix)) {
            return std::nullopt;
        }

        const auto frame_start  = prefix.size();
        const auto thread_start = error_message.find(thread_marker, frame_start);
        if (thread_start == std::string::npos) {
            return std::nullopt;
        }

        const auto detail_start = error_message.find(detail_separator, thread_start + thread_marker.size());
        if (detail_start == std::string::npos) {
            return std::nullopt;
        }

        const std::string frame_index = error_message.substr(frame_start, thread_start - frame_start);
        const std::string thread_id   = error_message.substr(thread_start + thread_marker.size(), detail_start - thread_start - thread_marker.size());
        if (frame_index.empty() || thread_id.empty()) {
            return std::nullopt;
        }

        std::string formatted_error = "unavailable in F:";
        formatted_error += frame_index;
        formatted_error += " T:";
        formatted_error += thread_id;
        return formatted_error;
    }

    std::string formatWatchError(const std::string& error_message) {
        if (const auto scoped_error = formatScopedWatchError(error_message); scoped_error.has_value()) {
            return scoped_error.value();
        }

        return error_message;
    }

} // namespace

std::vector<std::string> formatLocalsPaneRows(const std::vector<SLocalVariable>& locals) {
    std::vector<std::string> rows;
    rows.reserve(locals.size());

    for (const auto& local : locals) {
        rows.push_back(local.name + " : " + local.type + " = " + local.value);
    }

    return rows;
}

std::vector<std::string> formatWatchListPaneRows(const std::vector<SWatchResult>& watch_results) {
    std::vector<std::string> rows;
    rows.reserve(watch_results.size());

    for (const auto& watch_result : watch_results) {
        if (!watch_result.error_message.empty()) {
            rows.push_back(watch_result.expression + " ! " + formatWatchError(watch_result.error_message));
            continue;
        }

        rows.push_back(watch_result.expression + " = " + watch_result.value + " : " + watch_result.type);
    }

    return rows;
}

std::vector<std::string> formatDisassemblyPaneRows(const std::vector<SDisassemblyInstruction>& instructions) {
    std::vector<std::string> rows;
    rows.reserve(instructions.size());

    for (const auto& instruction : instructions) {
        std::ostringstream row_stream;
        row_stream << "0x" << std::hex << std::uppercase << instruction.address << "  " << instruction.mnemonic;

        if (!instruction.operands.empty()) {
            row_stream << " " << instruction.operands;
        }

        if (!instruction.comment.empty()) {
            row_stream << " ; " << instruction.comment;
        }

        rows.push_back(row_stream.str());
    }

    return rows;
}
