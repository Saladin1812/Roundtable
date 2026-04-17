#include "pane_state.hpp"

bool handleVerticalNavigation(ftxui::Event event, SSelectablePaneState& pane) {
    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k')) {
        if (pane.selected_index > 0) {
            --pane.selected_index;
        }
        return true;
    }

    if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j')) {
        if (pane.selected_index + 1 < pane.rows.size()) {
            ++pane.selected_index;
        }
        return true;
    }

    return false;
}

eFocusPane advanceFocusPane(eFocusPane focused_pane) {
    switch (focused_pane) {
        case eFocusPane::MEMORY_VIEW: return eFocusPane::WATCH_LIST;
        case eFocusPane::WATCH_LIST: return eFocusPane::LOCALS;
        case eFocusPane::LOCALS: return eFocusPane::MEMORY_VIEW;
    }

    return eFocusPane::MEMORY_VIEW;
}
