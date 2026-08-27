#include "stdafx.h"
#include "InvocationEngine.h"

#include <windows.h>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <vector>
#include <cstring>
#include <iomanip>

namespace
{
// In-process timeout contexts cannot be safely reclaimed while target code may
// still be running. Keep one bounded context until process isolation (issue
// #10) replaces this adapter.
std::atomic<unsigned> retainedTimeoutWorkers{0};
#if defined(_M_IX86)
using Call0 = uint64_t(*)(); using Call1 = uint64_t(*)(uintptr_t); using Call2 = uint64_t(*)(uintptr_t,uintptr_t); using Call3 = uint64_t(*)(uintptr_t,uintptr_t,uintptr_t); using Call4 = uint64_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t); using Call5 = uint64_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t); using Call6 = uint64_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t); using Call7 = uint64_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t); using Call8 = uint64_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
using StdCall0 = uint64_t (__stdcall*)(); using StdCall1 = uint64_t (__stdcall*)(uintptr_t); using StdCall2 = uint64_t (__stdcall*)(uintptr_t,uintptr_t); using StdCall3 = uint64_t (__stdcall*)(uintptr_t,uintptr_t,uintptr_t); using StdCall4 = uint64_t (__stdcall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t); using StdCall5 = uint64_t (__stdcall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t); using StdCall6 = uint64_t (__stdcall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t); using StdCall7 = uint64_t (__stdcall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t); using StdCall8 = uint64_t (__stdcall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
using ThisCall0 = uintptr_t (__thiscall*)(); using ThisCall1 = uintptr_t (__thiscall*)(uintptr_t); using ThisCall2 = uintptr_t (__thiscall*)(uintptr_t,uintptr_t); using ThisCall3 = uintptr_t (__thiscall*)(uintptr_t,uintptr_t,uintptr_t); using ThisCall4 = uintptr_t (__thiscall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t); using ThisCall5 = uintptr_t (__thiscall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t); using ThisCall6 = uintptr_t (__thiscall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t); using ThisCall7 = uintptr_t (__thiscall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t); using ThisCall8 = uintptr_t (__thiscall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
using FastCall0 = uintptr_t (__fastcall*)(); using FastCall1 = uintptr_t (__fastcall*)(uintptr_t); using FastCall2 = uintptr_t (__fastcall*)(uintptr_t,uintptr_t); using FastCall3 = uintptr_t (__fastcall*)(uintptr_t,uintptr_t,uintptr_t); using FastCall4 = uintptr_t (__fastcall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t); using FastCall5 = uintptr_t (__fastcall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t); using FastCall6 = uintptr_t (__fastcall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t); using FastCall7 = uintptr_t (__fastcall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t); using FastCall8 = uintptr_t (__fastcall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);

bool ParseX86CallingConvention(const std::string& name, X86CallingConvention& value)
{
    if (name == "__cdecl") value = X86CallingConvention::Cdecl;
    else if (name == "__stdcall") value = X86CallingConvention::Stdcall;
    else if (name == "__thiscall") value = X86CallingConvention::Thiscall;
    else if (name == "__fastcall") value = X86CallingConvention::Fastcall;
    else return false;
    return true;
}
#endif

#if defined(_M_X64)
extern "C" void NativeCallX64(uintptr_t targetAddress,
    const uintptr_t* arguments, uint32_t argumentCount, uint32_t floatingArgumentMask,
    uintptr_t* returned, uint64_t* floatingReturned, bool floatingReturn);
#endif

bool Number(const std::string& text, int base, uint64_t& value)
{ if(text.empty()) return false; errno=0; char* end=nullptr; const unsigned long long parsed=std::strtoull(text.c_str(),&end,base); if(errno==ERANGE||end==text.c_str()||*end!='\0') return false; value=static_cast<uint64_t>(parsed); return true; }

bool Value(const CallArgument& argument, uint64_t& value)
{
    if (argument.type.kind == TypeKind::Bool) { if (argument.value == "false") { value=0; return true; } if (argument.value == "true") { value=1; return true; } return false; }
    if (argument.type.kind == TypeKind::Pointer || argument.type.kind == TypeKind::Handle) { if (argument.value.rfind("opaque:", 0) != 0) return false; return Number(argument.value.substr(7), 0, value); }
    if (argument.type.isSigned) { if (argument.value.empty()) return false; errno=0; char* end=nullptr; const long long parsed=std::strtoll(argument.value.c_str(),&end,0); if(errno==ERANGE||end==argument.value.c_str()||*end!='\0') return false; value=static_cast<uint64_t>(parsed); return true; }
    return Number(argument.value, 0, value);
}

bool FloatingValue(const CallArgument& argument, uint64_t& bits)
{
    if (argument.type.width != 32 && argument.type.width != 64) return false;
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(argument.value.c_str(), &end);
    if (errno == ERANGE || end == argument.value.c_str() || *end != '\0' || !std::isfinite(value)) return false;
    if (argument.type.width == 32)
    {
        const float narrow = static_cast<float>(value);
        if (!std::isfinite(narrow)) return false;
        uint32_t raw = 0;
        std::memcpy(&raw, &narrow, sizeof(raw));
        bits = raw;
    }
    else std::memcpy(&bits, &value, sizeof(bits));
    return true;
}

bool DecodeHex(const std::string& text, std::vector<unsigned char>& bytes)
{
    if (text.size() % 2 != 0 || text.size() / 2 > 16 * 1024 * 1024) return false;
    bytes.clear();
    bytes.reserve(text.size() / 2);
    for (size_t i = 0; i < text.size(); i += 2)
    {
        const auto nibble = [](char value) -> int {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'a' && value <= 'f') return value - 'a' + 10;
            if (value >= 'A' && value <= 'F') return value - 'A' + 10;
            return -1;
        };
        const int high = nibble(text[i]);
        const int low = nibble(text[i + 1]);
        if (high < 0 || low < 0) return false;
        bytes.push_back(static_cast<unsigned char>((high << 4) | low));
    }
    return true;
}

std::string EncodeHex(const std::vector<unsigned char>& bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (unsigned char value : bytes)
    {
        result.push_back(digits[value >> 4]);
        result.push_back(digits[value & 0x0f]);
    }
    return result;
}

std::string EncodeString(const std::vector<unsigned char>& bytes, const TypeSpec& type)
{
    if (type.encoding == "utf16" || type.encoding == "wstr")
    {
        const size_t units = bytes.size() / sizeof(wchar_t);
        size_t length = 0;
        while (length < units && reinterpret_cast<const wchar_t*>(bytes.data())[length] != L'\0') ++length;
        if (length > static_cast<size_t>(INT_MAX)) return {};
        const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            reinterpret_cast<const wchar_t*>(bytes.data()), static_cast<int>(length), nullptr, 0, nullptr, nullptr);
        if (required <= 0 && length != 0) return {};
        std::string result(static_cast<size_t>(required), '\0');
        if (required != 0 && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            reinterpret_cast<const wchar_t*>(bytes.data()), static_cast<int>(length), result.data(), required, nullptr, nullptr) != required)
            return {};
        return result;
    }
    const size_t length = std::find(bytes.begin(), bytes.end(), static_cast<unsigned char>(0)) - bytes.begin();
    return std::string(bytes.begin(), bytes.begin() + length);
}

bool PrepareStorage(const CallArgument& argument, std::vector<unsigned char>& storage,
    std::string& error)
{
    if (argument.type.kind == TypeKind::String)
    {
        const bool wide = argument.type.encoding == "utf16" || argument.type.encoding == "wstr";
        if (argument.type.direction != ParameterDirection::In && argument.bufferSize == 0)
        { error = "string output requires an explicit buffer size"; return false; }
        if (argument.type.direction == ParameterDirection::Out)
        {
            if (wide && argument.bufferSize % sizeof(wchar_t) != 0)
            { error = "UTF-16 string buffer size must be a multiple of two"; return false; }
            storage.assign(static_cast<size_t>(argument.bufferSize), 0);
            return storage.size() <= 16 * 1024 * 1024;
        }
        if (argument.type.encoding == "utf16" || argument.type.encoding == "wstr")
        {
            const int required = argument.value.empty() ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                argument.value.data(), static_cast<int>(argument.value.size()), nullptr, 0);
            if (!argument.value.empty() && required <= 0) { error = "string is not valid UTF-8"; return false; }
            storage.resize((static_cast<size_t>(required) + 1) * sizeof(wchar_t));
            if (required != 0 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argument.value.data(),
                static_cast<int>(argument.value.size()), reinterpret_cast<wchar_t*>(storage.data()), required) != required)
            { error = "unable to convert UTF-8 string"; return false; }
        }
        else if (argument.type.encoding == "cstr" || argument.type.encoding == "utf8")
        {
            if (argument.value.find('\0') != std::string::npos) { error = "narrow string contains an embedded NUL"; return false; }
            storage.assign(argument.value.begin(), argument.value.end());
            storage.push_back(0);
        }
        else { error = "string encoding must be cstr, utf8, utf16, or wstr"; return false; }
        if (argument.bufferSize != 0 && storage.size() > argument.bufferSize)
        {
            error = "string value exceeds its buffer size";
            return false;
        }
        if (argument.type.direction == ParameterDirection::InOut)
        {
            if (wide && argument.bufferSize % sizeof(wchar_t) != 0)
            { error = "UTF-16 string buffer size must be a multiple of two"; return false; }
            storage.resize(static_cast<size_t>(argument.bufferSize), 0);
        }
        return storage.size() <= 16 * 1024 * 1024;
    }
    if (argument.type.kind != TypeKind::Bytes)
    {
        error = "storage is only valid for strings and byte buffers";
        return false;
    }
    if (!DecodeHex(argument.value, storage)) { error = "byte buffer value must be hexadecimal"; return false; }
    if (argument.type.direction == ParameterDirection::Out)
    {
        if (argument.bufferSize == 0) { error = "output buffer size is required"; return false; }
        storage.assign(static_cast<size_t>(argument.bufferSize), 0);
    }
    else if (argument.type.direction == ParameterDirection::InOut)
    {
        if (argument.bufferSize == 0) { error = "inout buffer size is required"; return false; }
        if (storage.size() > argument.bufferSize) { error = "inout value exceeds its buffer size"; return false; }
        storage.resize(static_cast<size_t>(argument.bufferSize), 0);
    }
    else if (argument.bufferSize != 0 && storage.size() > argument.bufferSize)
    { error = "input value exceeds its buffer size"; return false; }
    return storage.size() <= 16 * 1024 * 1024;
}

std::string ReturnValue(uint64_t value, const TypeSpec& type)
{
    if (type.kind == TypeKind::Bool) return value ? "true" : "false";
    if (type.kind == TypeKind::Pointer || type.kind == TypeKind::Handle) return "opaque:0x" + [&] { std::ostringstream out; out << std::hex << value; return out.str(); }();
    if (type.width > 0 && type.width < 64)
    {
        const uint64_t mask = (uint64_t(1) << type.width) - 1;
        value &= mask;
        if (type.isSigned)
        {
            const uint64_t sign = uint64_t(1) << (type.width - 1);
            if (value & sign) value |= ~mask;
        }
    }
    if (type.isSigned) return std::to_string(static_cast<int64_t>(value));
    return std::to_string(value);
}

std::string FloatingReturnValue(uint64_t bits, const TypeSpec& type)
{
    std::ostringstream output;
    output << std::setprecision(type.width == 32 ? 9 : 17);
    if (type.width == 32)
    {
        const uint32_t raw = static_cast<uint32_t>(bits);
        float value = 0.0f;
        std::memcpy(&value, &raw, sizeof(value));
        output << value;
    }
    else
    {
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        output << value;
    }
    return output.str();
}

bool SameIdentity(const ModuleIdentity& left, const ModuleIdentity& right)
{ return left.canonicalPath==right.canonicalPath && left.sha256==right.sha256 && left.architecture==right.architecture && left.timestamp==right.timestamp && left.imageSize==right.imageSize && left.preferredImageBase==right.preferredImageBase && left.pdbGuid==right.pdbGuid && left.pdbAge==right.pdbAge; }

struct CallContext
{
#if defined(_M_X64)
    NativeCallFrameX64 frame;
#else
    FARPROC address;
    const char* abi;
    const uintptr_t* values;
    size_t count;
    uint64_t returned;
    NativeCallFrameX86 frame;
    std::string frameError;
#endif
    int exceptionCode;
};

struct WorkerState
{
    HMODULE module;
    std::vector<std::vector<unsigned char>> argumentStorage;
#if defined(_M_IX86)
    std::string abi;
    uintptr_t values[8];
#endif
    CallContext call;
};

DWORD WINAPI CallWorker(void* raw)
{
    CallContext* context=static_cast<CallContext*>(raw);
    __try {
#if defined(_M_X64)
        NativeCallX64(context->frame.targetAddress, context->frame.arguments.data(),
            context->frame.argumentCount, context->frame.floatingArgumentMask,
            &context->frame.targetAddress, &context->frame.floatingReturnBits,
            context->frame.floatingReturn);
#else
#if defined(_M_IX86)
        if (!InvokeNativeCallX86(context->frame, context->frameError))
            context->exceptionCode = ERROR_INVALID_PARAMETER;
        else
            context->returned = context->frame.returned;
        return 0;
        if (std::strcmp(context->abi, "__stdcall") == 0)
        {
            switch(context->count){case 0:context->returned=reinterpret_cast<StdCall0>(context->address)();break;case 1:context->returned=reinterpret_cast<StdCall1>(context->address)(context->values[0]);break;case 2:context->returned=reinterpret_cast<StdCall2>(context->address)(context->values[0],context->values[1]);break;case 3:context->returned=reinterpret_cast<StdCall3>(context->address)(context->values[0],context->values[1],context->values[2]);break;case 4:context->returned=reinterpret_cast<StdCall4>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3]);break;case 5:context->returned=reinterpret_cast<StdCall5>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4]);break;case 6:context->returned=reinterpret_cast<StdCall6>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4],context->values[5]);break;case 7:context->returned=reinterpret_cast<StdCall7>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4],context->values[5],context->values[6]);break;default:context->returned=reinterpret_cast<StdCall8>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4],context->values[5],context->values[6],context->values[7]);break;}
        }
        else if (std::strcmp(context->abi, "__thiscall") == 0)
        {
            switch(context->count){case 1:context->returned=reinterpret_cast<ThisCall1>(context->address)(context->values[0]);break;case 2:context->returned=reinterpret_cast<ThisCall2>(context->address)(context->values[0],context->values[1]);break;case 3:context->returned=reinterpret_cast<ThisCall3>(context->address)(context->values[0],context->values[1],context->values[2]);break;case 4:context->returned=reinterpret_cast<ThisCall4>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3]);break;case 5:context->returned=reinterpret_cast<ThisCall5>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4]);break;case 6:context->returned=reinterpret_cast<ThisCall6>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4],context->values[5]);break;case 7:context->returned=reinterpret_cast<ThisCall7>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4],context->values[5],context->values[6]);break;default:context->returned=reinterpret_cast<ThisCall8>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4],context->values[5],context->values[6],context->values[7]);break;}
        }
        else if (std::strcmp(context->abi, "__fastcall") == 0)
        {
            switch(context->count){case 0:context->returned=reinterpret_cast<FastCall0>(context->address)();break;case 1:context->returned=reinterpret_cast<FastCall1>(context->address)(context->values[0]);break;case 2:context->returned=reinterpret_cast<FastCall2>(context->address)(context->values[0],context->values[1]);break;case 3:context->returned=reinterpret_cast<FastCall3>(context->address)(context->values[0],context->values[1],context->values[2]);break;case 4:context->returned=reinterpret_cast<FastCall4>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3]);break;case 5:context->returned=reinterpret_cast<FastCall5>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4]);break;case 6:context->returned=reinterpret_cast<FastCall6>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4],context->values[5]);break;case 7:context->returned=reinterpret_cast<FastCall7>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4],context->values[5],context->values[6]);break;default:context->returned=reinterpret_cast<FastCall8>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4],context->values[5],context->values[6],context->values[7]);break;}
        }
        else
#endif
        switch(context->count){case 0:context->returned=reinterpret_cast<Call0>(context->address)();break;case 1:context->returned=reinterpret_cast<Call1>(context->address)(context->values[0]);break;case 2:context->returned=reinterpret_cast<Call2>(context->address)(context->values[0],context->values[1]);break;case 3:context->returned=reinterpret_cast<Call3>(context->address)(context->values[0],context->values[1],context->values[2]);break;case 4:context->returned=reinterpret_cast<Call4>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3]);break;case 5:context->returned=reinterpret_cast<Call5>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4]);break;case 6:context->returned=reinterpret_cast<Call6>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4],context->values[5]);break;case 7:context->returned=reinterpret_cast<Call7>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4],context->values[5],context->values[6]);break;default:context->returned=reinterpret_cast<Call8>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4],context->values[5],context->values[6],context->values[7]);break;}
#endif
    } __except(context->exceptionCode=GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { }
    return 0;
}

}

bool InvokeNativeCallX86(NativeCallFrameX86& frame, std::string& error)
{
#if !defined(_M_IX86)
    (void)frame;
    error = "x86 native adapter requires an x86 build";
    return false;
#else
    if (frame.targetAddress == 0)
    {
        error = "x86 native adapter requires a target address";
        return false;
    }
    if (frame.argumentCount > frame.arguments.size())
    {
        error = "x86 native adapter supports at most eight arguments";
        return false;
    }
    const uint32_t registerWords = frame.convention == X86CallingConvention::Thiscall
        ? 1U : frame.convention == X86CallingConvention::Fastcall ? 2U : 0U;
    const uint32_t expectedStackBytes = frame.argumentCount > registerWords
        ? (frame.argumentCount - registerWords) * sizeof(uint32_t) : 0U;
    if (frame.stackBytes != expectedStackBytes)
    {
        error = "x86 native adapter frame stack size is inconsistent";
        return false;
    }
    if (frame.convention == X86CallingConvention::Thiscall && frame.argumentCount == 0)
    {
        error = "x86 thiscall frame requires an object pointer";
        return false;
    }
    switch (frame.convention)
    {
    case X86CallingConvention::Cdecl:
        switch (frame.argumentCount)
        {
        case 0: frame.returned = reinterpret_cast<Call0>(frame.targetAddress)(); break;
        case 1: frame.returned = reinterpret_cast<Call1>(frame.targetAddress)(frame.arguments[0]); break;
        case 2: frame.returned = reinterpret_cast<Call2>(frame.targetAddress)(frame.arguments[0], frame.arguments[1]); break;
        case 3: frame.returned = reinterpret_cast<Call3>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2]); break;
        case 4: frame.returned = reinterpret_cast<Call4>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3]); break;
        case 5: frame.returned = reinterpret_cast<Call5>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4]); break;
        case 6: frame.returned = reinterpret_cast<Call6>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4], frame.arguments[5]); break;
        case 7: frame.returned = reinterpret_cast<Call7>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4], frame.arguments[5], frame.arguments[6]); break;
        case 8: frame.returned = reinterpret_cast<Call8>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4], frame.arguments[5], frame.arguments[6], frame.arguments[7]); break;
        }
        return true;
    case X86CallingConvention::Stdcall:
        switch (frame.argumentCount)
        {
        case 0: frame.returned = reinterpret_cast<StdCall0>(frame.targetAddress)(); break;
        case 1: frame.returned = reinterpret_cast<StdCall1>(frame.targetAddress)(frame.arguments[0]); break;
        case 2: frame.returned = reinterpret_cast<StdCall2>(frame.targetAddress)(frame.arguments[0], frame.arguments[1]); break;
        case 3: frame.returned = reinterpret_cast<StdCall3>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2]); break;
        case 4: frame.returned = reinterpret_cast<StdCall4>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3]); break;
        case 5: frame.returned = reinterpret_cast<StdCall5>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4]); break;
        case 6: frame.returned = reinterpret_cast<StdCall6>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4], frame.arguments[5]); break;
        case 7: frame.returned = reinterpret_cast<StdCall7>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4], frame.arguments[5], frame.arguments[6]); break;
        case 8: frame.returned = reinterpret_cast<StdCall8>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4], frame.arguments[5], frame.arguments[6], frame.arguments[7]); break;
        }
        return true;
    case X86CallingConvention::Thiscall:
        switch (frame.argumentCount)
        {
        case 1: frame.returned = reinterpret_cast<ThisCall1>(frame.targetAddress)(frame.arguments[0]); break;
        case 2: frame.returned = reinterpret_cast<ThisCall2>(frame.targetAddress)(frame.arguments[0], frame.arguments[1]); break;
        case 3: frame.returned = reinterpret_cast<ThisCall3>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2]); break;
        case 4: frame.returned = reinterpret_cast<ThisCall4>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3]); break;
        case 5: frame.returned = reinterpret_cast<ThisCall5>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4]); break;
        case 6: frame.returned = reinterpret_cast<ThisCall6>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4], frame.arguments[5]); break;
        case 7: frame.returned = reinterpret_cast<ThisCall7>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4], frame.arguments[5], frame.arguments[6]); break;
        case 8: frame.returned = reinterpret_cast<ThisCall8>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4], frame.arguments[5], frame.arguments[6], frame.arguments[7]); break;
        }
        return true;
    case X86CallingConvention::Fastcall:
        switch (frame.argumentCount)
        {
        case 0: frame.returned = reinterpret_cast<FastCall0>(frame.targetAddress)(); break;
        case 1: frame.returned = reinterpret_cast<FastCall1>(frame.targetAddress)(frame.arguments[0]); break;
        case 2: frame.returned = reinterpret_cast<FastCall2>(frame.targetAddress)(frame.arguments[0], frame.arguments[1]); break;
        case 3: frame.returned = reinterpret_cast<FastCall3>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2]); break;
        case 4: frame.returned = reinterpret_cast<FastCall4>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3]); break;
        case 5: frame.returned = reinterpret_cast<FastCall5>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4]); break;
        case 6: frame.returned = reinterpret_cast<FastCall6>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4], frame.arguments[5]); break;
        case 7: frame.returned = reinterpret_cast<FastCall7>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4], frame.arguments[5], frame.arguments[6]); break;
        case 8: frame.returned = reinterpret_cast<FastCall8>(frame.targetAddress)(frame.arguments[0], frame.arguments[1], frame.arguments[2], frame.arguments[3], frame.arguments[4], frame.arguments[5], frame.arguments[6], frame.arguments[7]); break;
        }
        return true;
    }
    error = "x86 native adapter received an unknown calling convention";
    return false;
#endif
}

bool InvokeNativeCallX64(NativeCallFrameX64& frame, uintptr_t& returned,
    std::string& error)
{
#if !defined(_M_X64)
    (void)frame;
    (void)returned;
    error = "x64 native adapter requires an x64 build";
    return false;
#else
    if (frame.targetAddress == 0)
    {
        error = "x64 native adapter requires a target address";
        return false;
    }
    if (frame.argumentCount > frame.arguments.size())
    {
        error = "x64 native adapter supports at most eight arguments";
        return false;
    }
    NativeCallX64(frame.targetAddress, frame.arguments.data(), frame.argumentCount,
        frame.floatingArgumentMask, &returned,
        &frame.floatingReturnBits, frame.floatingReturn);
    return true;
#endif
}

bool InvokeX64Export(const std::string& imagePath, const CallRequest& request,
    const FunctionCatalog& catalog, CallResult& result, std::string& error)
{
#if !defined(_M_X64) && !defined(_M_IX86)
    (void)imagePath; (void)request; (void)catalog; (void)result; error = "x64 invocation requires an x64 build"; return false;
#else
    std::vector<CallDiagnostic> diagnostics;
    if (!ValidateCallRequest(request, catalog, diagnostics)) { result = {}; result.correlationId=request.correlationId; result.status="validation-failed"; result.diagnostics=diagnostics; error="call request validation failed"; return false; }
    for (const CallArgument& argument : request.arguments)
    {
        if (argument.type.kind != TypeKind::Integer && argument.type.kind != TypeKind::Bool && argument.type.kind != TypeKind::Pointer && argument.type.kind != TypeKind::Handle && argument.type.kind != TypeKind::String && argument.type.kind != TypeKind::Bytes && argument.type.kind != TypeKind::Floating)
        {
            result = {}; result.correlationId = request.correlationId; result.status = "validation-failed";
            result.diagnostics.push_back({"unsupported-type", "arguments", "x64 adapter supports scalar integer, bool, pointer, and floating values"});
            error = "unsupported x64 argument type";
            return false;
        }
        if (argument.type.kind == TypeKind::Pointer && (argument.type.direction != ParameterDirection::In || argument.bufferSize != 0 || !argument.ownership.empty()))
        {
            result = {}; result.correlationId=request.correlationId; result.status="validation-failed";
            result.diagnostics.push_back({"unsupported-output-descriptor", "arguments", "x64 adapter currently accepts input scalar values only"});
            error="output buffers and ownership descriptors are not supported by this adapter";
            return false;
        }
    }
    FunctionRecord const* selected=catalog.Find(request.selector);
    if (selected == nullptr) { error="function selector is unavailable"; return false; }
    if (selected->exportNames.empty() && (!request.allowInternal || !selected->executable)) { error="internal target is not executable or authorized"; return false; }
    const PrototypeSpec prototype=request.hasPrototypeOverride?request.prototypeOverride:selected->prototype;
#if defined(_M_IX86)
    if (prototype.abi != "__cdecl" && prototype.abi != "__stdcall" && prototype.abi != "__thiscall" && prototype.abi != "__fastcall") { result={}; result.correlationId=request.correlationId; result.status="validation-failed"; result.diagnostics.push_back({"unsupported-abi","prototype.abi","x86 adapter supports __cdecl, __stdcall, __thiscall, and __fastcall"}); error="unsupported x86 calling convention"; return false; }
    if (prototype.abi == "__thiscall" && request.arguments.empty()) { result={}; result.correlationId=request.correlationId; result.status="validation-failed"; result.diagnostics.push_back({"missing-object-pointer","arguments[0]","__thiscall requires the object pointer as the first argument"}); error="__thiscall requires an object pointer"; return false; }
    if (prototype.returnType.kind == TypeKind::Floating) { result={}; result.correlationId=request.correlationId; result.status="validation-failed"; result.diagnostics.push_back({"unsupported-return-type","prototype.return_type","x86 adapter supports integer, bool, and pointer returns only"}); error="unsupported x86 return type"; return false; }
#endif
    if (prototype.returnType.kind == TypeKind::Structure || prototype.returnType.kind == TypeKind::Void)
    { result={}; result.correlationId=request.correlationId; result.status="validation-failed"; result.diagnostics.push_back({"unsupported-return-type","prototype.return_type","x64 adapter supports scalar integer, bool, and pointer returns only"}); error="unsupported x64 return type"; return false; }
    FunctionCatalog current; if (!FunctionCatalog::Load(imagePath, current, error) || !SameIdentity(current.Module(), catalog.Module())) { error = "runtime module identity changed"; return false; }
    const FunctionRecord* record=selected;
    HMODULE module=LoadLibraryExA(imagePath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS); if(module==nullptr){error="unable to load target module";return false;}
    FARPROC address=nullptr;
    const uintptr_t moduleBase=reinterpret_cast<uintptr_t>(module);
    if (!record->exportNames.empty()) address=GetProcAddress(module,record->exportNames.front().c_str());
    else
    {
        if (record->startRva > current.Module().imageSize || moduleBase > std::numeric_limits<uintptr_t>::max() - record->startRva)
        { FreeLibrary(module); error="internal RVA arithmetic is invalid"; return false; }
        address=reinterpret_cast<FARPROC>(moduleBase + record->startRva);
    }
    if(address==nullptr){FreeLibrary(module);error="target address is unavailable";return false;}
    uint64_t values[8]={}; if(request.arguments.size()>8){FreeLibrary(module);error="x64 adapter supports at most eight arguments";return false;}
    uint32_t floatingMask = 0;
    for(size_t i=0;i<request.arguments.size();++i)
        if (request.arguments[i].type.kind == TypeKind::Floating)
        { if (!FloatingValue(request.arguments[i], values[i])) { FreeLibrary(module); error="invalid floating-point argument"; return false; } floatingMask |= (1U << i); }
        else if (request.arguments[i].type.kind != TypeKind::String && request.arguments[i].type.kind != TypeKind::Bytes && !Value(request.arguments[i],values[i]))
        { FreeLibrary(module); error="invalid typed argument"; return false; }
#if defined(_M_IX86)
    for(size_t i=0;i<request.arguments.size();++i) if(values[i] > UINT32_MAX) { FreeLibrary(module); error="x86 argument exceeds pointer width"; return false; }
#endif
    const uintptr_t functionAddress=reinterpret_cast<uintptr_t>(address);
    if (functionAddress < moduleBase || functionAddress-moduleBase > UINT32_MAX || static_cast<uint32_t>(functionAddress-moduleBase) != record->startRva) { FreeLibrary(module); error="resolved target does not match static RVA"; return false; }
    unsigned available=0;
    if (!retainedTimeoutWorkers.compare_exchange_strong(available, 1, std::memory_order_acq_rel))
    {
        FreeLibrary(module);
        result = {}; result.correlationId=request.correlationId; result.status="worker-capacity";
        result.diagnostics.push_back({"worker-capacity-exhausted", "call", "a timed-out worker is retained; process isolation is required before another call"});
        error="invocation worker capacity exhausted";
        return false;
    }
#if defined(_M_X64)
    WorkerState* state = new WorkerState();
    state->module = module;
    state->argumentStorage.resize(request.arguments.size());
    state->call.frame.targetAddress = functionAddress;
    state->call.frame.argumentCount = static_cast<uint32_t>(request.arguments.size());
    state->call.frame.floatingReturn = prototype.returnType.kind == TypeKind::Floating;
    state->call.frame.floatingArgumentMask = floatingMask;
    for (size_t index = 0; index < request.arguments.size(); ++index)
    {
        const CallArgument& argument = request.arguments[index];
        if (argument.type.kind == TypeKind::String || argument.type.kind == TypeKind::Bytes)
        {
            if (!PrepareStorage(argument, state->argumentStorage[index], error))
            { delete state; retainedTimeoutWorkers.store(0, std::memory_order_release); FreeLibrary(module); error = error.empty() ? "unable to prepare argument storage" : error; return false; }
            state->call.frame.arguments[index] = reinterpret_cast<uintptr_t>(state->argumentStorage[index].data());
        }
        else if (argument.type.kind == TypeKind::Floating)
        {
            if (!FloatingValue(argument, state->call.frame.arguments[index]))
            { delete state; retainedTimeoutWorkers.store(0, std::memory_order_release); FreeLibrary(module); error = "invalid floating-point argument"; return false; }
            state->call.frame.floatingArgumentMask |= (1U << index);
        }
        else state->call.frame.arguments[index] = static_cast<uintptr_t>(values[index]);
    }
#else
    X86CallingConvention convention = X86CallingConvention::Cdecl;
    if (!ParseX86CallingConvention(prototype.abi, convention))
    { FreeLibrary(module); error = "unsupported x86 calling convention"; return false; }
    WorkerState* state = new WorkerState(); state->module = module; state->abi = prototype.abi;
    state->call.address = address;
    state->call.count = request.arguments.size();
    state->call.frame.targetAddress = reinterpret_cast<uintptr_t>(address);
    state->call.frame.argumentCount = static_cast<uint32_t>(request.arguments.size());
    state->call.frame.convention = convention;
    const uint32_t registerWords = convention == X86CallingConvention::Thiscall ? 1U : convention == X86CallingConvention::Fastcall ? 2U : 0U;
    state->call.frame.stackBytes = state->call.frame.argumentCount > registerWords
        ? (state->call.frame.argumentCount - registerWords) * sizeof(uint32_t) : 0U;
    state->argumentStorage.resize(request.arguments.size());
    state->call.abi = state->abi.c_str();
    for (size_t index = 0; index < request.arguments.size(); ++index)
    {
        if (request.arguments[index].type.kind == TypeKind::String || request.arguments[index].type.kind == TypeKind::Bytes)
        {
            if (!PrepareStorage(request.arguments[index], state->argumentStorage[index], error)) { delete state; retainedTimeoutWorkers.store(0, std::memory_order_release); FreeLibrary(module); return false; }
            state->values[index] = reinterpret_cast<uintptr_t>(state->argumentStorage[index].data());
        }
        else state->values[index] = static_cast<uintptr_t>(values[index]);
        state->call.frame.arguments[index] = static_cast<uintptr_t>(values[index]);
    }
    state->call.values = state->values;
#endif
    HANDLE worker=CreateThread(nullptr, 0, CallWorker, &state->call, 0, nullptr);
    if (worker == nullptr) { retainedTimeoutWorkers.store(0, std::memory_order_release); FreeLibrary(module); delete state; error="unable to create invocation worker"; return false; }
    const DWORD timeout=request.timeoutMs == 0 ? 30000U : request.timeoutMs;
    const DWORD wait=WaitForSingleObject(worker, timeout);
    if (wait == WAIT_TIMEOUT)
    {
        CloseHandle(worker);
        result={}; result.correlationId=request.correlationId; result.status="timed-out"; result.diagnostics.push_back({"timeout","timeout_ms","target exceeded the invocation timeout"});
        error="target invocation timed out";
        return false;
    }
    if (wait == WAIT_FAILED)
    {
        CloseHandle(worker); retainedTimeoutWorkers.store(0, std::memory_order_release); FreeLibrary(module); delete state;
        result={}; result.correlationId=request.correlationId; result.status="worker-failed"; result.diagnostics.push_back({"worker-wait-failed","call","unable to wait for invocation worker"});
        error="unable to wait for invocation worker";
        return false;
    }
    CloseHandle(worker);
#if defined(_M_X64)
    const uint64_t returned=static_cast<uint64_t>(state->call.frame.targetAddress);
    const uint64_t floatingReturned=state->call.frame.floatingReturnBits;
#else
    const uint64_t returned=state->call.returned;
#endif
    const int exceptionCode=state->call.exceptionCode;
    std::vector<CallArgument> outputValues;
    for (size_t index = 0; index < request.arguments.size(); ++index)
        if ((request.arguments[index].type.kind == TypeKind::Bytes || request.arguments[index].type.kind == TypeKind::String) && request.arguments[index].type.direction != ParameterDirection::In)
        {
            CallArgument output = request.arguments[index];
            output.value = request.arguments[index].type.kind == TypeKind::String
                ? EncodeString(state->argumentStorage[index], request.arguments[index].type)
                : EncodeHex(state->argumentStorage[index]);
            output.bufferSize = state->argumentStorage[index].size();
            outputValues.push_back(std::move(output));
        }
    delete state;
    retainedTimeoutWorkers.store(0, std::memory_order_release);
    if (exceptionCode != 0) { FreeLibrary(module); result={}; result.correlationId=request.correlationId; result.status="crashed"; result.diagnostics.push_back({"target-exception","call","target raised a structured exception"}); error="target raised a structured exception"; return false; }
    FreeLibrary(module); result={}; result.correlationId=request.correlationId; result.success=true; result.status="completed";
#if defined(_M_X64)
    result.returnValue = prototype.returnType.kind == TypeKind::Floating
        ? FloatingReturnValue(floatingReturned, prototype.returnType)
        : ReturnValue(returned, prototype.returnType);
#else
    result.returnValue = ReturnValue(returned, prototype.returnType);
#endif
    result.returnType=prototype.returnType; result.prototypeUsed=prototype; result.resolvedModule=catalog.Module();
    result.outputValues = std::move(outputValues);
    return true;
#endif
}
