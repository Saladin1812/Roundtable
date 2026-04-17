#include <cstddef>
#include <cstdint>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
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
static bool handleVerticalNavigation(ftxui::Event event, SSelectablePaneState& pane) {
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
        .rows =
            {
                "0x1000  48 65 6C 6C 6F 20 57 6F  Hello Wo",
                "0x1008  72 6C 64 21 00 41 42 43  rld!.ABC",
                "0x1010  DE AD BE EF 10 20 30 40  .... 0@",
                "0x1018  01 02 03 04 05 06 07 08  ........",
                "0x1020  FF EE DD CC BB AA 99 88  ........",
            },
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
            switch (focused_pane) {
                case eFocusPane::MEMORY_VIEW: focused_pane = eFocusPane::WATCH_LIST; break;
                case eFocusPane::WATCH_LIST: focused_pane = eFocusPane::LOCALS; break;
                case eFocusPane::LOCALS: focused_pane = eFocusPane::MEMORY_VIEW; break;
            }
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
