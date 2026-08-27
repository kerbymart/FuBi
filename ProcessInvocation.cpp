#include "stdafx.h"
#include "ProcessInvocation.h"
#include "InvocationEngine.h"

#include <windows.h>
#include <fstream>
#include <mutex>
#include <vector>

namespace
{
std::string Quote(const std::string& value) { return "\"" + value + "\""; }
std::string ControllerDirectory()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string result(path);
    const size_t slash = result.find_last_of("\\/");
    return result.substr(0, slash + 1);
}

bool BinaryMatchesArchitecture(const std::string& path,
    const std::string& architecture)
{
    DWORD type = 0;
    if (GetBinaryTypeA(path.c_str(), &type) == FALSE) return false;
    if (architecture == "x64") return type == SCS_64BIT_BINARY;
    return architecture == "x86" && type == SCS_32BIT_BINARY;
}

void RecordExitCode(HANDLE process, CallResult& result)
{
    DWORD exitCode = 0;
    if (GetExitCodeProcess(process, &exitCode) != FALSE)
    {
        result.hasWorkerExitCode = true;
        result.workerExitCode = exitCode;
    }
}
void AddExitDiagnostic(CallResult& result)
{
    if (result.hasWorkerExitCode && result.workerExitCode != 0)
        result.diagnostics.push_back({"worker-process-exit", "worker_exit_code", "worker exited with a non-zero process status"});
}
bool IsCrashExitCode(DWORD exitCode)
{
    return (exitCode & 0xC0000000U) == 0xC0000000U;
}
struct RetainedWorker
{
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    std::string requestPath;
    std::string resultPath;
};
std::mutex retainedWorkersMutex;
std::vector<RetainedWorker> retainedWorkers;
void ReapRetainedWorkers()
{
    std::lock_guard<std::mutex> lock(retainedWorkersMutex);
    for (auto it = retainedWorkers.begin(); it != retainedWorkers.end();)
    {
        if (WaitForSingleObject(it->process, 0) != WAIT_OBJECT_0) { ++it; continue; }
        CloseHandle(it->thread); CloseHandle(it->process);
        DeleteFileA(it->requestPath.c_str()); DeleteFileA(it->resultPath.c_str());
        it = retainedWorkers.erase(it);
    }
}
void RetainWorker(PROCESS_INFORMATION& process, const char* requestPath, const char* resultPath)
{
    std::lock_guard<std::mutex> lock(retainedWorkersMutex);
    retainedWorkers.push_back({process.hProcess, process.hThread, requestPath, resultPath});
    process.hProcess = nullptr; process.hThread = nullptr;
}
}

bool NormalizeCall(const CallRequest& request, const FunctionCatalog& catalog,
    NormalizedCall& call, std::vector<CallDiagnostic>& diagnostics)
{
    diagnostics.clear();
    if (!ValidateCallRequest(request, catalog, diagnostics))
        return false;

    const FunctionRecord* selected = catalog.Find(request.selector);
    if (selected == nullptr && !request.hasPrototypeOverride)
        return false;

    call = {};
    call.request = request;
    call.module = catalog.Module();
    call.prototype = request.hasPrototypeOverride
        ? request.prototypeOverride : selected->prototype;
    return true;
}

bool DispatchCall(const CallRequest& request, const FunctionCatalog& catalog,
    InvocationAdapter& adapter, CallResult& result, std::string& error)
{
    NormalizedCall call;
    std::vector<CallDiagnostic> diagnostics;
    if (!NormalizeCall(request, catalog, call, diagnostics))
    {
        result = {};
        result.action = request.action;
        result.correlationId = request.correlationId;
        result.resolvedModule = catalog.Module();
        result.status = "validation-failed";
        result.diagnostics = std::move(diagnostics);
        error = "call request validation failed";
        return false;
    }

    result = {};
    result.action = request.action;
    result.correlationId = request.correlationId;
    result.resolvedModule = call.module;
    result.prototypeUsed = call.prototype;
    result.status = "not-executed";
    return adapter.Invoke(call, result, error);
}

bool WorkerInvocationAdapter::Invoke(const NormalizedCall& call,
    CallResult& result, std::string& error)
{
    return InvokeX64ExportProcess(imagePath_, call.request, catalog_, result,
        error, allowPointerResults_);
}

bool SelectInvocationWorker(const std::string& targetArchitecture,
    std::string& workerPath, std::string& error)
{
    workerPath.clear();
    error.clear();
    if (targetArchitecture != "x64" && targetArchitecture != "x86")
    {
        error = "unsupported target architecture";
        return false;
    }
#if defined(_M_IX86)
    if (targetArchitecture == "x64")
    {
        error = "x64 target cannot run under an x86 controller";
        return false;
    }
#endif
    const std::string fileName = targetArchitecture == "x86"
        ? "FubiInvocationWorker_x86.exe" : "FubiInvocationWorker.exe";
    workerPath = ControllerDirectory() + fileName;
    if (!BinaryMatchesArchitecture(workerPath, targetArchitecture))
    {
        workerPath.clear();
        error = "selected invocation worker is unavailable or has the wrong architecture";
        return false;
    }
    return true;
}

bool InvokeX64ExportProcess(const std::string& imagePath, const CallRequest& request,
    const FunctionCatalog& catalog, CallResult& result, std::string& error,
    bool allowPointerResults)
{
    ReapRetainedWorkers();
    auto failure = [&](const char* status, const char* code, const char* message)
    {
        result = {};
        result.correlationId = request.correlationId;
        result.status = status;
        result.diagnostics.push_back({code, "worker", message});
        error = message;
        return false;
    };
    std::vector<CallDiagnostic> diagnostics;
    if (!ValidateCallRequest(request, catalog, diagnostics))
    {
        result = {};
        result.correlationId = request.correlationId;
        result.status = "validation-failed";
        result.diagnostics = diagnostics;
        error = "call request validation failed";
        return false;
    }
    const FunctionRecord* selected = catalog.Find(request.selector);
    const PrototypeSpec& prototype = request.hasPrototypeOverride
        ? request.prototypeOverride : selected->prototype;
    if (prototype.returnType.kind == TypeKind::Pointer && !allowPointerResults)
    {
        result = {};
        result.correlationId = request.correlationId;
        result.status = "validation-failed";
        result.diagnostics.push_back({"pointer-result-unsupported", "prototype.return_type",
            "pointer return values cannot cross the isolated worker boundary"});
        error = "pointer return values are not supported by isolated invocation";
        return false;
    }
    std::string workerPath;
    if (!SelectInvocationWorker(catalog.Module().architecture, workerPath, error))
    {
        result = {};
        result.correlationId = request.correlationId;
        result.status = "worker-failed";
        result.diagnostics.push_back({"worker-architecture", "worker", error});
        return false;
    }
    char tempPath[MAX_PATH] = {};
    if (!GetTempPathA(MAX_PATH, tempPath)) return failure("worker-failed", "ipc-temp-path", "unable to locate temporary directory");
    char requestPath[MAX_PATH] = {};
    char resultPath[MAX_PATH] = {};
    if (!GetTempFileNameA(tempPath, "fbi", 0, requestPath) || !GetTempFileNameA(tempPath, "fbo", 0, resultPath))
    {
        DeleteFileA(requestPath); DeleteFileA(resultPath);
        return failure("worker-failed", "ipc-create", "unable to create worker IPC files");
    }
    auto cleanup = [&]()
    {
        const bool requestClean = DeleteFileA(requestPath) != FALSE || GetLastError() == ERROR_FILE_NOT_FOUND;
        const bool resultClean = DeleteFileA(resultPath) != FALSE || GetLastError() == ERROR_FILE_NOT_FOUND;
        return requestClean && resultClean;
    };
    {
        std::ofstream output(requestPath, std::ios::binary | std::ios::trunc);
        if (!output) { DeleteFileA(requestPath); DeleteFileA(resultPath); return failure("worker-failed", "ipc-write", "unable to write worker request"); }
        WriteCallRequestJson(output, request);
    }
    std::string command = Quote(workerPath) + " " + Quote(imagePath) + " " + Quote(requestPath) + " " + Quote(resultPath);
    std::vector<char> commandLine(command.begin(), command.end()); commandLine.push_back('\0');
    STARTUPINFOA startup = {}; startup.cb = sizeof(startup); PROCESS_INFORMATION process = {};
    if (!CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
    {
        DeleteFileA(requestPath); DeleteFileA(resultPath);
        return failure("worker-failed", "worker-launch", "unable to start invocation worker");
    }
    const DWORD timeout = request.timeoutMs == 0 ? 30000U : request.timeoutMs;
    const DWORD wait = WaitForSingleObject(process.hProcess, timeout);
    if (wait == WAIT_TIMEOUT)
    {
        const bool terminated = TerminateProcess(process.hProcess, ERROR_TIMEOUT) != FALSE;
        const bool stopped = WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0;
        result = {}; result.correlationId = request.correlationId;
        result.status = terminated && stopped ? "timed-out" : "termination-failed";
        if (stopped)
            RecordExitCode(process.hProcess, result);
        result.diagnostics.push_back({terminated && stopped ? "timeout" : "termination-failed", "timeout_ms", terminated && stopped ? "worker process exceeded the invocation timeout" : "worker could not be terminated safely"});
        AddExitDiagnostic(result);
        if (!(terminated && stopped))
            RetainWorker(process, requestPath, resultPath);
        else
        {
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
            if (!cleanup()) result.diagnostics.push_back({"cleanup-failed", "ipc", "unable to remove worker IPC files"});
        }
        error = terminated && stopped ? "invocation worker timed out" : "unable to terminate invocation worker";
        return false;
    }
    if (wait == WAIT_FAILED)
    {
        const bool terminated = TerminateProcess(process.hProcess, ERROR_OPERATION_ABORTED) != FALSE;
        const bool stopped = WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0;
        const bool clean = stopped ? cleanup() : false;
        const bool failed = failure("worker-failed", "worker-wait-failed", "unable to wait for invocation worker");
        if (stopped)
        {
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
        }
        else
        {
            RetainWorker(process, requestPath, resultPath);
            if (!terminated) result.diagnostics.push_back({"termination-failed", "worker", "worker could not be terminated safely"});
        }
        if (!clean) result.diagnostics.push_back({"cleanup-failed", "ipc", "unable to remove worker IPC files"});
        return failed;
    }
    DWORD exitCode = 0; const bool gotExitCode = GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    std::ifstream input(resultPath, std::ios::binary | std::ios::ate);
    if (!input)
    {
        const bool clean = cleanup(); result = {}; result.correlationId = request.correlationId;
        result.status = gotExitCode && IsCrashExitCode(exitCode) ? "worker-crashed" : "worker-failed";
        if (gotExitCode) { result.hasWorkerExitCode = true; result.workerExitCode = exitCode; }
        result.diagnostics.push_back({result.status == "worker-crashed" ? "worker-crashed" : "worker-no-result", "worker_exit_code", result.status == "worker-crashed" ? "worker exited abnormally without a result" : "worker exited without a result"});
        if (!clean) result.diagnostics.push_back({"cleanup-failed", "ipc", "unable to remove worker IPC files"});
        error = "worker exited without a result";
        return false;
    }
    const std::streamoff size = input.tellg();
    if (size < 0 || size > 4 * 1024 * 1024)
    {
        input.close(); const bool clean = cleanup();
        result = {}; result.correlationId = request.correlationId; result.status = "worker-failed";
        if (gotExitCode) { result.hasWorkerExitCode = true; result.workerExitCode = exitCode; }
        result.diagnostics.push_back({"result-size-limit", "worker", "worker result exceeds the size limit"});
        AddExitDiagnostic(result);
        error = "worker result exceeds the size limit";
        const bool failed = false;
        if (!clean) result.diagnostics.push_back({"cleanup-failed", "ipc", "unable to remove worker IPC files"});
        return failed;
    }
    std::string document(static_cast<size_t>(size), '\0'); input.seekg(0);
    if (!document.empty()) input.read(&document[0], static_cast<std::streamsize>(document.size()));
    input.close(); const bool clean = cleanup(); diagnostics.clear();
    if (!ParseCallResultJson(document, result, diagnostics))
    {
        result = {}; result.correlationId = request.correlationId;
        result.status = gotExitCode && IsCrashExitCode(exitCode) ? "worker-crashed" : "worker-failed";
        result.diagnostics = diagnostics;
        result.diagnostics.push_back({result.status == "worker-crashed" ? "worker-crashed" : "malformed-result", "worker", result.status == "worker-crashed" ? "worker exited abnormally before writing a result" : "worker result was malformed"});
        if (gotExitCode) { result.hasWorkerExitCode = true; result.workerExitCode = exitCode; }
        if (!clean) result.diagnostics.push_back({"cleanup-failed", "ipc", "unable to remove worker IPC files"});
        error = "worker result was malformed"; return false;
    }
    if (gotExitCode) { result.hasWorkerExitCode = true; result.workerExitCode = exitCode; }
    if (result.status == "crashed")
    {
        result.status = "worker-crashed";
        result.diagnostics.push_back({"worker-crashed", "worker_exit_code", "worker reported that target execution crashed"});
    }
    AddExitDiagnostic(result);
    if (!clean) result.diagnostics.push_back({"cleanup-failed", "ipc", "unable to remove worker IPC files"});
    return result.success;
}
