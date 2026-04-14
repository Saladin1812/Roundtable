#include <array>
#include <cstddef>
#include <cstdint>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <string>

enum class eFocusPane : std::uint8_t {
    LOCALS,
    MEMORY_VIEW,
    WATCH_LIST,
};

int main() {
    using namespace ftxui;

    auto                       screen                = ScreenInteractive::Fullscreen();
    eFocusPane                 focused_pane          = eFocusPane::MEMORY_VIEW;
    std::size_t                selected_locals_index = 0;
    std::array<std::string, 2> locals_mock_data      = {"a : int", "b : int"};

    auto                       renderer = Renderer([&] {
        auto locals_title = text(" Locals ") | bold;
        if (focused_pane == eFocusPane::LOCALS) {
            locals_title = locals_title | inverted;
        }

        auto memory_view_title = text(" Memory View ") | bold;
        if (focused_pane == eFocusPane::MEMORY_VIEW) {
            memory_view_title = memory_view_title | inverted;
        }
        auto watch_list_title = text(" Watch List ") | bold;
        if (focused_pane == eFocusPane::WATCH_LIST) {
            watch_list_title = watch_list_title | inverted;
        }
        Elements locals_elements = {
            locals_title,
            separator(),
        };
        for (std::size_t i = 0; i < locals_mock_data.size(); i++) {
            auto row = text(locals_mock_data[i]);

            if (focused_pane == eFocusPane::LOCALS && i == selected_locals_index) {
                row = row | inverted;
            }

            locals_elements.push_back(row);
        }
        auto locals = vbox(locals_elements) | border | size(WIDTH, EQUAL, 24);

        auto memory = vbox({
                          memory_view_title,
                          separator(),
                          text("Memory pane coming next"),
                      }) |
            border | flex;

        auto watch_list = vbox({
                              watch_list_title,
                              separator(),
                              text("a"),
                              text("ptr"),
                          }) |
            border | size(WIDTH, EQUAL, 24);

        auto status = hbox({
                          text(" Roundtable ") | inverted,
                          separator(),
                          text(" Press q to quit "),
                      }) |
            border;

        return vbox({
            hbox({
                locals,
                memory,
                watch_list,
            }) | flex,
            status,
        });
    });

    auto                       component = CatchEvent(renderer, [&](Event event) {
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
            if (event == Event::ArrowUp || event == Event::Character('k')) {
                if (selected_locals_index > 0) {
                    selected_locals_index--;
                    return true;
                }
            }
            if (event == Event::ArrowDown || event == Event::Character('j')) {
                if (selected_locals_index + 1 < locals_mock_data.size()) {
                    selected_locals_index++;
                    return true;
                }
            }
        }

        return false;
    });

    screen.Loop(component);
}
