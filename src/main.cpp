#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "app_config.hpp"
#include "debug_session.hpp"
#include "memory_view.hpp"
#include "pane_state.hpp"
#include "pane_rows.hpp"

namespace {

    ftxui::Element renderSelectablePane(const SSelectablePaneState& pane, bool is_focused) {
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

    std::vector<std::string> buildLeaderHints(const std::vector<SKeybinding>& keybindings) {
        std::vector<std::string> hints;
        hints.reserve(keybindings.size());

        for (const auto& keybinding : keybindings) {
            if (keybinding.keys.rfind("Space ", 0) != 0 || keybinding.keys.size() <= 6) {
                continue;
            }

            hints.push_back(keybinding.keys.substr(6) + " " + commandDescription(keybinding.command));
        }

        return hints;
    }

    std::optional<eCommand> findCommandForKeys(const std::vector<SKeybinding>& keybindings, const std::string& keys) {
        const auto keybinding_iterator = std::ranges::find_if(keybindings, [&](const SKeybinding& keybinding) { return keybinding.keys == keys; });
        if (keybinding_iterator == keybindings.end()) {
            return std::nullopt;
        }

        return keybinding_iterator->command;
    }

    ftxui::Element renderShortcutsOverlay(const std::vector<SKeybinding>& keybindings) {
        using namespace ftxui;

        Elements rows = {
            text(" Roundtable Shortcuts ") | bold,
            separator(),
            text("Tab  Cycle focus"),
            text("q  Quit"),
        };

        for (const auto& keybinding : keybindings) {
            rows.push_back(text(keybinding.keys + "  " + commandDescription(keybinding.command)));
        }

        return window(text(" Shortcuts "), vbox(rows)) | size(WIDTH, GREATER_THAN, 48);
    }

    ftxui::Element renderAuxiliaryViews(const SViewVisibilityState& view_visibility, const SSelectablePaneState& memory_view_pane, const SSelectablePaneState& disassembly_pane,
                                        eFocusPane focused_pane) {
        using namespace ftxui;

        const bool show_memory       = view_visibility.show_memory_view;
        const bool show_disassembly  = view_visibility.show_disassembly_view;
        const bool memory_is_focused = focused_pane == eFocusPane::MEMORY_VIEW;
        const bool disasm_is_focused = focused_pane == eFocusPane::DISASSEMBLY_VIEW;

        if (show_memory && show_disassembly) {
            return hbox({
                       renderSelectablePane(memory_view_pane, memory_is_focused) | flex,
                       renderSelectablePane(disassembly_pane, disasm_is_focused) | flex,
                   }) |
                flex;
        }

        if (show_memory) {
            return renderSelectablePane(memory_view_pane, memory_is_focused) | flex;
        }

        if (show_disassembly) {
            return renderSelectablePane(disassembly_pane, disasm_is_focused) | flex;
        }

        return renderSelectablePane(
                   {
                       .title = " Views ",
                       .rows  = {"Enable Memory or Disassembly with Space t / Space a"},
                   },
                   false) |
            flex;
    }

} // namespace

int main() {
    using namespace ftxui;

    const SAppConfig     app_config      = loadAppConfig("roundtable.toml");
    auto                 screen          = ScreenInteractive::Fullscreen();
    CMockDebugSession    debug_session   = {};
    SDebugSelection      debug_selection = {};
    SViewVisibilityState view_visibility = {
        .show_memory_view      = app_config.show_memory_view,
        .show_disassembly_view = app_config.show_disassembly_view,
    };
    eFocusPane           focused_pane   = normalizeFocusedPane(eFocusPane::MEMORY_VIEW, view_visibility);
    const auto           keybindings    = app_config.keybindings;
    bool                 leader_pending = false;

    SSelectablePaneState locals_pane = {
        .title = " Locals ",
        .rows  = formatLocalsPaneRows(debug_session.getLocals(debug_selection)),
    };
    SSelectablePaneState memory_view_pane = {
        .title = " Memory View ",
        .rows  = generateMemoryViewRows(debug_session.readMemory(debug_selection,
                                                                 {
                                                                     .start_address = 0x1000,
                                                                     .byte_count    = 40,
                                                                     .bytes_per_row = 8,
                                                                })),
    };
    SSelectablePaneState disassembly_pane = {
        .title = " Disassembly ",
        .rows  = formatDisassemblyPaneRows(debug_session.disassemble(debug_selection, 0x401000, 8)),
    };
    SSelectablePaneState watch_list_pane = {
        .title = " Watch List ",
        .rows  = formatWatchListPaneRows(debug_session.evaluateWatches(debug_selection,
                                                                       {
                                                                          {.expression = "a"},
                                                                          {.expression = "ptr"},
                                                                      })),
    };

    auto renderer = Renderer([&] {
        Element  locals          = renderSelectablePane(locals_pane, focused_pane == eFocusPane::LOCALS);
        Element  watch_list      = renderSelectablePane(watch_list_pane, focused_pane == eFocusPane::WATCH_LIST);
        Element  auxiliary_views = renderAuxiliaryViews(view_visibility, memory_view_pane, disassembly_pane, focused_pane);

        Elements status_items = {
            text(" Roundtable ") | inverted, separator(), text(" Tab cycle "), separator(), text(" Space commands "), separator(), text(" q quit "),
        };

        if (leader_pending) {
            const auto hints = buildLeaderHints(keybindings);
            for (const auto& hint : hints) {
                status_items.push_back(separator());
                status_items.push_back(text(hint));
            }
        }

        Element content = vbox({
            hbox({
                locals | size(WIDTH, EQUAL, 28),
                auxiliary_views | flex,
                watch_list | size(WIDTH, EQUAL, 28),
            }) | flex,
            hbox(status_items) | border,
        });

        if (view_visibility.show_shortcuts_overlay) {
            content = dbox({
                content,
                renderShortcutsOverlay(keybindings) | center,
            });
        }

        return content;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Character('q')) {
            screen.Exit();
            return true;
        }

        if (view_visibility.show_shortcuts_overlay && (event == Event::Escape || event == Event::Character('?'))) {
            view_visibility.show_shortcuts_overlay = false;
            leader_pending                         = false;
            return true;
        }

        if (leader_pending) {
            leader_pending = false;

            if (event == Event::Escape) {
                return true;
            }

            if (event.is_character()) {
                const auto command = findCommandForKeys(keybindings, "Space " + event.character());
                if (command.has_value()) {
                    executeCommand(command.value(), focused_pane, view_visibility);
                    return true;
                }
            }

            return true;
        }

        if (event == Event::Character(' ')) {
            leader_pending = true;
            return true;
        }

        if (event == Event::Tab) {
            focused_pane = advanceFocusPane(focused_pane, view_visibility);
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
        if (focused_pane == eFocusPane::DISASSEMBLY_VIEW) {
            return handleVerticalNavigation(event, disassembly_pane);
        }

        return false;
    });

    screen.Loop(component);
}
