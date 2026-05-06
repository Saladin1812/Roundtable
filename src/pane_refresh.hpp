#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "debug_session.hpp"
#include "memory_selection.hpp"
#include "pane_state.hpp"

struct SMemoryRenderContext {
    std::optional<SMemoryByteHighlight> highlight;
};

struct SPaneRefreshInputs {
    IDebugSession&                       debug_session;
    const SDebugSelection&               debug_selection;
    std::uint64_t                        disassembly_start_address = 0x401000;
    const std::string&                   disassembly_memory_reference;
    std::int64_t                         memory_navigation_offset = 0;
    eFocusPane                           focused_pane             = eFocusPane::MEMORY_VIEW;
    const std::vector<SWatchExpression>& watch_expressions;
    const std::string&                   manual_memory_target;
};

struct SPaneRefreshOutputs {
    SSelectablePaneState& locals_pane;
    SSelectablePaneState& memory_view_pane;
    SSelectablePaneState& disassembly_pane;
    SSelectablePaneState& watch_list_pane;
    std::string&          memory_target_label;
    SMemoryRenderContext& memory_context;
};

void refreshPaneRows(const SPaneRefreshInputs& inputs, SPaneRefreshOutputs& outputs);
