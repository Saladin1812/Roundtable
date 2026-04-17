#include <catch2/catch_test_macros.hpp>

#include "pane_rows.hpp"

TEST_CASE("formatLocalsPaneRows formats local variables for the locals pane") {
    const std::vector<SLocalVariable> locals = {
        {
            .name  = "a",
            .value = "42",
            .type  = "int",
        },
        {
            .name  = "ptr",
            .value = "0x1000",
            .type  = "char*",
        },
    };

    const std::vector<std::string> rows = formatLocalsPaneRows(locals);

    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == "a : int = 42");
    CHECK(rows[1] == "ptr : char* = 0x1000");
}

TEST_CASE("formatWatchListPaneRows formats watch results and errors") {
    const std::vector<SWatchResult> watch_results = {
        {
            .expression    = "a",
            .value         = "42",
            .type          = "int",
            .error_message = "",
        },
        {
            .expression    = "missing_value",
            .value         = "",
            .type          = "",
            .error_message = "Expression could not be evaluated",
        },
    };

    const std::vector<std::string> rows = formatWatchListPaneRows(watch_results);

    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == "a = 42 : int");
    CHECK(rows[1] == "missing_value : Expression could not be evaluated");
}
