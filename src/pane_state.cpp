#include "pane_state.hpp"

#include <array>

namespace {

    constexpr std::array<eFocusPane, 4> kFocusOrder = {
        eFocusPane::LOCALS,
        eFocusPane::MEMORY_VIEW,
        eFocusPane::DISASSEMBLY_VIEW,
        eFocusPane::WATCH_LIST,
    };

} // namespace

bool handleVerticalNavigation(ftxui::Event event, SSelectablePaneState& pane) {
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

bool isPaneVisible(eFocusPane focused_pane, const SViewVisibilityState& view_visibility) {
    switch (focused_pane) {
        case eFocusPane::LOCALS: return true;
        case eFocusPane::WATCH_LIST: return true;
        case eFocusPane::MEMORY_VIEW: return view_visibility.show_memory_view;
        case eFocusPane::DISASSEMBLY_VIEW: return view_visibility.show_disassembly_view;
    }

    return false;
}

eFocusPane normalizeFocusedPane(eFocusPane focused_pane, const SViewVisibilityState& view_visibility) {
    if (isPaneVisible(focused_pane, view_visibility)) {
        return focused_pane;
    }

    for (const auto candidate : kFocusOrder) {
        if (isPaneVisible(candidate, view_visibility)) {
            return candidate;
        }
    }

    return eFocusPane::LOCALS;
}

eFocusPane advanceFocusPane(eFocusPane focused_pane, const SViewVisibilityState& view_visibility) {
    eFocusPane normalized_focus = normalizeFocusedPane(focused_pane, view_visibility);

    for (std::size_t i = 0; i < kFocusOrder.size(); ++i) {
        if (kFocusOrder[i] != normalized_focus) {
            continue;
        }

        for (std::size_t offset = 1; offset <= kFocusOrder.size(); ++offset) {
            const auto candidate = kFocusOrder[(i + offset) % kFocusOrder.size()];
            if (isPaneVisible(candidate, view_visibility)) {
                return candidate;
            }
        }
    }

    return normalized_focus;
}

void executeCommand(eCommand command, eFocusPane& focused_pane, SViewVisibilityState& view_visibility) {
    switch (command) {
        case eCommand::FOCUS_LOCALS: focused_pane = eFocusPane::LOCALS; break;
        case eCommand::FOCUS_MEMORY:
            view_visibility.show_memory_view = true;
            focused_pane                     = eFocusPane::MEMORY_VIEW;
            break;
        case eCommand::FOCUS_DISASSEMBLY:
            view_visibility.show_disassembly_view = true;
            focused_pane                          = eFocusPane::DISASSEMBLY_VIEW;
            break;
        case eCommand::FOCUS_WATCH_LIST: focused_pane = eFocusPane::WATCH_LIST; break;
        case eCommand::ADD_WATCH: break;
        case eCommand::EDIT_WATCH: break;
        case eCommand::REMOVE_WATCH: break;
        case eCommand::SET_MEMORY_TARGET: break;
        case eCommand::CYCLE_THEME: break;
        case eCommand::RELOAD_CONFIG: break;
        case eCommand::TOGGLE_MEMORY: view_visibility.show_memory_view = !view_visibility.show_memory_view; break;
        case eCommand::TOGGLE_DISASSEMBLY: view_visibility.show_disassembly_view = !view_visibility.show_disassembly_view; break;
        case eCommand::TOGGLE_SHORTCUTS_HELP: view_visibility.show_shortcuts_overlay = !view_visibility.show_shortcuts_overlay; break;
    }

    focused_pane = normalizeFocusedPane(focused_pane, view_visibility);
}

std::optional<eCommand> parseCommandName(const std::string& command_name) {
    if (command_name == "focus_locals") {
        return eCommand::FOCUS_LOCALS;
    }
    if (command_name == "focus_memory") {
        return eCommand::FOCUS_MEMORY;
    }
    if (command_name == "focus_disassembly") {
        return eCommand::FOCUS_DISASSEMBLY;
    }
    if (command_name == "focus_watch_list") {
        return eCommand::FOCUS_WATCH_LIST;
    }
    if (command_name == "add_watch") {
        return eCommand::ADD_WATCH;
    }
    if (command_name == "edit_watch") {
        return eCommand::EDIT_WATCH;
    }
    if (command_name == "remove_watch") {
        return eCommand::REMOVE_WATCH;
    }
    if (command_name == "set_memory_target") {
        return eCommand::SET_MEMORY_TARGET;
    }
    if (command_name == "cycle_theme") {
        return eCommand::CYCLE_THEME;
    }
    if (command_name == "reload_config") {
        return eCommand::RELOAD_CONFIG;
    }
    if (command_name == "toggle_memory") {
        return eCommand::TOGGLE_MEMORY;
    }
    if (command_name == "toggle_disassembly") {
        return eCommand::TOGGLE_DISASSEMBLY;
    }
    if (command_name == "toggle_shortcuts_help") {
        return eCommand::TOGGLE_SHORTCUTS_HELP;
    }

    return std::nullopt;
}

std::string commandDescription(eCommand command) {
    switch (command) {
        case eCommand::FOCUS_LOCALS: return "Focus Locals";
        case eCommand::FOCUS_MEMORY: return "Focus Memory";
        case eCommand::FOCUS_DISASSEMBLY: return "Focus Disassembly";
        case eCommand::FOCUS_WATCH_LIST: return "Focus Watch List";
        case eCommand::ADD_WATCH: return "Add Watch";
        case eCommand::EDIT_WATCH: return "Edit Watch";
        case eCommand::REMOVE_WATCH: return "Remove Watch";
        case eCommand::SET_MEMORY_TARGET: return "Set Memory Target";
        case eCommand::CYCLE_THEME: return "Choose Theme";
        case eCommand::RELOAD_CONFIG: return "Reload Config";
        case eCommand::TOGGLE_MEMORY: return "Toggle Memory View";
        case eCommand::TOGGLE_DISASSEMBLY: return "Toggle Disassembly View";
        case eCommand::TOGGLE_SHORTCUTS_HELP: return "Toggle Shortcuts Help";
    }

    return "Unknown Command";
}

std::vector<SKeybinding> defaultKeybindings() {
    return {
        {.keys = "Space l", .command = eCommand::FOCUS_LOCALS},       {.keys = "Space m", .command = eCommand::FOCUS_MEMORY},
        {.keys = "Space d", .command = eCommand::FOCUS_DISASSEMBLY},  {.keys = "Space w", .command = eCommand::FOCUS_WATCH_LIST},
        {.keys = "Space n", .command = eCommand::ADD_WATCH},          {.keys = "Space e", .command = eCommand::EDIT_WATCH},
        {.keys = "Space x", .command = eCommand::REMOVE_WATCH},       {.keys = "Space g", .command = eCommand::SET_MEMORY_TARGET},
        {.keys = "Space c", .command = eCommand::CYCLE_THEME},
        {.keys = "Space R", .command = eCommand::RELOAD_CONFIG},      {.keys = "Space t", .command = eCommand::TOGGLE_MEMORY},
        {.keys = "Space a", .command = eCommand::TOGGLE_DISASSEMBLY}, {.keys = "Space ?", .command = eCommand::TOGGLE_SHORTCUTS_HELP},
    };
}

std::optional<std::int64_t> memoryNavigationDelta(ftxui::Event event, std::size_t bytes_per_row, std::size_t visible_row_count) {
    if (bytes_per_row == 0) {
        return std::nullopt;
    }

    const auto row_delta  = static_cast<std::int64_t>(bytes_per_row);
    const auto page_delta = static_cast<std::int64_t>(bytes_per_row * std::max<std::size_t>(visible_row_count, 1));

    if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::Character('h')) {
        return -row_delta;
    }

    if (event == ftxui::Event::ArrowRight || event == ftxui::Event::Character('l')) {
        return row_delta;
    }

    if (event == ftxui::Event::PageUp) {
        return -page_delta;
    }

    if (event == ftxui::Event::PageDown) {
        return page_delta;
    }

    return std::nullopt;
}
