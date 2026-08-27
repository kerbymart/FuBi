#include "stdafx.h"
#include "ProcessInvocation.h"
#include "InvocationEngine.h"

#include <windows.h>
#include <fstream>

namespace
{
std::string Quote(const std::string& value) { return "\"" + value + "\""; }
std::string WorkerPath()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string result(path);
    const size_t slash = result.find_last_of("\\/");
    return result.substr(0, slash + 1) + "FubiInvocationWorker.exe";
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
}

bool InvokeX64ExportProcess(const std::string& imagePath, const CallRequest& request,
    const FunctionCatalog& catalog, CallResult& result, std::string& error)
{
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
    std::string command = Quote(WorkerPath()) + " " + Quote(imagePath) + " " + Quote(requestPath) + " " + Quote(resultPath);
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
        RecordExitCode(process.hProcess, result);
        result.diagnostics.push_back({terminated && stopped ? "timeout" : "termination-failed", "timeout_ms", terminated && stopped ? "worker process exceeded the invocation timeout" : "worker could not be terminated safely"});
        AddExitDiagnostic(result);
        if (!(terminated && stopped))
            WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        if (!cleanup()) result.diagnostics.push_back({"cleanup-failed", "ipc", "unable to remove worker IPC files"});
        error = terminated && stopped ? "invocation worker timed out" : "unable to terminate invocation worker";
        return false;
    }
    if (wait == WAIT_FAILED)
    {
        TerminateProcess(process.hProcess, ERROR_OPERATION_ABORTED); WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        const bool clean = cleanup(); const bool failed = failure("worker-failed", "worker-wait-failed", "unable to wait for invocation worker");
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
