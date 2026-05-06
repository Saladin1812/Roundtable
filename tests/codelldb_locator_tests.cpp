#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "codelldb_locator.hpp"

TEST_CASE("findCodeLldbInstall detects a valid install from additional roots") {
    const auto base_path   = std::filesystem::temp_directory_path() / "roundtable-codelldb-locator-test";
    const auto install_root = base_path / "extension";
    const auto adapter_path = install_root / "adapter" /
#if defined(_WIN32)
        "codelldb.exe";
#else
        "codelldb";
#endif
    const auto liblldb_path = install_root /
#if defined(_WIN32)
        std::filesystem::path("lldb") / "bin" / "liblldb.dll";
#elif defined(__APPLE__)
        std::filesystem::path("lldb") / "lib" / "liblldb.dylib";
#else
        std::filesystem::path("lldb") / "lib" / "liblldb.so";
#endif

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

    const auto install = findCodeLldbInstall({install_root});

    REQUIRE(install.has_value());
    CHECK(install->command == adapter_path.string());
    CHECK(install->liblldb_path == liblldb_path.string());

    std::filesystem::remove_all(base_path);
}
