#pragma once

#include <cstddef>
#include <cstdint>
#include <ftxui/component/event.hpp>
#include <string>
#include <vector>

enum class eFocusPane : std::uint8_t {
    LOCALS,
    MEMORY_VIEW,
    WATCH_LIST,
};

struct SSelectablePaneState {
    std::string              title;
    std::vector<std::string> rows;
    std::size_t              selected_index = 0;
};

bool handleVerticalNavigation(ftxui::Event event, SSelectablePaneState& pane);
eFocusPane advanceFocusPane(eFocusPane focused_pane);
