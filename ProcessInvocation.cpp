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
    std::vector<CallDiagnostic> diagnostics;
    if (!ValidateCallRequest(request,catalog,diagnostics)) { result={}; result.correlationId=request.correlationId; result.status="validation-failed"; result.diagnostics=diagnostics; error="call request validation failed"; return false; }
    char tempPath[MAX_PATH]={}; if(!GetTempPathA(MAX_PATH,tempPath)){error="unable to locate temporary directory";return false;}
    char requestPath[MAX_PATH]={}; char resultPath[MAX_PATH]={}; if(!GetTempFileNameA(tempPath,"fbi",0,requestPath)||!GetTempFileNameA(tempPath,"fbo",0,resultPath)){error="unable to create worker IPC files";return false;}
    { std::ofstream output(requestPath,std::ios::binary|std::ios::trunc); if(!output){DeleteFileA(requestPath);DeleteFileA(resultPath);error="unable to write worker request";return false;} WriteCallRequestJson(output,request); }
    std::string command=Quote(WorkerPath())+" "+Quote(imagePath)+" "+Quote(requestPath)+" "+Quote(resultPath); std::vector<char> commandLine(command.begin(),command.end()); commandLine.push_back('\0'); STARTUPINFOA startup={}; startup.cb=sizeof(startup); PROCESS_INFORMATION process={};
    if(!CreateProcessA(nullptr,commandLine.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)){DeleteFileA(requestPath);DeleteFileA(resultPath);error="unable to start invocation worker";return false;}
    const DWORD timeout=request.timeoutMs==0?30000U:request.timeoutMs; const DWORD wait=WaitForSingleObject(process.hProcess,timeout);
    if(wait==WAIT_TIMEOUT){TerminateProcess(process.hProcess,ERROR_TIMEOUT);WaitForSingleObject(process.hProcess,5000);CloseHandle(process.hThread);CloseHandle(process.hProcess);DeleteFileA(requestPath);DeleteFileA(resultPath);result={};result.correlationId=request.correlationId;result.status="timed-out";result.diagnostics.push_back({"timeout","timeout_ms","worker process exceeded the invocation timeout"});error="invocation worker timed out";return false;}
    if(wait==WAIT_FAILED){TerminateProcess(process.hProcess,ERROR_OPERATION_ABORTED);CloseHandle(process.hThread);CloseHandle(process.hProcess);DeleteFileA(requestPath);DeleteFileA(resultPath);error="unable to wait for invocation worker";return false;}
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    std::ifstream input(resultPath,std::ios::binary|std::ios::ate); if(!input){DeleteFileA(requestPath);DeleteFileA(resultPath);error="worker did not produce a result";return false;} const std::streamoff size=input.tellg(); if(size<0||size>4*1024*1024){DeleteFileA(requestPath);DeleteFileA(resultPath);error="worker result exceeds the size limit";return false;} std::string document(static_cast<size_t>(size),'\0');input.seekg(0);if(!document.empty())input.read(&document[0],static_cast<std::streamsize>(document.size()));DeleteFileA(requestPath);DeleteFileA(resultPath);
    diagnostics.clear(); if(!ParseCallResultJson(document,result,diagnostics)){result={};result.correlationId=request.correlationId;result.status="worker-failed";result.diagnostics=diagnostics;error="worker result was malformed";return false;} return result.success;
}
