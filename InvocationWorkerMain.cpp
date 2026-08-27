#include "stdafx.h"

#include "CallContract.h"
#include "InvocationEngine.h"
#include "SessionReferences.h"

#include <fstream>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <io.h>
#include <sstream>
#include <windows.h>

namespace
{
class TargetOutputSilencer final
{
public:
    TargetOutputSilencer()
    {
        std::fflush(nullptr);
        savedOutputFd_ = _dup(_fileno(stdout));
        savedErrorFd_ = _dup(_fileno(stderr));
        _sopen_s(&sinkFd_, "NUL", _O_WRONLY | _O_BINARY, _SH_DENYNO, 0);
        if (savedOutputFd_ >= 0 && savedErrorFd_ >= 0 && sinkFd_ >= 0)
        {
            _dup2(sinkFd_, _fileno(stdout));
            _dup2(sinkFd_, _fileno(stderr));
        }
        sink_ = CreateFileA("NUL", GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (sink_ == INVALID_HANDLE_VALUE) { sink_ = nullptr; return; }
        previousOutput_ = GetStdHandle(STD_OUTPUT_HANDLE);
        previousError_ = GetStdHandle(STD_ERROR_HANDLE);
        SetStdHandle(STD_OUTPUT_HANDLE, sink_);
        SetStdHandle(STD_ERROR_HANDLE, sink_);
    }

    ~TargetOutputSilencer()
    {
        std::fflush(nullptr);
        if (savedOutputFd_ >= 0) _dup2(savedOutputFd_, _fileno(stdout));
        if (savedErrorFd_ >= 0) _dup2(savedErrorFd_, _fileno(stderr));
        if (savedOutputFd_ >= 0) _close(savedOutputFd_);
        if (savedErrorFd_ >= 0) _close(savedErrorFd_);
        if (sinkFd_ >= 0) _close(sinkFd_);
        if (sink_ == nullptr) return;
        SetStdHandle(STD_OUTPUT_HANDLE, previousOutput_);
        SetStdHandle(STD_ERROR_HANDLE, previousError_);
        CloseHandle(sink_);
    }

private:
    HANDLE sink_ = nullptr;
    HANDLE previousOutput_ = nullptr;
    HANDLE previousError_ = nullptr;
    int savedOutputFd_ = -1;
    int savedErrorFd_ = -1;
    int sinkFd_ = -1;
};

bool ParseWorkerPointer(const std::string& text, uint64_t& value)
{
    if (text.rfind("opaque:0x", 0) != 0 || text.size() == 9) return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str() + 9, &end, 16);
    if (errno == ERANGE || end == text.c_str() + 9 || *end != '\0') return false;
    value = parsed;
    return true;
}

bool WriteProtocolLine(HANDLE protocol, const CallResult& result)
{
    std::ostringstream encoded;
    WriteCallResultJson(encoded, result);
    encoded << '\n';
    const std::string line = encoded.str();
    DWORD written = 0;
    return WriteFile(protocol, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) &&
        written == line.size();
}

int RunSession(const char* imagePath, HANDLE protocol)
{
    FunctionCatalog catalog;
    std::string error;
    if (!FunctionCatalog::Load(imagePath, catalog, error)) return 3;
    // Keep one module reference for the whole session. Individual invocation
    // calls may temporarily add/release a reference, but returned addresses
    // remain valid until this process exits or the session is terminated.
    HMODULE pinnedModule = nullptr;
    {
        TargetOutputSilencer silence;
        pinnedModule = LoadLibraryA(imagePath);
    }
    if (pinnedModule == nullptr) return 7;
    SessionReferences references;
    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        CallRequest request;
        std::vector<CallDiagnostic> diagnostics;
        CallResult result;
        result.action = "call";
        if (!ParseCallRequestJson(line, request, diagnostics))
        {
            result.correlationId = request.correlationId;
            result.status = "validation-failed";
            result.diagnostics = std::move(diagnostics);
        }
        else if (request.action == "release")
        {
            result.action = "release";
            result.correlationId = request.correlationId;
            result.resolvedModule = catalog.Module();
            const bool released = references.Release(request.reference) ||
                references.ReleaseHandle(request.reference, catalog.Module().sha256, catalog.Module().architecture);
            if (released)
            {
                result.success = true;
                result.status = "released";
                result.releasedReference = request.reference;
            }
            else
            {
                result.status = "validation-failed";
                result.diagnostics.push_back({"reference-not-found", "reference",
                    "reference is unknown or already released"});
            }
        }
        else
        {
            // Resolve only worker-issued tokens. The numeric address is kept
            // inside this worker and never crosses the protocol boundary.
            bool referencesValid = true;
            for (CallArgument& argument : request.arguments)
            {
                if ((argument.type.kind != TypeKind::Pointer && argument.type.kind != TypeKind::Handle) ||
                    argument.value.rfind("opaque:session-", 0) != 0) continue;
                uint64_t address = 0;
                const bool resolved = argument.type.kind == TypeKind::Handle
                    ? references.ResolveHandle(argument.value, address, catalog.Module().sha256, catalog.Module().architecture)
                    : references.Resolve(argument.value, address);
                if (!resolved)
                {
                    referencesValid = false;
                    diagnostics.push_back({"reference-not-found", "arguments",
                        "reference is unknown, stale, or belongs to another session"});
                    break;
                }
                std::ostringstream pointer;
                pointer << "opaque:0x" << std::hex << address;
                argument.value = pointer.str();
            }
            if (!referencesValid)
            {
                result.correlationId = request.correlationId;
                result.status = "validation-failed";
                result.diagnostics = std::move(diagnostics);
            }
            else if (!ValidateCallRequest(request, catalog, diagnostics))
            {
                result.correlationId = request.correlationId;
                result.resolvedModule = catalog.Module();
                result.status = "validation-failed";
                result.diagnostics = std::move(diagnostics);
            }
            else
            {
                TargetOutputSilencer silence;
                if (!InvokeX64Export(imagePath, request, catalog, result, error) && !error.empty())
                    result.diagnostics.push_back({"worker-failed", "call", error});
            }
            if (result.success && (result.prototypeUsed.returnType.kind == TypeKind::Pointer || result.prototypeUsed.returnType.kind == TypeKind::Handle))
            {
                uint64_t address = 0;
                if (!ParseWorkerPointer(result.returnValue, address) || address == 0)
                {
                    result.success = false;
                    result.status = "worker-failed";
                    result.returnValue.clear();
                    result.diagnostics.push_back({"pointer-result-invalid", "return_value",
                        "worker returned an invalid pointer representation"});
                }
                else
                {
                    if (result.prototypeUsed.returnType.kind == TypeKind::Handle &&
                        result.prototypeUsed.returnType.ownership == "owned" &&
                        result.prototypeUsed.returnType.releaseAdapter.empty())
                    {
                        result.success = false;
                        result.status = "validation-failed";
                        result.returnValue.clear();
                        result.diagnostics.push_back({"handle-release-unconfigured", "return_value",
                            "owned handles require an explicitly configured release adapter"});
                    }
                    else
                    {
                        SessionReferences::HandleReleaseAdapter release;
                        if (result.prototypeUsed.returnType.releaseAdapter == "CloseHandle")
                            release = [](uint64_t value) { return CloseHandle(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value))) != FALSE; };
                        result.returnValue = result.prototypeUsed.returnType.kind == TypeKind::Handle
                            ? references.IssueHandle(address, {result.prototypeUsed.returnType.width, catalog.Module().sha256, catalog.Module().architecture, result.prototypeUsed.returnType.ownership, std::move(release)})
                            : references.Issue(address);
                    }
                    if (!result.returnValue.empty())
                        result.issuedReferences.push_back(result.returnValue);
                }
            }
        }
        if (!WriteProtocolLine(protocol, result)) break;
        if (request.action == "quit") break;
    }
    FreeLibrary(pinnedModule);
    return 0;
}
}

// The worker is deliberately a separate executable. It accepts one bounded
// request file and writes one structured result file, so a supervisor can
// terminate this process without leaving target code in its own address space.
int main(int argc, char* argv[])
{
    if (argc == 4 && std::string(argv[2]) == "--session")
    {
        char* end = nullptr;
        const unsigned long long value = std::strtoull(argv[3], &end, 16);
        if (end == argv[3] || *end != '\0' || value == 0) return 2;
        return RunSession(argv[1], reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value)));
    }
    if (argc != 4) return 2;
    std::ifstream input(argv[2], std::ios::binary | std::ios::ate);
    if (!input) return 3;
    const std::streamoff size=input.tellg();
    if (size < 0 || size > 4 * 1024 * 1024) return 3;
    std::string document(static_cast<size_t>(size), '\0'); input.seekg(0); if (!document.empty()) input.read(&document[0], static_cast<std::streamsize>(document.size()));
    CallRequest request; std::vector<CallDiagnostic> diagnostics;
    CallResult result; result.status="validation-failed";
    if (!ParseCallRequestJson(document, request, diagnostics)) { result.correlationId=request.correlationId; result.diagnostics=diagnostics; }
    else
    {
        FunctionCatalog catalog; std::string error;
        if (!FunctionCatalog::Load(argv[1], catalog, error) || !ValidateCallRequest(request, catalog, diagnostics)) { result.correlationId=request.correlationId; result.resolvedModule=catalog.Module(); result.diagnostics=diagnostics; if(!error.empty()) result.diagnostics.push_back({"worker-validation-failed","call",error}); }
        else if (!InvokeX64Export(argv[1], request, catalog, result, error) && !error.empty()) result.diagnostics.push_back({"worker-failed","call",error});
    }
    std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
    if (!output) return 4;
    WriteCallResultJson(output, result);
    return result.success ? 0 : 1;
}
