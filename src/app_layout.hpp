#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include "app_config.hpp"
#include "app_theme.hpp"
#include "pane_refresh.hpp"
#include "pane_state.hpp"

struct SDebugSelection;
struct SStoppedContext;

enum class ePromptMode : std::uint8_t {
    NONE,
    ADD_WATCH,
    EDIT_WATCH,
    ADD_BREAKPOINT,
    MEMORY_TARGET,
};

struct SPromptState {
    ePromptMode mode = ePromptMode::NONE;
    std::string input;
    std::size_t cursor_index     = 0;
    bool        replace_on_input = false;
};

struct SThemePickerState {
    bool         active          = false;
    eThemePreset original_preset = eThemePreset::DEFAULT;
    std::size_t  selected_index  = 0;
};

struct SProfilePickerState {
    bool        active         = false;
    std::size_t selected_index = 0;
};

struct SAppLayoutState {
    const SSelectablePaneState&              locals_pane;
    const SSelectablePaneState&              threads_pane;
    const SSelectablePaneState&              stack_pane;
    const SSelectablePaneState&              memory_view_pane;
    const SSelectablePaneState&              disassembly_pane;
    const SSelectablePaneState&              watch_list_pane;
    const SSelectablePaneState&              breakpoints_pane;
    const SViewVisibilityState&              view_visibility;
    const SMemoryRenderContext&              memory_context;
    const SAppTheme&                         theme;
    const std::vector<SKeybinding>&          keybindings;
    const std::vector<SLaunchProfileConfig>& launch_profiles;
    const SStoppedContext&                   stopped_context;
    const SDebugSelection&                   debug_selection;
    const SPromptState&                      prompt_state;
    const SThemePickerState&                 theme_picker_state;
    const SProfilePickerState&               profile_picker_state;
    eFocusPane                               focused_pane = eFocusPane::MEMORY_VIEW;
    std::string                              current_status;
    bool                                     leader_pending = false;
};

constexpr std::array<eThemePreset, 4> kThemePresets = {
    eThemePreset::DEFAULT,
    eThemePreset::AMBER,
    eThemePreset::ICE,
    eThemePreset::FOREST,
};

SPromptState   beginPrompt(ePromptMode mode, std::string initial_input = "", bool replace_on_input = false);
std::string    buildPromptDisplay(const SPromptState& prompt_state);
std::string    themePresetName(eThemePreset preset);
std::size_t    themePresetIndex(eThemePreset preset);
ftxui::Element renderRoundtableLayout(const SAppLayoutState& state);
