#include "stdafx.h"
#include "ProcessInvocation.h"
#include "InvocationEngine.h"

#include <windows.h>
#include <fstream>
#include <sstream>

namespace
{
std::string Quote(const std::string& value) { return "\"" + value + "\""; }
std::string WorkerPath()
{
    char path[MAX_PATH]={}; GetModuleFileNameA(nullptr,path,MAX_PATH); std::string result(path); const size_t slash=result.find_last_of("\\/"); return result.substr(0,slash+1)+"FubiInvocationWorker.exe";
}
}

bool InvokeX64ExportProcess(const std::string& imagePath, const CallRequest& request,
    const FunctionCatalog& catalog, CallResult& result, std::string& error)
{
    auto failure = [&](const char* status, const char* code, const char* message) { result={}; result.correlationId=request.correlationId; result.status=status; result.diagnostics.push_back({code,"worker",message}); error=message; return false; };
    std::vector<CallDiagnostic> diagnostics;
    if (!ValidateCallRequest(request,catalog,diagnostics)) { result={}; result.correlationId=request.correlationId; result.status="validation-failed"; result.diagnostics=diagnostics; error="call request validation failed"; return false; }
    char tempPath[MAX_PATH]={}; if(!GetTempPathA(MAX_PATH,tempPath)) return failure("worker-failed","ipc-temp-path","unable to locate temporary directory");
    char requestPath[MAX_PATH]={}; char resultPath[MAX_PATH]={}; if(!GetTempFileNameA(tempPath,"fbi",0,requestPath)||!GetTempFileNameA(tempPath,"fbo",0,resultPath)){DeleteFileA(requestPath);DeleteFileA(resultPath);return failure("worker-failed","ipc-create","unable to create worker IPC files");}
    { std::ofstream output(requestPath,std::ios::binary|std::ios::trunc); if(!output){DeleteFileA(requestPath);DeleteFileA(resultPath);return failure("worker-failed","ipc-write","unable to write worker request");} WriteCallRequestJson(output,request); }
    std::string command=Quote(WorkerPath())+" "+Quote(imagePath)+" "+Quote(requestPath)+" "+Quote(resultPath); std::vector<char> commandLine(command.begin(),command.end()); commandLine.push_back('\0'); STARTUPINFOA startup={}; startup.cb=sizeof(startup); PROCESS_INFORMATION process={};
    if(!CreateProcessA(nullptr,commandLine.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)){DeleteFileA(requestPath);DeleteFileA(resultPath);return failure("worker-failed","worker-launch","unable to start invocation worker");}
    const DWORD timeout=request.timeoutMs==0?30000U:request.timeoutMs; const DWORD wait=WaitForSingleObject(process.hProcess,timeout);
    if(wait==WAIT_TIMEOUT){TerminateProcess(process.hProcess,ERROR_TIMEOUT);WaitForSingleObject(process.hProcess,5000);CloseHandle(process.hThread);CloseHandle(process.hProcess);DeleteFileA(requestPath);DeleteFileA(resultPath);result={};result.correlationId=request.correlationId;result.status="timed-out";result.diagnostics.push_back({"timeout","timeout_ms","worker process exceeded the invocation timeout"});error="invocation worker timed out";return false;}
    if(wait==WAIT_FAILED){TerminateProcess(process.hProcess,ERROR_OPERATION_ABORTED);CloseHandle(process.hThread);CloseHandle(process.hProcess);DeleteFileA(requestPath);DeleteFileA(resultPath);return failure("worker-failed","worker-wait-failed","unable to wait for invocation worker");}
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    std::ifstream input(resultPath,std::ios::binary|std::ios::ate); if(!input){DeleteFileA(requestPath);DeleteFileA(resultPath);return failure("worker-crashed","worker-no-result","worker exited without a result");} const std::streamoff size=input.tellg(); if(size<0||size>4*1024*1024){DeleteFileA(requestPath);DeleteFileA(resultPath);return failure("worker-failed","result-size-limit","worker result exceeds the size limit");} std::string document(static_cast<size_t>(size),'\0');input.seekg(0);if(!document.empty())input.read(&document[0],static_cast<std::streamsize>(document.size()));DeleteFileA(requestPath);DeleteFileA(resultPath);
    diagnostics.clear(); if(!ParseCallResultJson(document,result,diagnostics)){result={};result.correlationId=request.correlationId;result.status="worker-failed";result.diagnostics=diagnostics;result.diagnostics.push_back({"malformed-result","worker","worker result was malformed"});error="worker result was malformed";return false;} return result.success;
}
