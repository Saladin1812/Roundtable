#pragma once

#include <string>

#include "debug_session.hpp"
#include "debugger_controller.hpp"

std::string formatStoppedContextStatus(const SStoppedContext& stopped_context, const SDebugSelection& selection);
