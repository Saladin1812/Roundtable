#pragma once

#include <cstddef>
#include <cstdint>
#include <ftxui/component/event.hpp>
#include <optional>
#include <string>
#include <vector>

enum class eFocusPane : std::uint8_t {
    LOCALS,
    MEMORY_VIEW,
    DISASSEMBLY_VIEW,
    WATCH_LIST,
};

enum class eCommand : std::uint8_t {
    FOCUS_LOCALS,
    FOCUS_MEMORY,
    FOCUS_DISASSEMBLY,
    FOCUS_WATCH_LIST,
    ADD_WATCH,
    EDIT_WATCH,
    REMOVE_WATCH,
    ADD_BREAKPOINT,
    SET_MEMORY_TARGET,
    CONTINUE_EXECUTION,
    STEP_OVER,
    STEP_INTO,
    STEP_OUT,
    PAUSE_EXECUTION,
    TERMINATE_SESSION,
    RESTART_SESSION,
    CYCLE_THEME,
    RELOAD_CONFIG,
    TOGGLE_MEMORY,
    TOGGLE_DISASSEMBLY,
    TOGGLE_SHORTCUTS_HELP,
};

struct SSelectablePaneState {
    std::string              title;
    std::vector<std::string> rows;
    std::size_t              selected_index = 0;
};

struct SViewVisibilityState {
    bool show_memory_view       = true;
    bool show_disassembly_view  = false;
    bool show_shortcuts_overlay = false;
};

struct SKeybinding {
    std::string keys;
    eCommand    command = eCommand::FOCUS_MEMORY;
};

bool                        handleVerticalNavigation(ftxui::Event event, SSelectablePaneState& pane);
eFocusPane                  advanceFocusPane(eFocusPane focused_pane, const SViewVisibilityState& view_visibility);
eFocusPane                  normalizeFocusedPane(eFocusPane focused_pane, const SViewVisibilityState& view_visibility);
bool                        isPaneVisible(eFocusPane focused_pane, const SViewVisibilityState& view_visibility);
void                        executeCommand(eCommand command, eFocusPane& focused_pane, SViewVisibilityState& view_visibility);
std::optional<eCommand>     parseCommandName(const std::string& command_name);
std::string                 commandDescription(eCommand command);
std::vector<SKeybinding>    defaultKeybindings();
std::optional<std::int64_t> memoryNavigationDelta(ftxui::Event event, std::size_t bytes_per_row, std::size_t visible_row_count);
