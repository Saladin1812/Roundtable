#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

#include "codelldb_locator.hpp"

namespace {

    class CScopedEnvironmentVariable {
      public:
        CScopedEnvironmentVariable(std::string name, std::string value) : name_(std::move(name)), previous_value_(readCurrentValue(name_)) {
            setValue(value);
        }

        CScopedEnvironmentVariable(const CScopedEnvironmentVariable&)            = delete;
        CScopedEnvironmentVariable& operator=(const CScopedEnvironmentVariable&) = delete;

        ~CScopedEnvironmentVariable() {
            if (previous_value_.has_value()) {
                setValue(previous_value_.value());
            } else {
                unsetValue();
            }
        }

      private:
        static std::optional<std::string> readCurrentValue(const std::string& name) {
            const char* value = std::getenv(name.c_str());
            if (value == nullptr) {
                return std::nullopt;
            }

            return std::string(value);
        }

        void setValue(const std::string& value) const {
#if defined(_WIN32)
            _putenv_s(name_.c_str(), value.c_str());
#else
            setenv(name_.c_str(), value.c_str(), 1);
#endif
        }

        void unsetValue() const {
#if defined(_WIN32)
            _putenv_s(name_.c_str(), "");
#else
            unsetenv(name_.c_str());
#endif
        }

        std::string                name_;
        std::optional<std::string> previous_value_;
    };

    std::filesystem::path testAdapterPath(const std::filesystem::path& install_root) {
        return install_root / "adapter" /
#if defined(_WIN32)
            "codelldb.exe";
#else
            "codelldb";
#endif
    }

    std::filesystem::path testLibLldbPath(const std::filesystem::path& install_root) {
        return install_root /
#if defined(_WIN32)
            std::filesystem::path("lldb") / "bin" / "liblldb.dll";
#elif defined(__APPLE__)
            std::filesystem::path("lldb") / "lib" / "liblldb.dylib";
#else
            std::filesystem::path("lldb") / "lib" / "liblldb.so";
#endif
    }

    void createTestInstall(const std::filesystem::path& install_root) {
        const auto adapter_path = testAdapterPath(install_root);
        const auto liblldb_path = testLibLldbPath(install_root);

        std::filesystem::create_directories(adapter_path.parent_path());
        std::filesystem::create_directories(liblldb_path.parent_path());
        {
            std::ofstream adapter_stream(adapter_path);
            adapter_stream << "adapter";
        }
        {
            std::ofstream liblldb_stream(liblldb_path);
            liblldb_stream << "liblldb";
        }
    }

} // namespace

TEST_CASE("findCodeLldbInstall detects a valid install from additional roots") {
    const auto base_path    = std::filesystem::temp_directory_path() / "roundtable-codelldb-locator-test";
    const auto install_root = base_path / "extension";

    createTestInstall(install_root);

    const auto install = findCodeLldbInstall({install_root});

    REQUIRE(install.has_value());
    CHECK(install->command == testAdapterPath(install_root).string());
    CHECK(install->liblldb_path == testLibLldbPath(install_root).string());

    std::filesystem::remove_all(base_path);
}

TEST_CASE("findCodeLldbInstall detects a valid install from PATH as a fallback") {
    const auto base_path    = std::filesystem::temp_directory_path() / "roundtable-codelldb-path-locator-test";
    const auto install_root = base_path / "extension";

    createTestInstall(install_root);

    const CScopedEnvironmentVariable scoped_home("HOME", (base_path / "empty-home").string());
    const CScopedEnvironmentVariable scoped_local_app_data("LOCALAPPDATA", (base_path / "empty-local-app-data").string());
    const CScopedEnvironmentVariable scoped_user_profile("USERPROFILE", (base_path / "empty-user-profile").string());
    const CScopedEnvironmentVariable scoped_path("PATH", (install_root / "adapter").string());

    const auto                       install = findCodeLldbInstall();

    REQUIRE(install.has_value());
    CHECK(install->command == testAdapterPath(install_root).string());
    CHECK(install->liblldb_path == testLibLldbPath(install_root).string());
    CHECK(install->source.rfind("path:", 0) == 0);

    std::filesystem::remove_all(base_path);
}
