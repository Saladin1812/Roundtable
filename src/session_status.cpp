#include "session_status.hpp"

#include <algorithm>

namespace {

    bool hasLocation(const SStoppedLocation& location) {
        return !location.function_name.empty() || !location.source_path.empty() || location.line > 0;
    }

    std::string compactPathForStatus(const std::string& source_path) {
        if (source_path.empty()) {
            return "<unknown>";
        }

        const auto last_separator = source_path.find_last_of("/\\");
        if (last_separator == std::string::npos) {
            return source_path;
        }

        return source_path.substr(last_separator + 1);
    }

    SStoppedLocation selectedLocation(const SStoppedContext& stopped_context, const SDebugSelection& selection) {
        if (selection.frame_index < stopped_context.stack_frames.size()) {
            const auto& frame = stopped_context.stack_frames[selection.frame_index];
            return {
                .function_name = frame.function_name,
                .source_path   = frame.source_path,
                .line          = frame.line,
                .column        = frame.column,
            };
        }

        return stopped_context.location;
    }

    std::string formatLocation(const SStoppedLocation& location) {
        if (!hasLocation(location)) {
            return "location n/a";
        }

        std::string text = compactPathForStatus(location.source_path);
        if (location.line > 0) {
            text += ":" + std::to_string(location.line);
            if (location.column > 0) {
                text += ":" + std::to_string(location.column);
            }
        }

        if (!location.function_name.empty()) {
            text += " " + location.function_name + "()";
        }

        return text;
    }

} // namespace

std::string formatStoppedContextStatus(const SStoppedContext& stopped_context, const SDebugSelection& selection) {
    std::string text = "Frame ";

    if (selection.thread_id != 0) {
        text += "T:" + std::to_string(selection.thread_id);
        const auto thread_iterator =
            std::ranges::find_if(stopped_context.threads, [&](const SStoppedThread& thread) { return thread.id == selection.thread_id && !thread.name.empty(); });
        if (thread_iterator != stopped_context.threads.end()) {
            text += " " + thread_iterator->name;
        }
    } else {
        text += "T:n/a";
    }

    text += " F:" + std::to_string(selection.frame_index);
    text += " ";
    text += formatLocation(selectedLocation(stopped_context, selection));
    return text;
}
