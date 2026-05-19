#include "pane_rows.hpp"

#include <sstream>

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
            rows.push_back(watch_result.expression + " ! " + watch_result.error_message);
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
