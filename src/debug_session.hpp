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

class IDebugSession {
  public:
    virtual ~IDebugSession() = default;

    virtual SDebugCapabilities getCapabilities() const                                                               = 0;
    virtual SMemoryReadResult  readMemory(const SDebugSelection& selection, const SMemoryReadRequest& request) const = 0;
};

class CMockDebugSession : public IDebugSession {
  public:
    SDebugCapabilities getCapabilities() const override;
    SMemoryReadResult  readMemory(const SDebugSelection& selection, const SMemoryReadRequest& request) const override;
};
