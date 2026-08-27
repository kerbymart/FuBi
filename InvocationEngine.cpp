#include "stdafx.h"
#include "InvocationEngine.h"

#include <windows.h>
#include <cerrno>
#include <cstdlib>
#include <limits>

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

bool Value(const std::string& text, uint64_t& value)
{ if(text.empty())return false; errno=0; char* end=nullptr; const unsigned long long parsed=std::strtoull(text.c_str(),&end,0); if(errno==ERANGE||end==text.c_str()||*end!='\0')return false; value=static_cast<uint64_t>(parsed); return true; }
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
    }
    FunctionCatalog current; if (!FunctionCatalog::Load(imagePath, current, error) || current.Module().sha256 != catalog.Module().sha256 || current.Module().architecture != catalog.Module().architecture) { error = "runtime module identity changed"; return false; }
    const FunctionRecord* record=catalog.Find(request.selector); if(record==nullptr){error="function selector is unavailable";return false;}
    HMODULE module=LoadLibraryExA(imagePath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS); if(module==nullptr){error="unable to load target module";return false;}
    FARPROC address=nullptr; if(!record->exportNames.empty()) address=GetProcAddress(module,record->exportNames.front().c_str()); else if(record->id.ordinal!=0) address=GetProcAddress(module,MAKEINTRESOURCEA(record->id.ordinal));
    if(address==nullptr){FreeLibrary(module);error="export address is unavailable";return false;}
    const PrototypeSpec prototype=request.hasPrototypeOverride?request.prototypeOverride:record->prototype; uint64_t values[8]={}; if(request.arguments.size()>8){FreeLibrary(module);error="x64 adapter supports at most eight arguments";return false;}
    for(size_t i=0;i<request.arguments.size();++i){const TypeKind kind=request.arguments[i].type.kind;if(kind!=TypeKind::Integer&&kind!=TypeKind::Bool&&kind!=TypeKind::Pointer){FreeLibrary(module);error="x64 adapter supports integer, bool, and pointer arguments only";return false;}if(!Value(request.arguments[i].value,values[i])){FreeLibrary(module);error="invalid integer argument";return false;}}
    uint64_t returned=0; switch(request.arguments.size()){case 0:returned=reinterpret_cast<Call0>(address)();break;case 1:returned=reinterpret_cast<Call1>(address)(values[0]);break;case 2:returned=reinterpret_cast<Call2>(address)(values[0],values[1]);break;case 3:returned=reinterpret_cast<Call3>(address)(values[0],values[1],values[2]);break;case 4:returned=reinterpret_cast<Call4>(address)(values[0],values[1],values[2],values[3]);break;case 5:returned=reinterpret_cast<Call5>(address)(values[0],values[1],values[2],values[3],values[4]);break;case 6:returned=reinterpret_cast<Call6>(address)(values[0],values[1],values[2],values[3],values[4],values[5]);break;case 7:returned=reinterpret_cast<Call7>(address)(values[0],values[1],values[2],values[3],values[4],values[5],values[6]);break;default:returned=reinterpret_cast<Call8>(address)(values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7]);break;}
    FreeLibrary(module); result={}; result.correlationId=request.correlationId; result.success=true; result.status="completed"; result.returnValue=std::to_string(returned); result.returnType=prototype.returnType; result.prototypeUsed=prototype; result.resolvedModule=catalog.Module(); return true;
#endif
}
