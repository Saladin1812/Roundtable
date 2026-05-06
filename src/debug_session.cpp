#include "debug_session.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace {

    constexpr std::uint64_t   POINTER_MEMORY_BASE      = 0x1000;
    constexpr std::uint64_t   INTEGER_MEMORY_BASE      = 0x2000;
    constexpr std::uint64_t   POINTER_SLOT_MEMORY_BASE = 0x3000;

    std::vector<std::uint8_t> readMockMemoryRange(std::uint64_t start_address, std::size_t byte_count) {
        const std::vector<std::uint8_t> pointer_memory = {
            0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x57, 0x6F, 0x72, 0x6C, 0x64, 0x21, 0x00, 0x41, 0x42, 0x43, 0xDE, 0xAD, 0xBE, 0xEF,
            0x10, 0x20, 0x30, 0x40, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
        };
        const std::vector<std::uint8_t> integer_memory = {
            0x2A, 0x00, 0x00, 0x00, 0x90, 0x91, 0x92, 0x93, 0xA0, 0xA1, 0xA2, 0xA3, 0xB0, 0xB1, 0xB2, 0xB3, 0xC0, 0xC1, 0xC2, 0xC3,
            0xD0, 0xD1, 0xD2, 0xD3, 0xE0, 0xE1, 0xE2, 0xE3, 0xF0, 0xF1, 0xF2, 0xF3, 0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23,
        };
        const std::array<std::uint8_t, 8> pointer_slot_memory = {
            0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        };

        const auto range_end               = start_address + byte_count;
        const bool overlaps_pointer_memory = start_address < POINTER_MEMORY_BASE + pointer_memory.size() && range_end > POINTER_MEMORY_BASE;
        const bool overlaps_integer_memory = start_address < INTEGER_MEMORY_BASE + integer_memory.size() && range_end > INTEGER_MEMORY_BASE;
        const bool overlaps_pointer_slot   = start_address < POINTER_SLOT_MEMORY_BASE + pointer_slot_memory.size() && range_end > POINTER_SLOT_MEMORY_BASE;

        if (!overlaps_pointer_memory && !overlaps_integer_memory && !overlaps_pointer_slot) {
            const std::size_t clamped_byte_count = std::min(byte_count, pointer_memory.size());
            return std::vector<std::uint8_t>(pointer_memory.begin(), pointer_memory.begin() + static_cast<std::ptrdiff_t>(clamped_byte_count));
        }

        std::vector<std::uint8_t> bytes(byte_count, 0xCC);

        for (std::size_t index = 0; index < byte_count; ++index) {
            const std::uint64_t address = start_address + index;

            if (address >= POINTER_MEMORY_BASE && address < POINTER_MEMORY_BASE + pointer_memory.size()) {
                bytes[index] = pointer_memory[static_cast<std::size_t>(address - POINTER_MEMORY_BASE)];
                continue;
            }

            if (address >= INTEGER_MEMORY_BASE && address < INTEGER_MEMORY_BASE + integer_memory.size()) {
                bytes[index] = integer_memory[static_cast<std::size_t>(address - INTEGER_MEMORY_BASE)];
                continue;
            }

            if (address >= POINTER_SLOT_MEMORY_BASE && address < POINTER_SLOT_MEMORY_BASE + pointer_slot_memory.size()) {
                bytes[index] = pointer_slot_memory[static_cast<std::size_t>(address - POINTER_SLOT_MEMORY_BASE)];
            }
        }

        return bytes;
    }

} // namespace

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
            .memory_reference    = "0x3000",
            .variables_reference = 0,
        },
    };
}

SMemoryReadResult CMockDebugSession::readMemory(const SDebugSelection& selection, const SMemoryReadRequest& request) {
    static_cast<void>(selection);

    return {
        .start_address = request.start_address,
        .memory_bytes  = readMockMemoryRange(request.start_address, request.byte_count),
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
                .memory_reference = "0x3000",
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
