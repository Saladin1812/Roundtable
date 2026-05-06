#include "process_list.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <exception>
#include <ranges>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

    std::string trim(std::string value) {
        const auto not_space = [](unsigned char character) { return !std::isspace(character); };
        const auto begin     = std::ranges::find_if(value, not_space);
        if (begin == value.end()) {
            return {};
        }

        const auto end = std::ranges::find_if(std::ranges::reverse_view(value), not_space).base();
        return std::string(begin, end);
    }

    std::vector<std::string> parseCsvLine(const std::string& line) {
        std::vector<std::string> fields;
        std::string              field;
        bool                     in_quotes = false;

        for (std::size_t index = 0; index < line.size(); ++index) {
            const char character = line[index];
            if (character == '"') {
                if (in_quotes && index + 1 < line.size() && line[index + 1] == '"') {
                    field.push_back('"');
                    ++index;
                } else {
                    in_quotes = !in_quotes;
                }
                continue;
            }

            if (character == ',' && !in_quotes) {
                fields.push_back(field);
                field.clear();
                continue;
            }

            field.push_back(character);
        }

        fields.push_back(field);
        return fields;
    }

    std::string readCommandOutput(const char* command) {
#ifdef _WIN32
        FILE* pipe = _popen(command, "r");
#else
        FILE* pipe = popen(command, "r");
#endif
        if (pipe == nullptr) {
            return {};
        }

        std::array<char, 512> buffer = {};
        std::string           output;
        while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return output;
    }

    std::int64_t currentProcessId() {
#ifdef _WIN32
        return static_cast<std::int64_t>(GetCurrentProcessId());
#else
        return static_cast<std::int64_t>(getpid());
#endif
    }

    void normalizeProcesses(std::vector<SProcessInfo>& processes) {
        const auto self_process_id = currentProcessId();
        processes.erase(std::remove_if(processes.begin(), processes.end(),
                                       [&](const SProcessInfo& process) { return process.process_id <= 0 || process.process_id == self_process_id || process.name.empty(); }),
                        processes.end());

        std::ranges::sort(processes, [](const SProcessInfo& left, const SProcessInfo& right) {
            if (left.name != right.name) {
                return left.name < right.name;
            }
            return left.process_id < right.process_id;
        });
    }

} // namespace

std::vector<SProcessInfo> parseUnixProcessListOutput(const std::string& output) {
    std::vector<SProcessInfo> processes;
    std::istringstream        output_stream(output);
    std::string               line;

    while (std::getline(output_stream, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        std::istringstream line_stream(line);
        std::int64_t       process_id = 0;
        if (!(line_stream >> process_id)) {
            continue;
        }

        std::string process_name;
        std::getline(line_stream, process_name);
        process_name = trim(process_name);
        if (process_name.empty()) {
            continue;
        }

        processes.push_back({
            .process_id = process_id,
            .name       = process_name,
        });
    }

    return processes;
}

std::vector<SProcessInfo> parseWindowsProcessListOutput(const std::string& output) {
    std::vector<SProcessInfo> processes;
    std::istringstream        output_stream(output);
    std::string               line;

    while (std::getline(output_stream, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const auto fields = parseCsvLine(line);
        if (fields.size() < 2) {
            continue;
        }

        try {
            processes.push_back({
                .process_id = std::stoll(trim(fields[1])),
                .name       = trim(fields[0]),
            });
        } catch (const std::exception&) { continue; }
    }

    return processes;
}

std::vector<SProcessInfo> listAttachableProcesses() {
    std::vector<SProcessInfo> processes;

#ifdef _WIN32
    processes = parseWindowsProcessListOutput(readCommandOutput("tasklist /FO CSV /NH"));
#else
    processes = parseUnixProcessListOutput(readCommandOutput("ps -e -o pid=,comm="));
#endif

    normalizeProcesses(processes);
    return processes;
}
