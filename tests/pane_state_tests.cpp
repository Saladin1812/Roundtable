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
    CHECK(advanceFocusPane(eFocusPane::WATCH_LIST, view_visibility) == eFocusPane::BREAKPOINTS);
    CHECK(advanceFocusPane(eFocusPane::BREAKPOINTS, view_visibility) == eFocusPane::LOCALS);
    CHECK(advanceFocusPane(eFocusPane::LOCALS, view_visibility) == eFocusPane::THREADS);
    CHECK(advanceFocusPane(eFocusPane::THREADS, view_visibility) == eFocusPane::STACK);
    CHECK(advanceFocusPane(eFocusPane::STACK, view_visibility) == eFocusPane::MEMORY_VIEW);
}

TEST_CASE("advanceFocusPane skips hidden center panes") {
    const SViewVisibilityState view_visibility = {
        .show_memory_view      = false,
        .show_disassembly_view = true,
    };

    CHECK(advanceFocusPane(eFocusPane::LOCALS, view_visibility) == eFocusPane::THREADS);
    CHECK(advanceFocusPane(eFocusPane::THREADS, view_visibility) == eFocusPane::STACK);
    CHECK(advanceFocusPane(eFocusPane::STACK, view_visibility) == eFocusPane::DISASSEMBLY_VIEW);
    CHECK(advanceFocusPane(eFocusPane::DISASSEMBLY_VIEW, view_visibility) == eFocusPane::WATCH_LIST);
    CHECK(advanceFocusPane(eFocusPane::WATCH_LIST, view_visibility) == eFocusPane::BREAKPOINTS);
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
    CHECK(parseCommandName("focus_memory") == eCommand::FOCUS_MEMORY);
    CHECK(parseCommandName("focus_threads") == eCommand::FOCUS_THREADS);
    CHECK(parseCommandName("focus_stack") == eCommand::FOCUS_STACK);
    CHECK(parseCommandName("focus_breakpoints") == eCommand::FOCUS_BREAKPOINTS);
    CHECK(parseCommandName("add_watch") == eCommand::ADD_WATCH);
    CHECK(parseCommandName("edit_watch") == eCommand::EDIT_WATCH);
    CHECK(parseCommandName("remove_watch") == eCommand::REMOVE_WATCH);
    CHECK(parseCommandName("move_watch_up") == eCommand::MOVE_WATCH_UP);
    CHECK(parseCommandName("move_watch_down") == eCommand::MOVE_WATCH_DOWN);
    CHECK(parseCommandName("duplicate_watch") == eCommand::DUPLICATE_WATCH);
    CHECK(parseCommandName("add_breakpoint") == eCommand::ADD_BREAKPOINT);
    CHECK(parseCommandName("remove_breakpoint") == eCommand::REMOVE_BREAKPOINT);
    CHECK(parseCommandName("toggle_breakpoint") == eCommand::TOGGLE_BREAKPOINT);
    CHECK(parseCommandName("set_memory_target") == eCommand::SET_MEMORY_TARGET);
    CHECK(parseCommandName("continue_execution") == eCommand::CONTINUE_EXECUTION);
    CHECK(parseCommandName("step_over") == eCommand::STEP_OVER);
    CHECK(parseCommandName("step_into") == eCommand::STEP_INTO);
    CHECK(parseCommandName("step_out") == eCommand::STEP_OUT);
    CHECK(parseCommandName("pause_execution") == eCommand::PAUSE_EXECUTION);
    CHECK(parseCommandName("terminate_session") == eCommand::TERMINATE_SESSION);
    CHECK(parseCommandName("restart_session") == eCommand::RESTART_SESSION);
    CHECK(parseCommandName("choose_profile") == eCommand::CHOOSE_PROFILE);
    CHECK(parseCommandName("cycle_theme") == eCommand::CYCLE_THEME);
    CHECK(parseCommandName("reload_config") == eCommand::RELOAD_CONFIG);
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
    CHECK(memoryNavigationDelta(ftxui::Event::ArrowLeft, 8, 5) == -8);

    CHECK(memoryNavigationDelta(ftxui::Event::Character('l'), 8, 5) == 8);

    CHECK(memoryNavigationDelta(ftxui::Event::PageUp, 8, 5) == -40);

    CHECK(memoryNavigationDelta(ftxui::Event::PageDown, 8, 5) == 40);

    CHECK_FALSE(memoryNavigationDelta(ftxui::Event::Tab, 8, 5).has_value());
}
