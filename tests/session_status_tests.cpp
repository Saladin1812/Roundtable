#include <catch2/catch_test_macros.hpp>

#include "session_status.hpp"

TEST_CASE("formatStoppedContextStatus includes selected thread frame and location") {
    const SStoppedContext stopped_context = {
        .location = {.function_name = "fallback", .source_path = "/tmp/fallback.cpp", .line = 3, .column = 1},
        .threads  = {{.id = 7, .name = "main thread"}},
        .stack_frames =
            {
                {.function_name = "main", .source_path = "/tmp/sample.cpp", .line = 42, .column = 5},
                {.function_name = "helper", .source_path = "/tmp/helper.cpp", .line = 9, .column = 2},
            },
    };

    const SDebugSelection selection = {
        .thread_id   = 7,
        .frame_index = 1,
    };

    CHECK(formatStoppedContextStatus(stopped_context, selection) == "Frame T:7 main thread F:1 helper.cpp:9:2 helper()");
}

TEST_CASE("formatStoppedContextStatus handles missing thread and location") {
    const SStoppedContext stopped_context = {};
    const SDebugSelection selection       = {};

    CHECK(formatStoppedContextStatus(stopped_context, selection) == "Frame T:n/a F:0 location n/a");
}
