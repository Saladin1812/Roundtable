#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SProcessInfo {
    std::int64_t process_id = 0;
    std::string  name;
};

std::vector<SProcessInfo> parseUnixProcessListOutput(const std::string& output);
std::vector<SProcessInfo> parseWindowsProcessListOutput(const std::string& output);
std::vector<SProcessInfo> listAttachableProcesses();
