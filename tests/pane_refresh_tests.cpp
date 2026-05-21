#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "debug_session.hpp"
#include "pane_refresh.hpp"

TEST_CASE("refreshPaneRows starts selected local memory at the variable address") {
    CMockDebugSession                   debug_session;

    const SDebugSelection               debug_selection = {};
    const std::string                   disassembly_memory_reference;
    const std::vector<SWatchExpression> watch_expressions;
    const std::string                   manual_memory_target;

    SSelectablePaneState                locals_pane = {
                       .title          = " Locals ",
                       .rows           = {},
                       .selected_index = 0,
    };
    SSelectablePaneState memory_view_pane = {
        .title          = " Memory ",
        .rows           = {},
        .selected_index = 0,
    };
    SSelectablePaneState disassembly_pane = {
        .title          = " Disassembly ",
        .rows           = {},
        .selected_index = 0,
    };
    SSelectablePaneState watch_list_pane = {
        .title          = " Watch List ",
        .rows           = {},
        .selected_index = 0,
    };

    std::string          memory_target_label;
    SMemoryRenderContext memory_context;

    SPaneRefreshInputs   inputs = {
          .debug_session                = debug_session,
          .debug_selection              = debug_selection,
          .disassembly_start_address    = 0x401000,
          .disassembly_memory_reference = disassembly_memory_reference,
          .memory_navigation_offset     = 0,
          .focused_pane                 = eFocusPane::LOCALS,
          .watch_expressions            = watch_expressions,
          .manual_memory_target         = manual_memory_target,
    };
    SPaneRefreshOutputs outputs = {
        .locals_pane         = locals_pane,
        .memory_view_pane    = memory_view_pane,
        .disassembly_pane    = disassembly_pane,
        .watch_list_pane     = watch_list_pane,
        .memory_target_label = memory_target_label,
        .memory_context      = memory_context,
    };

    refreshPaneRows(inputs, outputs);

    REQUIRE_FALSE(memory_view_pane.rows.empty());
    REQUIRE(memory_view_pane.rows.size() >= 3);
    CHECK(memory_view_pane.rows[2] == "0x2000  2A 00 00 00 90 91 92 93  *.......");
}
