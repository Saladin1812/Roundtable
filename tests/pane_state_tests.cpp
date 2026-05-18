#include <catch2/catch_test_macros.hpp>
#include <ftxui/component/event.hpp>

#include "pane_state.hpp"

TEST_CASE("advanceFocusPane cycles through each pane in order") {
    const SViewVisibilityState view_visibility = {
        .show_memory_view      = true,
        .show_disassembly_view = true,
    };

    CHECK(advanceFocusPane(eFocusPane::MEMORY_VIEW, view_visibility) == eFocusPane::DISASSEMBLY_VIEW);
    CHECK(advanceFocusPane(eFocusPane::DISASSEMBLY_VIEW, view_visibility) == eFocusPane::WATCH_LIST);
    CHECK(advanceFocusPane(eFocusPane::WATCH_LIST, view_visibility) == eFocusPane::LOCALS);
    CHECK(advanceFocusPane(eFocusPane::LOCALS, view_visibility) == eFocusPane::MEMORY_VIEW);
}

TEST_CASE("advanceFocusPane skips hidden center panes") {
    const SViewVisibilityState view_visibility = {
        .show_memory_view      = false,
        .show_disassembly_view = true,
    };

    CHECK(advanceFocusPane(eFocusPane::LOCALS, view_visibility) == eFocusPane::DISASSEMBLY_VIEW);
    CHECK(advanceFocusPane(eFocusPane::DISASSEMBLY_VIEW, view_visibility) == eFocusPane::WATCH_LIST);
}

TEST_CASE("executeCommand toggles views and normalizes focus") {
    eFocusPane           focused_pane    = eFocusPane::MEMORY_VIEW;
    SViewVisibilityState view_visibility = {
        .show_memory_view      = true,
        .show_disassembly_view = false,
    };

    executeCommand(eCommand::TOGGLE_MEMORY, focused_pane, view_visibility);
    CHECK_FALSE(view_visibility.show_memory_view);
    CHECK(focused_pane == eFocusPane::LOCALS);

    executeCommand(eCommand::FOCUS_DISASSEMBLY, focused_pane, view_visibility);
    CHECK(view_visibility.show_disassembly_view);
    CHECK(focused_pane == eFocusPane::DISASSEMBLY_VIEW);
}

TEST_CASE("parseCommandName returns commands for known names") {
    REQUIRE(parseCommandName("focus_memory").has_value());
    CHECK(parseCommandName("focus_memory").value() == eCommand::FOCUS_MEMORY);
    REQUIRE(parseCommandName("add_watch").has_value());
    CHECK(parseCommandName("add_watch").value() == eCommand::ADD_WATCH);
    REQUIRE(parseCommandName("edit_watch").has_value());
    CHECK(parseCommandName("edit_watch").value() == eCommand::EDIT_WATCH);
    REQUIRE(parseCommandName("remove_watch").has_value());
    CHECK(parseCommandName("remove_watch").value() == eCommand::REMOVE_WATCH);
    REQUIRE(parseCommandName("add_breakpoint").has_value());
    CHECK(parseCommandName("add_breakpoint").value() == eCommand::ADD_BREAKPOINT);
    REQUIRE(parseCommandName("set_memory_target").has_value());
    CHECK(parseCommandName("set_memory_target").value() == eCommand::SET_MEMORY_TARGET);
    REQUIRE(parseCommandName("continue_execution").has_value());
    CHECK(parseCommandName("continue_execution").value() == eCommand::CONTINUE_EXECUTION);
    REQUIRE(parseCommandName("step_over").has_value());
    CHECK(parseCommandName("step_over").value() == eCommand::STEP_OVER);
    REQUIRE(parseCommandName("step_into").has_value());
    CHECK(parseCommandName("step_into").value() == eCommand::STEP_INTO);
    REQUIRE(parseCommandName("step_out").has_value());
    CHECK(parseCommandName("step_out").value() == eCommand::STEP_OUT);
    REQUIRE(parseCommandName("pause_execution").has_value());
    CHECK(parseCommandName("pause_execution").value() == eCommand::PAUSE_EXECUTION);
    REQUIRE(parseCommandName("terminate_session").has_value());
    CHECK(parseCommandName("terminate_session").value() == eCommand::TERMINATE_SESSION);
    REQUIRE(parseCommandName("cycle_theme").has_value());
    CHECK(parseCommandName("cycle_theme").value() == eCommand::CYCLE_THEME);
    REQUIRE(parseCommandName("reload_config").has_value());
    CHECK(parseCommandName("reload_config").value() == eCommand::RELOAD_CONFIG);
    CHECK_FALSE(parseCommandName("missing_command").has_value());
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

TEST_CASE("memoryNavigationDelta returns row and page movement for memory view") {
    REQUIRE(memoryNavigationDelta(ftxui::Event::ArrowLeft, 8, 5).has_value());
    CHECK(memoryNavigationDelta(ftxui::Event::ArrowLeft, 8, 5).value() == -8);

    REQUIRE(memoryNavigationDelta(ftxui::Event::Character('l'), 8, 5).has_value());
    CHECK(memoryNavigationDelta(ftxui::Event::Character('l'), 8, 5).value() == 8);

    REQUIRE(memoryNavigationDelta(ftxui::Event::PageUp, 8, 5).has_value());
    CHECK(memoryNavigationDelta(ftxui::Event::PageUp, 8, 5).value() == -40);

    REQUIRE(memoryNavigationDelta(ftxui::Event::PageDown, 8, 5).has_value());
    CHECK(memoryNavigationDelta(ftxui::Event::PageDown, 8, 5).value() == 40);

    CHECK_FALSE(memoryNavigationDelta(ftxui::Event::Tab, 8, 5).has_value());
}
