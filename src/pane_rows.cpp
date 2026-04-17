#include "pane_rows.hpp"

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
            rows.push_back(watch_result.expression + " : " + watch_result.error_message);
            continue;
        }

        rows.push_back(watch_result.expression + " = " + watch_result.value + " : " + watch_result.type);
    }

    return rows;
}
