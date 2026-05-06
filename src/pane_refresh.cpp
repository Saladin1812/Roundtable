#include "pane_refresh.hpp"

#include <algorithm>
#include <utility>

#include "memory_view.hpp"
#include "pane_rows.hpp"

namespace {

    std::string compactMemoryTargetLabel(std::string source, const std::string& value = {}) {
        if (source == "ip") {
            return "IP";
        }
        if (value.empty()) {
            return source;
        }

        return std::move(source) + ":" + value;
    }

} // namespace

void refreshPaneRows(const SPaneRefreshInputs& inputs, SPaneRefreshOutputs& outputs) {
    const auto locals            = inputs.debug_session.getLocals(inputs.debug_selection);
    const auto watch_results     = inputs.debug_session.evaluateWatches(inputs.debug_selection, inputs.watch_expressions);
    outputs.locals_pane.rows     = formatLocalsPaneRows(locals);
    outputs.watch_list_pane.rows = formatWatchListPaneRows(watch_results);
    outputs.memory_target_label  = compactMemoryTargetLabel("IP");
    outputs.memory_context.highlight.reset();

    if (!inputs.manual_memory_target.empty()) {
        outputs.memory_target_label = compactMemoryTargetLabel("M", inputs.manual_memory_target);
        if (const auto direct_address = findFirstHexAddress(inputs.manual_memory_target); direct_address.has_value()) {
            const SMemoryReadRequest memory_read_request = {
                .start_address    = direct_address.value(),
                .memory_reference = "",
                .byte_count       = 40,
                .bytes_per_row    = 8,
            };
            const auto memory_read_result = inputs.debug_session.readMemory(inputs.debug_selection, memory_read_request);
            outputs.memory_view_pane.rows = generateMemoryViewRows(memory_read_result);
        } else {
            const std::vector<SWatchResult> memory_target_results = inputs.debug_session.evaluateWatches(inputs.debug_selection, {{.expression = inputs.manual_memory_target}});
            const auto memory_read_request   = buildMemoryReadRequest(memory_target_results, 0, inputs.disassembly_start_address, inputs.disassembly_memory_reference);
            const auto memory_read_result    = inputs.debug_session.readMemory(inputs.debug_selection, memory_read_request);
            const auto synthetic_memory_rows = buildSyntheticMemoryRows(memory_target_results, 0, memory_read_request.bytes_per_row);
            const bool using_synthetic_rows  = synthetic_memory_rows.has_value();
            const bool target_has_explicit_memory_reference = !memory_target_results.empty() && !memory_target_results.front().memory_reference.empty() &&
                findFirstHexAddress(memory_target_results.front().memory_reference).has_value();
            outputs.memory_context.highlight =
                buildMemoryByteHighlight(memory_target_results, 0, memory_read_request, using_synthetic_rows && !target_has_explicit_memory_reference);

            if (synthetic_memory_rows.has_value() && !target_has_explicit_memory_reference) {
                outputs.memory_view_pane.rows = synthetic_memory_rows.value();
            } else if (!memory_read_result.error_message.empty() && synthetic_memory_rows.has_value()) {
                outputs.memory_view_pane.rows = synthetic_memory_rows.value();
            } else {
                outputs.memory_view_pane.rows = generateMemoryViewRows(memory_read_result);
            }
        }
    } else if (inputs.focused_pane == eFocusPane::WATCH_LIST && !watch_results.empty()) {
        const auto selected_watch_index = std::min(outputs.watch_list_pane.selected_index, watch_results.size() - 1);
        outputs.memory_target_label     = compactMemoryTargetLabel("W", watch_results[selected_watch_index].expression);
        const auto memory_read_request =
            buildMemoryReadRequest(watch_results, outputs.watch_list_pane.selected_index, inputs.disassembly_start_address, inputs.disassembly_memory_reference);
        const auto memory_read_result    = inputs.debug_session.readMemory(inputs.debug_selection, memory_read_request);
        const auto synthetic_memory_rows = buildSyntheticMemoryRows(watch_results, outputs.watch_list_pane.selected_index, memory_read_request.bytes_per_row);
        const bool using_synthetic_rows  = synthetic_memory_rows.has_value();
        const bool selected_watch_has_explicit_memory_reference =
            !watch_results[selected_watch_index].memory_reference.empty() && findFirstHexAddress(watch_results[selected_watch_index].memory_reference).has_value();
        outputs.memory_context.highlight = buildMemoryByteHighlight(watch_results, outputs.watch_list_pane.selected_index, memory_read_request,
                                                                    using_synthetic_rows && !selected_watch_has_explicit_memory_reference);

        if (synthetic_memory_rows.has_value() && !selected_watch_has_explicit_memory_reference) {
            outputs.memory_view_pane.rows = synthetic_memory_rows.value();
        } else if (!memory_read_result.error_message.empty() && synthetic_memory_rows.has_value()) {
            outputs.memory_view_pane.rows = synthetic_memory_rows.value();
        } else {
            outputs.memory_view_pane.rows = generateMemoryViewRows(memory_read_result);
        }
    } else {
        if (!locals.empty()) {
            const auto selected_local_index = std::min(outputs.locals_pane.selected_index, locals.size() - 1);
            outputs.memory_target_label     = compactMemoryTargetLabel("L", locals[selected_local_index].name);
        }
        const auto memory_read_request   = buildMemoryReadRequest(inputs.debug_session, inputs.debug_selection, locals, outputs.locals_pane.selected_index,
                                                                  inputs.disassembly_start_address, inputs.disassembly_memory_reference);
        const auto memory_read_result    = inputs.debug_session.readMemory(inputs.debug_selection, memory_read_request);
        const auto synthetic_memory_rows = buildSyntheticMemoryRows(locals, outputs.locals_pane.selected_index, memory_read_request.bytes_per_row);
        const bool using_synthetic_rows  = synthetic_memory_rows.has_value();
        const auto selected_local_index  = locals.empty() ? 0UL : std::min(outputs.locals_pane.selected_index, locals.size() - 1);
        const bool selected_local_has_explicit_memory_reference =
            !locals.empty() && !locals[selected_local_index].memory_reference.empty() && findFirstHexAddress(locals[selected_local_index].memory_reference).has_value();
        outputs.memory_context.highlight =
            buildMemoryByteHighlight(locals, outputs.locals_pane.selected_index, memory_read_request, using_synthetic_rows && !selected_local_has_explicit_memory_reference);

        if (synthetic_memory_rows.has_value() && !selected_local_has_explicit_memory_reference) {
            outputs.memory_view_pane.rows = synthetic_memory_rows.value();
        } else if (!memory_read_result.error_message.empty() && synthetic_memory_rows.has_value()) {
            outputs.memory_view_pane.rows = synthetic_memory_rows.value();
        } else {
            outputs.memory_view_pane.rows = generateMemoryViewRows(memory_read_result);
        }
    }

    outputs.memory_view_pane.title = " Memory [" + outputs.memory_target_label + "] ";
    outputs.disassembly_pane.rows  = formatDisassemblyPaneRows(inputs.debug_session.disassemble(inputs.debug_selection, inputs.disassembly_start_address, 8));

    outputs.locals_pane.selected_index = std::min(outputs.locals_pane.selected_index, outputs.locals_pane.rows.empty() ? 0UL : outputs.locals_pane.rows.size() - 1);
    outputs.memory_view_pane.selected_index =
        std::min(outputs.memory_view_pane.selected_index, outputs.memory_view_pane.rows.empty() ? 0UL : outputs.memory_view_pane.rows.size() - 1);
    outputs.disassembly_pane.selected_index =
        std::min(outputs.disassembly_pane.selected_index, outputs.disassembly_pane.rows.empty() ? 0UL : outputs.disassembly_pane.rows.size() - 1);
    outputs.watch_list_pane.selected_index = std::min(outputs.watch_list_pane.selected_index, outputs.watch_list_pane.rows.empty() ? 0UL : outputs.watch_list_pane.rows.size() - 1);
}
