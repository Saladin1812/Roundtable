#include <cstdint>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>

enum class eFocusPane : std::uint8_t {
    LOCALS,
    MEMORY_VIEW,
    WATCH_LIST,
};

int main() {
    using namespace ftxui;

    auto       screen       = ScreenInteractive::Fullscreen();
    eFocusPane focused_pane = eFocusPane::MEMORY_VIEW;

    auto       renderer = Renderer([&] {
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
        auto locals = vbox({
                          locals_title,
                          separator(),
                          text("a : int"),
                          text("b : int"),
                      }) |
            border | size(WIDTH, EQUAL, 24);

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

    auto       component = CatchEvent(renderer, [&](Event event) {
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
        return false;
    });

    screen.Loop(component);
}
