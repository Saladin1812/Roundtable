#pragma once

#include <cstddef>
#include <cstdint>

#include "memory_view.hpp"

struct SDebugSelection {
    std::int64_t thread_id   = 0;
    std::size_t  frame_index = 0;
};

struct SDebugCapabilities {
    bool supports_memory_read       = false;
    bool supports_memory_write      = false;
    bool supports_watch_expressions = false;
    bool supports_disassembly       = false;
    bool supports_data_breakpoints  = false;
};

struct SWatchExpression {
    std::string expression;
};

struct SWatchResult {
    std::string expression;
    std::string value;
    std::string type;
    std::string error_message;
};

struct SDisassemblyInstruction {
    std::uint64_t address = 0;
    std::string   mnemonic;
    std::string   operands;
    std::string   comment;
};

class IDebugSession {
  public:
    virtual ~IDebugSession() = default;

    virtual SDebugCapabilities                   getCapabilities() const                                                                                         = 0;
    virtual SMemoryReadResult                    readMemory(const SDebugSelection& selection, const SMemoryReadRequest& request) const                           = 0;
    virtual std::vector<SWatchResult>            evaluateWatches(const SDebugSelection& selection, const std::vector<SWatchExpression>& watch_expressions) const = 0;
    virtual std::vector<SDisassemblyInstruction> disassemble(const SDebugSelection& selection, std::uint64_t start_address, std::size_t instruction_count) const = 0;
};

class CMockDebugSession : public IDebugSession {
  public:
    SDebugCapabilities                   getCapabilities() const override;
    SMemoryReadResult                    readMemory(const SDebugSelection& selection, const SMemoryReadRequest& request) const override;
    std::vector<SWatchResult>            evaluateWatches(const SDebugSelection& selection, const std::vector<SWatchExpression>& watch_expressions) const override;
    std::vector<SDisassemblyInstruction> disassemble(const SDebugSelection& selection, std::uint64_t start_address, std::size_t instruction_count) const override;
};
