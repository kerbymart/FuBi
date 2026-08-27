#pragma once

#include "CallContract.h"

#include <array>
#include <cstdint>
#include <string>

// The adapter boundary carries normalized values only. It has no knowledge of
// CLI text, catalog records, or prototype inference.
struct NativeCallFrameX64
{
    uintptr_t targetAddress = 0;
    std::array<uintptr_t, 8> arguments{};
    uint32_t argumentCount = 0;
};

bool InvokeNativeCallX64(const NativeCallFrameX64& frame, uintptr_t& returned,
    std::string& error);

// Performs the only target-loading operation in this milestone. All request
// validation and static identity checks must succeed before this function.
bool InvokeX64Export(const std::string& imagePath, const CallRequest& request,
    const FunctionCatalog& catalog, CallResult& result, std::string& error);
