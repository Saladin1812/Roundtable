#include <catch2/catch_test_macros.hpp>

#include "debug_session.hpp"

TEST_CASE("mock debug session exposes memory read capability") {
    CMockDebugSession        debug_session = {};

    const SDebugCapabilities capabilities = debug_session.getCapabilities();

    CHECK(capabilities.supports_memory_read);
    CHECK_FALSE(capabilities.supports_memory_write);
    CHECK(capabilities.supports_watch_expressions);
    CHECK(capabilities.supports_disassembly);
    CHECK_FALSE(capabilities.supports_data_breakpoints);
}

TEST_CASE("mock debug session reads memory for the current selection") {
    CMockDebugSession     debug_session   = {};
    const SDebugSelection debug_selection = {
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

TEST_CASE("mock debug session returns locals for the current selection") {
    CMockDebugSession     debug_session   = {};
    const SDebugSelection debug_selection = {
        .thread_id   = 1,
        .frame_index = 0,
    };

    const std::vector<SLocalVariable> locals = debug_session.getLocals(debug_selection);

    REQUIRE(locals.size() == 2);
    CHECK(locals[0].name == "a");
    CHECK(locals[0].value == "42");
    CHECK(locals[0].type == "int");
    CHECK(locals[1].name == "ptr");
    CHECK(locals[1].value == "0x1000");
    CHECK(locals[1].type == "char*");
}

TEST_CASE("mock debug session evaluates watch expressions") {
    CMockDebugSession                   debug_session     = {};
    const SDebugSelection               debug_selection   = {};
    const std::vector<SWatchExpression> watch_expressions = {
        {.expression = "a"},
        {.expression = "ptr"},
        {.expression = "missing_value"},
    };

    const std::vector<SWatchResult> watch_results = debug_session.evaluateWatches(debug_selection, watch_expressions);

    REQUIRE(watch_results.size() == 3);
    CHECK(watch_results[0].value == "42");
    CHECK(watch_results[0].type == "int");
    CHECK(watch_results[0].error_message.empty());
    CHECK(watch_results[1].value == "0x1000");
    CHECK(watch_results[1].type == "char*");
    CHECK(watch_results[2].error_message == "Expression could not be evaluated");
}

TEST_CASE("mock debug session returns disassembly from a start address") {
    CMockDebugSession                          debug_session   = {};
    const SDebugSelection                      debug_selection = {};

    const std::vector<SDisassemblyInstruction> instructions = debug_session.disassemble(debug_selection, 0x401001, 2);

    REQUIRE(instructions.size() == 2);
    CHECK(instructions[0].address == 0x401001);
    CHECK(instructions[0].mnemonic == "mov");
    CHECK(instructions[0].operands == "rbp, rsp");
    CHECK(instructions[1].address == 0x401004);
    CHECK(instructions[1].mnemonic == "mov");
    CHECK(instructions[1].operands == "eax, 42");
}
