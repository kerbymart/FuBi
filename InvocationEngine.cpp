#include "stdafx.h"
#include "InvocationEngine.h"

#include <windows.h>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace
{
using Call0 = uint64_t(*)();
using Call1 = uint64_t(*)(uint64_t);
using Call2 = uint64_t(*)(uint64_t,uint64_t);
using Call3 = uint64_t(*)(uint64_t,uint64_t,uint64_t);
using Call4 = uint64_t(*)(uint64_t,uint64_t,uint64_t,uint64_t);
using Call5 = uint64_t(*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
using Call6 = uint64_t(*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
using Call7 = uint64_t(*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
using Call8 = uint64_t(*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);

bool Number(const std::string& text, int base, uint64_t& value)
{ if(text.empty()) return false; errno=0; char* end=nullptr; const unsigned long long parsed=std::strtoull(text.c_str(),&end,base); if(errno==ERANGE||end==text.c_str()||*end!='\0') return false; value=static_cast<uint64_t>(parsed); return true; }

bool Value(const CallArgument& argument, uint64_t& value)
{
    if (argument.type.kind == TypeKind::Bool) { if (argument.value == "false") { value=0; return true; } if (argument.value == "true") { value=1; return true; } return false; }
    if (argument.type.kind == TypeKind::Pointer) { if (argument.value.rfind("opaque:", 0) != 0) return false; return Number(argument.value.substr(7), 0, value); }
    if (argument.type.isSigned) { if (argument.value.empty()) return false; errno=0; char* end=nullptr; const long long parsed=std::strtoll(argument.value.c_str(),&end,0); if(errno==ERANGE||end==argument.value.c_str()||*end!='\0') return false; value=static_cast<uint64_t>(parsed); return true; }
    return Number(argument.value, 0, value);
}

std::string ReturnValue(uint64_t value, const TypeSpec& type)
{
    if (type.kind == TypeKind::Bool) return value ? "true" : "false";
    if (type.kind == TypeKind::Pointer) return "opaque:0x" + [&] { std::ostringstream out; out << std::hex << value; return out.str(); }();
    if (type.isSigned && type.width > 0 && type.width < 64) { const uint64_t mask=(uint64_t(1)<<type.width)-1; value &= mask; const uint64_t sign=(uint64_t(1)<<(type.width-1)); if(value&sign) value |= ~mask; }
    if (type.isSigned) return std::to_string(static_cast<int64_t>(value));
    return std::to_string(value);
}

bool SameIdentity(const ModuleIdentity& left, const ModuleIdentity& right)
{ return left.canonicalPath==right.canonicalPath && left.sha256==right.sha256 && left.architecture==right.architecture && left.timestamp==right.timestamp && left.imageSize==right.imageSize && left.preferredImageBase==right.preferredImageBase && left.pdbGuid==right.pdbGuid && left.pdbAge==right.pdbAge; }

struct CallContext
{
    FARPROC address;
    const uint64_t* values;
    size_t count;
    uint64_t returned;
    int exceptionCode;
};

DWORD WINAPI CallWorker(void* raw)
{
    CallContext* context=static_cast<CallContext*>(raw);
    __try {
        switch(context->count){case 0:context->returned=reinterpret_cast<Call0>(context->address)();break;case 1:context->returned=reinterpret_cast<Call1>(context->address)(context->values[0]);break;case 2:context->returned=reinterpret_cast<Call2>(context->address)(context->values[0],context->values[1]);break;case 3:context->returned=reinterpret_cast<Call3>(context->address)(context->values[0],context->values[1],context->values[2]);break;case 4:context->returned=reinterpret_cast<Call4>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3]);break;case 5:context->returned=reinterpret_cast<Call5>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4]);break;case 6:context->returned=reinterpret_cast<Call6>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4],context->values[5]);break;case 7:context->returned=reinterpret_cast<Call7>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4],context->values[5],context->values[6]);break;default:context->returned=reinterpret_cast<Call8>(context->address)(context->values[0],context->values[1],context->values[2],context->values[3],context->values[4],context->values[5],context->values[6],context->values[7]);break;}
    } __except(context->exceptionCode=GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { }
    return 0;
}
}

bool InvokeX64Export(const std::string& imagePath, const CallRequest& request,
    const FunctionCatalog& catalog, CallResult& result, std::string& error)
{
#if !defined(_M_X64)
    (void)imagePath; (void)request; (void)catalog; (void)result; error = "x64 invocation requires an x64 build"; return false;
#else
    std::vector<CallDiagnostic> diagnostics;
    if (!ValidateCallRequest(request, catalog, diagnostics)) { result = {}; result.correlationId=request.correlationId; result.status="validation-failed"; result.diagnostics=diagnostics; error="call request validation failed"; return false; }
    for (const CallArgument& argument : request.arguments)
    {
        if (argument.type.kind != TypeKind::Integer && argument.type.kind != TypeKind::Bool && argument.type.kind != TypeKind::Pointer)
        {
            result = {}; result.correlationId = request.correlationId; result.status = "validation-failed";
            result.diagnostics.push_back({"unsupported-type", "arguments", "x64 adapter supports integer, bool, and pointer values only"});
            error = "unsupported x64 argument type";
            return false;
        }
        if (argument.type.direction != ParameterDirection::In || argument.bufferSize != 0 || !argument.ownership.empty())
        {
            result = {}; result.correlationId=request.correlationId; result.status="validation-failed";
            result.diagnostics.push_back({"unsupported-output-descriptor", "arguments", "x64 adapter currently accepts input scalar values only"});
            error="output buffers and ownership descriptors are not supported by this adapter";
            return false;
        }
    }
    FunctionRecord const* selected=catalog.Find(request.selector);
    if (selected == nullptr) { error="function selector is unavailable"; return false; }
    if (selected->exportNames.empty()) { error="internal targets are not supported by the export invocation engine"; return false; }
    const PrototypeSpec prototype=request.hasPrototypeOverride?request.prototypeOverride:selected->prototype;
    if (prototype.returnType.kind == TypeKind::Floating || prototype.returnType.kind == TypeKind::Structure || prototype.returnType.kind == TypeKind::Void)
    { result={}; result.correlationId=request.correlationId; result.status="validation-failed"; result.diagnostics.push_back({"unsupported-return-type","prototype.return_type","x64 adapter supports scalar integer, bool, and pointer returns only"}); error="unsupported x64 return type"; return false; }
    FunctionCatalog current; if (!FunctionCatalog::Load(imagePath, current, error) || !SameIdentity(current.Module(), catalog.Module())) { error = "runtime module identity changed"; return false; }
    const FunctionRecord* record=selected;
    HMODULE module=LoadLibraryExA(imagePath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS); if(module==nullptr){error="unable to load target module";return false;}
    FARPROC address=GetProcAddress(module,record->exportNames.front().c_str());
    if(address==nullptr){FreeLibrary(module);error="export address is unavailable";return false;}
    uint64_t values[8]={}; if(request.arguments.size()>8){FreeLibrary(module);error="x64 adapter supports at most eight arguments";return false;}
    for(size_t i=0;i<request.arguments.size();++i) if(!Value(request.arguments[i],values[i])) { FreeLibrary(module); error="invalid typed argument"; return false; }
    const uintptr_t moduleBase=reinterpret_cast<uintptr_t>(module); const uintptr_t functionAddress=reinterpret_cast<uintptr_t>(address);
    if (functionAddress < moduleBase || functionAddress-moduleBase > UINT32_MAX || static_cast<uint32_t>(functionAddress-moduleBase) != record->startRva) { FreeLibrary(module); error="resolved export does not match static RVA"; return false; }
    CallContext context{address, values, request.arguments.size(), 0, 0};
    HANDLE worker=CreateThread(nullptr, 0, CallWorker, &context, 0, nullptr);
    if (worker == nullptr) { FreeLibrary(module); error="unable to create invocation worker"; return false; }
    const DWORD timeout=request.timeoutMs == 0 ? 30000U : request.timeoutMs;
    const DWORD wait=WaitForSingleObject(worker, timeout);
    if (wait == WAIT_TIMEOUT)
    {
        TerminateThread(worker, ERROR_TIMEOUT); CloseHandle(worker);
        result={}; result.correlationId=request.correlationId; result.status="timed-out"; result.diagnostics.push_back({"timeout","timeout_ms","target exceeded the invocation timeout"});
        error="target invocation timed out";
        return false;
    }
    CloseHandle(worker);
    const uint64_t returned=context.returned;
    if (context.exceptionCode != 0) { FreeLibrary(module); result={}; result.correlationId=request.correlationId; result.status="crashed"; result.diagnostics.push_back({"target-exception","call","target raised a structured exception"}); error="target raised a structured exception"; return false; }
    FreeLibrary(module); result={}; result.correlationId=request.correlationId; result.success=true; result.status="completed"; result.returnValue=ReturnValue(returned,prototype.returnType); result.returnType=prototype.returnType; result.prototypeUsed=prototype; result.resolvedModule=catalog.Module(); return true;
#endif
}
