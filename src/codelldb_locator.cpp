#include "codelldb_locator.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>

namespace {

    std::optional<std::filesystem::path> getEnvPath(const char* name) {
        const char* value = std::getenv(name);
        if (value == nullptr || *value == '\0') {
            return std::nullopt;
        }

        return std::filesystem::path(value);
    }

    std::filesystem::path adapterRelativePath() {
#if defined(_WIN32)
        return std::filesystem::path("adapter") / "codelldb.exe";
#else
        return std::filesystem::path("adapter") / "codelldb";
#endif
    }

    std::filesystem::path liblldbRelativePath() {
#if defined(_WIN32)
        return std::filesystem::path("lldb") / "bin" / "liblldb.dll";
#elif defined(__APPLE__)
        return std::filesystem::path("lldb") / "lib" / "liblldb.dylib";
#else
        return std::filesystem::path("lldb") / "lib" / "liblldb.so";
#endif
    }

    std::optional<SCodeLldbInstall> installFromRoot(const std::filesystem::path& root, const std::string& source) {
        const auto      command_path = root / adapterRelativePath();
        const auto      liblldb_path = root / liblldbRelativePath();

        std::error_code error_code;
        if (!std::filesystem::exists(command_path, error_code) || !std::filesystem::exists(liblldb_path, error_code)) {
            return std::nullopt;
        }

        return SCodeLldbInstall{
            .command      = command_path.string(),
            .liblldb_path = liblldb_path.string(),
            .source       = source,
        };
    }

    std::vector<std::filesystem::path> platformDefaultRoots() {
        std::vector<std::filesystem::path> roots;

#if defined(_WIN32)
        if (const auto local_app_data = getEnvPath("LOCALAPPDATA"); local_app_data.has_value()) {
            roots.push_back(local_app_data.value() / "nvim-data" / "mason" / "packages" / "codelldb" / "extension");
        }
        if (const auto user_profile = getEnvPath("USERPROFILE"); user_profile.has_value()) {
            roots.push_back(user_profile.value() / ".vscode" / "extensions");
        }
#elif defined(__APPLE__)
        if (const auto home = getEnvPath("HOME"); home.has_value()) {
            roots.push_back(home.value() / "Library" / "Application Support" / "nvim" / "mason" / "packages" / "codelldb" / "extension");
            roots.push_back(home.value() / ".vscode" / "extensions");
        }
#else
        if (const auto home = getEnvPath("HOME"); home.has_value()) {
            roots.push_back(home.value() / ".local" / "share" / "nvim" / "mason" / "packages" / "codelldb" / "extension");
            roots.push_back(home.value() / ".vscode" / "extensions");
        }
#endif

        return roots;
    }

    std::optional<SCodeLldbInstall> findVsCodeExtensionInstall(const std::filesystem::path& extensions_root, const std::string& source_prefix) {
        std::error_code error_code;
        if (!std::filesystem::exists(extensions_root, error_code) || !std::filesystem::is_directory(extensions_root, error_code)) {
            return std::nullopt;
        }

        std::vector<std::filesystem::path> matches;
        for (const auto& entry : std::filesystem::directory_iterator(extensions_root, error_code)) {
            if (error_code || !entry.is_directory()) {
                continue;
            }

            const auto directory_name = entry.path().filename().string();
            if (!directory_name.starts_with("vadimcn.vscode-lldb-")) {
                continue;
            }

            matches.push_back(entry.path() / "extension");
        }

        std::ranges::sort(matches);
        std::ranges::reverse(matches);

        for (const auto& match : matches) {
            if (const auto install = installFromRoot(match, source_prefix + ":" + match.parent_path().filename().string()); install.has_value()) {
                return install;
            }
        }

        return std::nullopt;
    }

} // namespace

std::optional<SCodeLldbInstall> findCodeLldbInstall(const std::vector<std::filesystem::path>& additional_roots) {
    for (const auto& root : additional_roots) {
        if (const auto install = installFromRoot(root, "custom:" + root.string()); install.has_value()) {
            return install;
        }
    }

    for (const auto& root : platformDefaultRoots()) {
        const auto filename = root.filename().string();
        if (filename == "extensions") {
            if (const auto install = findVsCodeExtensionInstall(root, "vscode"); install.has_value()) {
                return install;
            }
            continue;
        }

        if (const auto install = installFromRoot(root, "mason:" + root.string()); install.has_value()) {
            return install;
        }
    }

    return std::nullopt;
}
