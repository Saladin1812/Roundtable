#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "debug_session.hpp"
#include "memory_view.hpp"

TEST_CASE("generateMemoryViewRows formats bytes into memory rows") {
    const std::vector<std::uint8_t> memory_bytes = {
        0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x57, 0x6F, 0x72, 0x6C, 0x64, 0x21, 0x00, 0x41, 0x42, 0x43, 0xDE, 0xAD, 0xBE, 0xEF,
        0x10, 0x20, 0x30, 0x40, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
    };

    const std::vector<std::string> rows = generateMemoryViewRows(0x1000, memory_bytes);

    REQUIRE(rows.size() == 5);
    CHECK(rows[0] == "0x1000  48 65 6C 6C 6F 20 57 6F  Hello Wo");
    CHECK(rows[1] == "0x1008  72 6C 64 21 00 41 42 43  rld!.ABC");
    CHECK(rows[2] == "0x1010  DE AD BE EF 10 20 30 40  ..... 0@");
    CHECK(rows[3] == "0x1018  01 02 03 04 05 06 07 08  ........");
    CHECK(rows[4] == "0x1020  FF EE DD CC BB AA 99 88  ........");
}

TEST_CASE("generateMemoryViewRows supports a partial trailing row") {
    const std::vector<std::uint8_t> memory_bytes = {
        0x41, 0x42, 0x43, 0x00, 0x44,
    };

    const std::vector<std::string> rows = generateMemoryViewRows(0x2000, memory_bytes, 4);

    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == "0x2000  41 42 43 00  ABC.");
    CHECK(rows[1] == "0x2004  44  D");
}

TEST_CASE("generateMemoryViewRows returns no rows when bytes_per_row is zero") {
    const std::vector<std::uint8_t> memory_bytes = {0x41, 0x42};

    const std::vector<std::string>  rows = generateMemoryViewRows(0x3000, memory_bytes, 0);

    CHECK(rows.empty());
}

TEST_CASE("mock memory provider returns requested byte count") {
    const CMockDebugSession debug_session   = {};
    const SDebugSelection   debug_selection = {};

    const SMemoryReadResult memory_read_result = debug_session.readMemory(debug_selection,
                                                                          {
                                                                              .start_address = 0x4000,
                                                                              .byte_count    = 16,
                                                                              .bytes_per_row = 8,
                                                                          });

    CHECK(memory_read_result.start_address == 0x4000);
    CHECK(memory_read_result.bytes_per_row == 8);
    CHECK(memory_read_result.memory_bytes.size() == 16);
    CHECK(memory_read_result.error_message.empty());
}

TEST_CASE("mock memory provider clamps requested byte count to available bytes") {
    const CMockDebugSession debug_session   = {};
    const SDebugSelection   debug_selection = {};

    const SMemoryReadResult memory_read_result = debug_session.readMemory(debug_selection,
                                                                          {
                                                                              .start_address = 0x5000,
                                                                              .byte_count    = 64,
                                                                              .bytes_per_row = 8,
                                                                          });

    CHECK(memory_read_result.memory_bytes.size() == 40);
}

TEST_CASE("generateMemoryViewRows returns provider error as a row") {
    const SMemoryReadResult memory_read_result = {
        .start_address = 0x6000,
        .memory_bytes  = {},
        .bytes_per_row = 8,
        .error_message = "Failed to read memory",
    };

    const std::vector<std::string> rows = generateMemoryViewRows(memory_read_result);

    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == "Failed to read memory");
}
