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
    uint32_t floatingArgumentMask = 0;
    uint32_t argumentCount = 0;
    bool floatingReturn = false;
    uint64_t floatingReturnBits = 0;
};

bool InvokeNativeCallX64(NativeCallFrameX64& frame, uintptr_t& returned,
    std::string& error);

// Normalized x86 frame. The adapter owns convention-specific register
// placement and stack cleanup; callers provide only machine-word values.
enum class X86CallingConvention : uint8_t
{
    Cdecl,
    Stdcall,
    Thiscall,
    Fastcall
};

struct NativeCallFrameX86
{
    uintptr_t targetAddress = 0;
    std::array<uintptr_t, 8> arguments{};
    uint32_t argumentCount = 0;
    uint32_t stackBytes = 0;
    X86CallingConvention convention = X86CallingConvention::Cdecl;
    uint64_t returned = 0;
};

bool InvokeNativeCallX86(NativeCallFrameX86& frame, std::string& error);

// Performs the only target-loading operation in this milestone. All request
// validation and static identity checks must succeed before this function.
bool InvokeX64Export(const std::string& imagePath, const CallRequest& request,
    const FunctionCatalog& catalog, CallResult& result, std::string& error);
