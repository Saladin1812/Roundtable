#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct SCodeLldbInstall {
    std::string command;
    std::string liblldb_path;
    std::string source;
};

std::optional<SCodeLldbInstall> findCodeLldbInstall(const std::vector<std::filesystem::path>& additional_roots = {});
