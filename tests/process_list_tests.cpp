#include <catch2/catch_test_macros.hpp>

#include "process_list.hpp"

TEST_CASE("parseUnixProcessListOutput parses pid and command pairs") {
    const auto processes = parseUnixProcessListOutput("  101 init\n2042 codelldb\n  bad line\n4096 roundtable_dap_sample\n");

    REQUIRE(processes.size() == 3);
    CHECK(processes[0].process_id == 101);
    CHECK(processes[0].name == "init");
    CHECK(processes[1].process_id == 2042);
    CHECK(processes[1].name == "codelldb");
    CHECK(processes[2].process_id == 4096);
    CHECK(processes[2].name == "roundtable_dap_sample");
}

TEST_CASE("parseWindowsProcessListOutput parses csv tasklist rows") {
    const auto processes = parseWindowsProcessListOutput("\"CodeLLDB.exe\",\"2042\",\"Console\",\"1\",\"12,000 K\"\n"
                                                         "\"sample.exe\",\"4096\",\"Console\",\"1\",\"8,000 K\"\n");

    REQUIRE(processes.size() == 2);
    CHECK(processes[0].process_id == 2042);
    CHECK(processes[0].name == "CodeLLDB.exe");
    CHECK(processes[1].process_id == 4096);
    CHECK(processes[1].name == "sample.exe");
}
