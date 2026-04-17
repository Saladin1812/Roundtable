#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "memory_view.hpp"
#include "pane_state.hpp"

static ftxui::Element renderSelectablePane(const SSelectablePaneState& pane, bool is_focused) {
    using namespace ftxui;
    Elements rows;
    Element  title = text(pane.title) | bold;
    if (is_focused) {
        title = title | inverted;
    }
    rows.push_back(title);
    rows.push_back(separator());

    for (std::size_t i = 0; i < pane.rows.size(); ++i) {

        Element data_row = text(pane.rows[i]);
        if (is_focused && pane.selected_index == i) {
            rows.push_back(data_row | inverted);
        } else {
            rows.push_back(data_row);
        }
    }

    return vbox(rows) | border;
}
int main() {
    using namespace ftxui;

    auto                 screen       = ScreenInteractive::Fullscreen();
    eFocusPane           focused_pane = eFocusPane::MEMORY_VIEW;

    SSelectablePaneState locals_pane = {
        .title = " Locals ",
        .rows  = {"a : int", "b : int"},
    };
    SSelectablePaneState memory_view_pane = {
        .title = " Memory View ",
        .rows  = generateMemoryViewRows(0x1000,
                                        {
                                           0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x57, 0x6F, 0x72, 0x6C, 0x64, 0x21, 0x00, 0x41, 0x42, 0x43, 0xDE, 0xAD, 0xBE, 0xEF,
                                           0x10, 0x20, 0x30, 0x40, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
                                       }),
    };
    SSelectablePaneState watch_list_pane = {
        .title = " Watch List ",
        .rows  = {"a", "ptr"},
    };

    auto renderer = Renderer([&] {
        Element locals      = renderSelectablePane(locals_pane, focused_pane == eFocusPane::LOCALS);
        Element memory_view = renderSelectablePane(memory_view_pane, focused_pane == eFocusPane::MEMORY_VIEW);
        Element watch_list  = renderSelectablePane(watch_list_pane, focused_pane == eFocusPane::WATCH_LIST);
        Element status      = hbox({
                             text(" Roundtable ") | inverted,
                             separator(),
                             text(" Press q to quit "),
                         }) |
            border;

        return vbox({
            hbox({
                locals | size(WIDTH, EQUAL, 24),
                memory_view | flex,
                watch_list | size(WIDTH, EQUAL, 24),
            }) | flex,
            status,
        });
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Character('q')) {
            screen.Exit();
            return true;
        }

        if (event == Event::Tab) {
            focused_pane = advanceFocusPane(focused_pane);
            return true;
        }

        if (focused_pane == eFocusPane::LOCALS) {
            return handleVerticalNavigation(event, locals_pane);
        }
        if (focused_pane == eFocusPane::WATCH_LIST) {
            return handleVerticalNavigation(event, watch_list_pane);
        }
        if (focused_pane == eFocusPane::MEMORY_VIEW) {
            return handleVerticalNavigation(event, memory_view_pane);
        }
        return false;
    });

    screen.Loop(component);
}
