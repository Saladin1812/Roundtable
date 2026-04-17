#include "debug_session.hpp"

#include <algorithm>
#include <vector>

SDebugCapabilities CMockDebugSession::getCapabilities() const {
    return {
        .supports_memory_read       = true,
        .supports_memory_write      = false,
        .supports_watch_expressions = true,
        .supports_disassembly       = true,
        .supports_data_breakpoints  = false,
    };
}

SMemoryReadResult CMockDebugSession::readMemory(const SDebugSelection& selection, const SMemoryReadRequest& request) const {
    static_cast<void>(selection);

    static const std::vector<std::uint8_t> mock_memory_bytes = {
        0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x57, 0x6F, 0x72, 0x6C, 0x64, 0x21, 0x00, 0x41, 0x42, 0x43, 0xDE, 0xAD, 0xBE, 0xEF,
        0x10, 0x20, 0x30, 0x40, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
    };

    const std::size_t clamped_byte_count = std::min(request.byte_count, mock_memory_bytes.size());

    return {
        .start_address = request.start_address,
        .memory_bytes  = std::vector<std::uint8_t>(mock_memory_bytes.begin(), mock_memory_bytes.begin() + clamped_byte_count),
        .bytes_per_row = request.bytes_per_row,
        .error_message = "",
    };
}

std::vector<SWatchResult> CMockDebugSession::evaluateWatches(const SDebugSelection& selection, const std::vector<SWatchExpression>& watch_expressions) const {
    static_cast<void>(selection);

    std::vector<SWatchResult> watch_results;
    watch_results.reserve(watch_expressions.size());

    for (const auto& watch_expression : watch_expressions) {
        if (watch_expression.expression == "a") {
            watch_results.push_back({
                .expression    = watch_expression.expression,
                .value         = "42",
                .type          = "int",
                .error_message = "",
            });
            continue;
        }

        if (watch_expression.expression == "ptr") {
            watch_results.push_back({
                .expression    = watch_expression.expression,
                .value         = "0x1000",
                .type          = "char*",
                .error_message = "",
            });
            continue;
        }

        watch_results.push_back({
            .expression    = watch_expression.expression,
            .value         = "",
            .type          = "",
            .error_message = "Expression could not be evaluated",
        });
    }

    return watch_results;
}

std::vector<SDisassemblyInstruction> CMockDebugSession::disassemble(const SDebugSelection& selection, std::uint64_t start_address, std::size_t instruction_count) const {
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
