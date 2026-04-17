#include <catch2/catch_test_macros.hpp>
#include <ftxui/component/event.hpp>

#include "pane_state.hpp"

TEST_CASE("advanceFocusPane cycles through each pane in order") {
    CHECK(advanceFocusPane(eFocusPane::MEMORY_VIEW) == eFocusPane::WATCH_LIST);
    CHECK(advanceFocusPane(eFocusPane::WATCH_LIST) == eFocusPane::LOCALS);
    CHECK(advanceFocusPane(eFocusPane::LOCALS) == eFocusPane::MEMORY_VIEW);
}

TEST_CASE("handleVerticalNavigation moves selection down within bounds") {
    SSelectablePaneState pane{
        .title = " Locals ",
        .rows  = {"a", "b", "c"},
    };

    CHECK(handleVerticalNavigation(ftxui::Event::ArrowDown, pane));
    CHECK(pane.selected_index == 1);

    CHECK(handleVerticalNavigation(ftxui::Event::Character('j'), pane));
    CHECK(pane.selected_index == 2);

    CHECK(handleVerticalNavigation(ftxui::Event::ArrowDown, pane));
    CHECK(pane.selected_index == 2);
}

TEST_CASE("handleVerticalNavigation moves selection up within bounds") {
    SSelectablePaneState pane{
        .title          = " Watch List ",
        .rows           = {"ptr", "value", "result"},
        .selected_index = 2,
    };

    CHECK(handleVerticalNavigation(ftxui::Event::ArrowUp, pane));
    CHECK(pane.selected_index == 1);

    CHECK(handleVerticalNavigation(ftxui::Event::Character('k'), pane));
    CHECK(pane.selected_index == 0);

    CHECK(handleVerticalNavigation(ftxui::Event::ArrowUp, pane));
    CHECK(pane.selected_index == 0);
}

TEST_CASE("handleVerticalNavigation ignores unrelated input") {
    SSelectablePaneState pane{
        .title          = " Memory View ",
        .rows           = {"0x1000"},
        .selected_index = 0,
    };

    CHECK_FALSE(handleVerticalNavigation(ftxui::Event::Tab, pane));
    CHECK(pane.selected_index == 0);
}
