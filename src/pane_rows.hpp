#pragma once

#include <string>
#include <vector>

#include "debug_session.hpp"

std::vector<std::string> formatLocalsPaneRows(const std::vector<SLocalVariable>& locals);
std::vector<std::string> formatWatchListPaneRows(const std::vector<SWatchResult>& watch_results);
