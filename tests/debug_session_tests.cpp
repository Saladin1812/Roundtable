#include <catch2/catch_test_macros.hpp>

#include "debug_session.hpp"

TEST_CASE("mock debug session exposes memory read capability") {
    const CMockDebugSession  debug_session = {};

    const SDebugCapabilities capabilities = debug_session.getCapabilities();

    CHECK(capabilities.supports_memory_read);
    CHECK_FALSE(capabilities.supports_memory_write);
    CHECK_FALSE(capabilities.supports_watch_expressions);
    CHECK_FALSE(capabilities.supports_disassembly);
    CHECK_FALSE(capabilities.supports_data_breakpoints);
}

TEST_CASE("mock debug session reads memory for the current selection") {
    const CMockDebugSession debug_session   = {};
    const SDebugSelection   debug_selection = {
          .thread_id   = 1,
          .frame_index = 0,
    };

    const SMemoryReadResult memory_read_result = debug_session.readMemory(debug_selection,
                                                                          {
                                                                              .start_address = 0x7000,
                                                                              .byte_count    = 8,
                                                                              .bytes_per_row = 8,
                                                                          });

    CHECK(memory_read_result.start_address == 0x7000);
    REQUIRE(memory_read_result.memory_bytes.size() == 8);
    CHECK(memory_read_result.error_message.empty());
}
