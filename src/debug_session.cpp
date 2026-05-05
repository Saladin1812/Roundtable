#include "debug_session.hpp"

#include <algorithm>
#include <vector>

SDebugCapabilities CMockDebugSession::getCapabilities() {
    return {
        .supports_memory_read       = true,
        .supports_memory_write      = false,
        .supports_watch_expressions = true,
        .supports_disassembly       = true,
        .supports_data_breakpoints  = false,
    };
}

std::vector<SLocalVariable> CMockDebugSession::getLocals(const SDebugSelection& selection) {
    static_cast<void>(selection);

    return {
        {
            .name                = "a",
            .value               = "42",
            .type                = "int",
            .memory_reference    = "0x2000",
            .variables_reference = 0,
        },
        {
            .name                = "ptr",
            .value               = "0x1000",
            .type                = "char*",
            .memory_reference    = "0x1000",
            .variables_reference = 0,
        },
    };
}

SMemoryReadResult CMockDebugSession::readMemory(const SDebugSelection& selection, const SMemoryReadRequest& request) {
    static_cast<void>(selection);

    const std::vector<std::uint8_t> pointer_memory = {
        0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x57, 0x6F, 0x72, 0x6C, 0x64, 0x21, 0x00, 0x41, 0x42, 0x43, 0xDE, 0xAD, 0xBE, 0xEF,
        0x10, 0x20, 0x30, 0x40, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
    };
    const std::vector<std::uint8_t> integer_memory = {
        0x2A, 0x00, 0x00, 0x00, 0x90, 0x91, 0x92, 0x93, 0xA0, 0xA1,
        0xA2, 0xA3, 0xB0, 0xB1, 0xB2, 0xB3, 0xC0, 0xC1, 0xC2, 0xC3,
        0xD0, 0xD1, 0xD2, 0xD3, 0xE0, 0xE1, 0xE2, 0xE3, 0xF0, 0xF1,
        0xF2, 0xF3, 0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23,
    };
    const std::vector<std::uint8_t> pointer_slot_memory = {
        0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    std::vector<std::uint8_t> selected_memory;
    if (request.start_address == 0x2000) {
        selected_memory = integer_memory;
    } else if (request.start_address == 0x3000) {
        selected_memory = pointer_slot_memory;
    } else {
        selected_memory = pointer_memory;
    }

    const std::size_t clamped_byte_count = std::min(request.byte_count, selected_memory.size());

    return {
        .start_address = request.start_address,
        .memory_bytes  = std::vector<std::uint8_t>(selected_memory.begin(), selected_memory.begin() + clamped_byte_count),
        .bytes_per_row = request.bytes_per_row,
        .error_message = "",
    };
}

std::vector<SWatchResult> CMockDebugSession::evaluateWatches(const SDebugSelection& selection, const std::vector<SWatchExpression>& watch_expressions) {
    static_cast<void>(selection);

    std::vector<SWatchResult> watch_results;
    watch_results.reserve(watch_expressions.size());

    for (const auto& watch_expression : watch_expressions) {
        if (watch_expression.expression == "a") {
            watch_results.push_back({
                .expression       = watch_expression.expression,
                .value            = "42",
                .type             = "int",
                .memory_reference = "0x2000",
                .error_message    = "",
            });
            continue;
        }

        if (watch_expression.expression == "&a") {
            watch_results.push_back({
                .expression       = watch_expression.expression,
                .value            = "0x2000",
                .type             = "int*",
                .memory_reference = "0x2000",
                .error_message    = "",
            });
            continue;
        }

        if (watch_expression.expression == "ptr") {
            watch_results.push_back({
                .expression       = watch_expression.expression,
                .value            = "0x1000",
                .type             = "char*",
                .memory_reference = "0x1000",
                .error_message    = "",
            });
            continue;
        }

        if (watch_expression.expression == "sample_value") {
            watch_results.push_back({
                .expression       = watch_expression.expression,
                .value            = "42",
                .type             = "int",
                .memory_reference = "0x2000",
                .error_message    = "",
            });
            continue;
        }

        if (watch_expression.expression == "sample_bytes") {
            watch_results.push_back({
                .expression       = watch_expression.expression,
                .value            = R"({_M_elems:"Hello!\0A"})",
                .type             = "std::array<unsigned char, 8>",
                .memory_reference = "",
                .error_message    = "",
            });
            continue;
        }

        if (watch_expression.expression == "&ptr") {
            watch_results.push_back({
                .expression       = watch_expression.expression,
                .value            = "0x3000",
                .type             = "char**",
                .memory_reference = "0x3000",
                .error_message    = "",
            });
            continue;
        }

        watch_results.push_back({
            .expression       = watch_expression.expression,
            .value            = "",
            .type             = "",
            .memory_reference = "",
            .error_message    = "Expression could not be evaluated",
        });
    }

    return watch_results;
}

std::vector<SDisassemblyInstruction> CMockDebugSession::disassemble(const SDebugSelection& selection, std::uint64_t start_address, std::size_t instruction_count) {
    static_cast<void>(selection);

    static const std::vector<SDisassemblyInstruction> mock_instructions = {
        {
            .address  = 0x401000,
            .mnemonic = "push",
            .operands = "rbp",
            .comment  = "",
        },
        {
            .address  = 0x401001,
            .mnemonic = "mov",
            .operands = "rbp, rsp",
            .comment  = "",
        },
        {
            .address  = 0x401004,
            .mnemonic = "mov",
            .operands = "eax, 42",
            .comment  = "",
        },
        {
            .address  = 0x401009,
            .mnemonic = "ret",
            .operands = "",
            .comment  = "",
        },
    };

    std::vector<SDisassemblyInstruction> instructions;

    for (const auto& instruction : mock_instructions) {
        if (instruction.address < start_address) {
            continue;
        }

        instructions.push_back(instruction);
        if (instructions.size() == instruction_count) {
            break;
        }
    }

    return instructions;
}
