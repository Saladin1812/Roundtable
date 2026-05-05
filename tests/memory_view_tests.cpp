#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "debug_session.hpp"
#include "memory_selection.hpp"
#include "memory_view.hpp"

namespace {

    class CArrayAddressTestSession : public IDebugSession {
      public:
        SDebugCapabilities getCapabilities() override {
            return {};
        }

        std::vector<SLocalVariable> getLocals(const SDebugSelection& selection) override {
            static_cast<void>(selection);
            return {};
        }

        SMemoryReadResult readMemory(const SDebugSelection& selection, const SMemoryReadRequest& request) override {
            static_cast<void>(selection);
            static_cast<void>(request);
            return {};
        }

        std::vector<SWatchResult> evaluateWatches(const SDebugSelection& selection, const std::vector<SWatchExpression>& watch_expressions) override {
            static_cast<void>(selection);

            std::vector<SWatchResult> results;
            for (const auto& watch_expression : watch_expressions) {
                if (watch_expression.expression == "&sample_bytes._M_elems[0]") {
                    results.push_back({
                        .expression       = watch_expression.expression,
                        .value            = "0x7000",
                        .type             = "unsigned char*",
                        .memory_reference = "0x7000",
                        .error_message    = "",
                    });
                    continue;
                }

                if (watch_expression.expression == "&sample_bytes[0]") {
                    results.push_back({
                        .expression       = watch_expression.expression,
                        .value            = "0x7000",
                        .type             = "unsigned char*",
                        .memory_reference = "0x7000",
                        .error_message    = "",
                    });
                    continue;
                }

                if (watch_expression.expression == "sample_bytes.data()") {
                    results.push_back({
                        .expression       = watch_expression.expression,
                        .value            = "pointer pretty value",
                        .type             = "unsigned char*",
                        .memory_reference = "0x7000",
                        .error_message    = "",
                    });
                    continue;
                }

                results.push_back({
                    .expression       = watch_expression.expression,
                    .value            = "",
                    .type             = "",
                    .memory_reference = "",
                    .error_message    = "missing",
                });
            }

            return results;
        }

        std::vector<SDisassemblyInstruction> disassemble(const SDebugSelection& selection, std::uint64_t start_address, std::size_t instruction_count) override {
            static_cast<void>(selection);
            static_cast<void>(start_address);
            static_cast<void>(instruction_count);
            return {};
        }
    };

} // namespace

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
    CMockDebugSession       debug_session   = {};
    const SDebugSelection   debug_selection = {};

    const SMemoryReadResult memory_read_result = debug_session.readMemory(debug_selection,
                                                                          {
                                                                              .start_address    = 0x4000,
                                                                              .memory_reference = "",
                                                                              .byte_count       = 16,
                                                                              .bytes_per_row    = 8,
                                                                          });

    CHECK(memory_read_result.start_address == 0x4000);
    CHECK(memory_read_result.bytes_per_row == 8);
    CHECK(memory_read_result.memory_bytes.size() == 16);
    CHECK(memory_read_result.memory_bytes[0] == 0x48);
    CHECK(memory_read_result.error_message.empty());
}

TEST_CASE("mock memory provider clamps requested byte count to available bytes") {
    CMockDebugSession       debug_session   = {};
    const SDebugSelection   debug_selection = {};

    const SMemoryReadResult memory_read_result = debug_session.readMemory(debug_selection,
                                                                          {
                                                                              .start_address    = 0x5000,
                                                                              .memory_reference = "",
                                                                              .byte_count       = 64,
                                                                              .bytes_per_row    = 8,
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

TEST_CASE("findFirstHexAddress extracts the first hexadecimal address from text") {
    const auto address = findFirstHexAddress("volatile std::array* = 0x7FFFABCD1234");

    REQUIRE(address.has_value());
    CHECK(address.value() == 0x7FFFABCD1234ULL);
}

TEST_CASE("buildMemoryReadRequest uses evaluated address for a selected non-pointer local") {
    CMockDebugSession                 debug_session   = {};
    const SDebugSelection             debug_selection = {};
    const std::vector<SLocalVariable> locals          = debug_session.getLocals(debug_selection);

    const SMemoryReadRequest          memory_read_request = buildMemoryReadRequest(debug_session, debug_selection, locals, 0, 0x1000);

    CHECK(memory_read_request.start_address == 0x2000);
    CHECK(memory_read_request.memory_reference == "0x2000");
    CHECK(memory_read_request.byte_count == 40);
    CHECK(memory_read_request.bytes_per_row == 8);
}

TEST_CASE("buildMemoryReadRequest uses pointer local value when available") {
    CMockDebugSession                 debug_session   = {};
    const SDebugSelection             debug_selection = {};
    const std::vector<SLocalVariable> locals          = debug_session.getLocals(debug_selection);

    const SMemoryReadRequest          memory_read_request = buildMemoryReadRequest(debug_session, debug_selection, locals, 1, 0x2000);

    CHECK(memory_read_request.start_address == 0x1000);
}

TEST_CASE("buildMemoryReadRequest uses array element address when available") {
    CArrayAddressTestSession          debug_session   = {};
    const SDebugSelection             debug_selection = {};
    const std::vector<SLocalVariable> locals          = {
        {
                     .name                = "sample_bytes",
                     .value               = "{...}",
                     .type                = "volatile std::array<unsigned char, 8>",
                     .memory_reference    = "0x7000",
                     .variables_reference = 1019,
        },
    };

    const SMemoryReadRequest memory_read_request = buildMemoryReadRequest(debug_session, debug_selection, locals, 0, 0x1000);

    CHECK(memory_read_request.start_address == 0x7000);
}

TEST_CASE("buildSyntheticMemoryRows formats bytes from an array-like local value") {
    const std::vector<SLocalVariable> locals = {
        {
            .name                = "sample_bytes",
            .value               = R"({_M_elems:"Hello!\0A"})",
            .type                = "volatile std::array<unsigned char, 8>",
            .memory_reference    = "",
            .variables_reference = 1019,
        },
    };

    const auto rows = buildSyntheticMemoryRows(locals, 0);

    REQUIRE(rows.has_value());
    REQUIRE(rows->size() == 1);
    CHECK(rows->at(0) == "<value>  48 65 6C 6C 6F 21 00 41  Hello!.A");
}

TEST_CASE("buildSyntheticMemoryRows returns no rows for non-array locals") {
    const std::vector<SLocalVariable> locals = {
        {
            .name                = "sample_value",
            .value               = "42",
            .type                = "const int",
            .memory_reference    = "",
            .variables_reference = 0,
        },
    };

    CHECK_FALSE(buildSyntheticMemoryRows(locals, 0).has_value());
}

TEST_CASE("buildMemoryByteHighlight uses integer width for selected int local") {
    const std::vector<SLocalVariable> locals = {
        {
            .name                = "a",
            .value               = "42",
            .type                = "int",
            .memory_reference    = "0x2000",
            .variables_reference = 0,
        },
    };

    const auto highlight = buildMemoryByteHighlight(locals, 0,
                                                    {
                                                        .start_address    = 0x2000,
                                                        .memory_reference = "0x2000",
                                                        .byte_count       = 40,
                                                        .bytes_per_row    = 8,
                                                    },
                                                    false);

    REQUIRE(highlight.has_value());
    CHECK_FALSE(highlight->synthetic);
    CHECK(highlight->start_address == 0x2000);
    CHECK(highlight->byte_count == 4);
}

TEST_CASE("buildMemoryByteHighlight uses parsed byte count for synthetic array rows") {
    const std::vector<SWatchResult> watch_results = {
        {
            .expression       = "sample_bytes",
            .value            = R"({_M_elems:"Hello!\0A"})",
            .type             = "volatile std::array<unsigned char, 8>",
            .memory_reference = "",
            .error_message    = "",
        },
    };

    const auto highlight = buildMemoryByteHighlight(watch_results, 0,
                                                    {
                                                        .start_address    = 0x0,
                                                        .memory_reference = "",
                                                        .byte_count       = 8,
                                                        .bytes_per_row    = 8,
                                                    },
                                                    true);

    REQUIRE(highlight.has_value());
    CHECK(highlight->synthetic);
    CHECK(highlight->byte_count == 8);
}

TEST_CASE("buildMemoryReadRequest uses watch memory reference when available") {
    const std::vector<SWatchResult> watch_results = {
        {
            .expression       = "sample_bytes",
            .value            = R"({_M_elems:"Hello!\0A"})",
            .type             = "volatile std::array<unsigned char, 8>",
            .memory_reference = "0x7000",
            .error_message    = "",
        },
    };

    const auto memory_read_request = buildMemoryReadRequest(watch_results, 0, 0x1000);

    CHECK(memory_read_request.start_address == 0x7000);
    CHECK(memory_read_request.memory_reference == "0x7000");
}

TEST_CASE("buildSyntheticMemoryRows formats bytes from an array-like watch value") {
    const std::vector<SWatchResult> watch_results = {
        {
            .expression       = "sample_bytes",
            .value            = R"({_M_elems:"Hello!\0A"})",
            .type             = "volatile std::array<unsigned char, 8>",
            .memory_reference = "",
            .error_message    = "",
        },
    };

    const auto rows = buildSyntheticMemoryRows(watch_results, 0);

    REQUIRE(rows.has_value());
    REQUIRE(rows->size() == 1);
    CHECK(rows->at(0) == "<value>  48 65 6C 6C 6F 21 00 41  Hello!.A");
}
