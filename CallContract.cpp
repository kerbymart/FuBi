#include "stdafx.h"
#include "CallContract.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <cctype>
#include <ostream>
#include <sstream>
#include <set>
#include <functional>

namespace
{
constexpr uint64_t kMaximumBufferBytes = 16 * 1024 * 1024;
void Diagnostic(std::vector<CallDiagnostic>& out, const char* code, const std::string& path, const char* message)
{ out.push_back({code, path, message}); }
void Json(std::ostream& out, const std::string& value)
{
    out << '"';
    for (unsigned char c : value) { if (c == '"' || c == '\\') out << '\\' << c; else if (c == '\n') out << "\\n"; else if (c == '\r') out << "\\r"; else if (c == '\t') out << "\\t"; else if (c < 0x20) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(c) << std::dec; else out << c; }
    out << '"';
}
void WriteType(std::ostream& out, const TypeSpec& type)
{
    out << "{\"kind\":"; Json(out, TypeKindName(type.kind)); out << ",\"width\":" << type.width << ",\"signed\":" << (type.isSigned ? "true" : "false") << ",\"pointer_depth\":" << static_cast<unsigned>(type.pointerDepth) << ",\"direction\":"; Json(out, type.direction == ParameterDirection::In ? "in" : type.direction == ParameterDirection::Out ? "out" : "inout"); out << ",\"element_count\":" << type.elementCount << ",\"encoding\":"; Json(out, type.encoding); out << ",\"ownership\":"; Json(out, type.ownership); out << ",\"layout\":"; Json(out, type.layout); out << ",\"release_adapter\":"; Json(out, type.releaseAdapter); out << '}';
}
void WritePrototype(std::ostream& out, const PrototypeSpec& prototype)
{
    out << "{\"abi\":"; Json(out, prototype.abi); out << ",\"source\":"; Json(out, prototype.source); out << ",\"return_type\":"; WriteType(out, prototype.returnType); out << ",\"parameters\":["; for (size_t i=0;i<prototype.parameters.size();++i) { if(i) out << ','; WriteType(out, prototype.parameters[i]); } out << "],\"variadic\":" << (prototype.variadic ? "true" : "false") << ",\"quality\":"; Json(out, PrototypeQualityName(prototype.quality)); out << '}';
}
bool EqualType(const TypeSpec& left, const TypeSpec& right)
{ return left.kind == right.kind && left.width == right.width && left.isSigned == right.isSigned && left.pointerDepth == right.pointerDepth && left.direction == right.direction && left.elementCount == right.elementCount && left.encoding == right.encoding && left.ownership == right.ownership && left.layout == right.layout && left.releaseAdapter == right.releaseAdapter; }
bool Integer(const std::string& text, uint16_t width, bool signedValue)
{
    if (text.empty() || width == 0 || width > 64) return false;
    errno = 0; char* end = nullptr;
    if (signedValue) { const long long value = std::strtoll(text.c_str(), &end, 0); if (errno == ERANGE || end == text.c_str() || *end != '\0') return false; if (width == 64) return true; const long long maximum = (1LL << (width - 1)) - 1; const long long minimum = -(1LL << (width - 1)); return value >= minimum && value <= maximum; }
    if (text.front() == '-') return false; const unsigned long long value = std::strtoull(text.c_str(), &end, 0); if (errno == ERANGE || end == text.c_str() || *end != '\0') return false; if (width == 64) return true; return value <= ((1ULL << width) - 1);
}
bool ArgumentValue(const CallArgument& argument)
{
    switch (argument.type.kind)
    {
    case TypeKind::Bool: return argument.value == "true" || argument.value == "false";
    case TypeKind::Integer: return Integer(argument.value, argument.type.width, argument.type.isSigned);
    case TypeKind::Floating: { if (argument.value.empty()) return false; char* end=nullptr; errno=0; const double value=std::strtod(argument.value.c_str(),&end); return errno != ERANGE && end != argument.value.c_str() && *end == '\0' && std::isfinite(value); }
    case TypeKind::String:
        if (argument.type.pointerDepth != 1 || argument.value.find('\0') != std::string::npos) return false;
        if (argument.type.direction != ParameterDirection::In && argument.bufferSize == 0) return false;
        if (argument.type.direction != ParameterDirection::In &&
            (argument.type.encoding == "utf16" || argument.type.encoding == "wstr") &&
            argument.bufferSize % 2 != 0) return false;
        if (argument.type.direction == ParameterDirection::Out && !argument.value.empty()) return false;
        if (argument.value.size() > static_cast<size_t>(INT_MAX)) return false;
        if (!argument.value.empty() && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argument.value.data(), static_cast<int>(argument.value.size()), nullptr, 0) <= 0) return false;
        return
            (argument.type.encoding == "cstr" || argument.type.encoding == "utf8" || argument.type.encoding == "utf16" || argument.type.encoding == "wstr") && argument.value.size() <= kMaximumBufferBytes;
    case TypeKind::Bytes:
        if (argument.type.pointerDepth != 1) return false;
        if (argument.type.direction == ParameterDirection::Out && argument.value.empty()) return true;
        return argument.value.size() % 2 == 0 && std::all_of(argument.value.begin(), argument.value.end(), [](char c) {
            return std::isxdigit(static_cast<unsigned char>(c)) != 0;
        });
    case TypeKind::Pointer: return argument.type.pointerDepth > 0 && argument.value.rfind("opaque:", 0) == 0;
    case TypeKind::Handle: return argument.type.pointerDepth == 0 && argument.value.rfind("opaque:", 0) == 0;
    default: return false;
    }
}
bool ValidType(const TypeSpec& type)
{
    if (type.pointerDepth > 8) return false;
    switch (type.kind) { case TypeKind::Bool: return type.width == 1; case TypeKind::Integer: return type.width == 8 || type.width == 16 || type.width == 32 || type.width == 64; case TypeKind::Floating: return type.width == 32 || type.width == 64; case TypeKind::String: return type.pointerDepth == 1; case TypeKind::Bytes: return type.pointerDepth == 1 && type.width == 8; case TypeKind::Pointer: return type.pointerDepth != 0; case TypeKind::Handle: return (type.width == 32 || type.width == 64) && type.pointerDepth == 0 && (type.ownership == "borrowed" || type.ownership == "owned") && (type.releaseAdapter.empty() || type.releaseAdapter == "CloseHandle") && (type.ownership != "owned" || !type.releaseAdapter.empty()); case TypeKind::Structure: return type.width != 0; case TypeKind::Void: return type.width == 0 && type.pointerDepth == 0; default: return false; }
}
bool ValidPrototype(const PrototypeSpec& prototype)
{ return !prototype.abi.empty() && (prototype.quality == PrototypeQuality::UserDeclared || prototype.quality == PrototypeQuality::ExactSymbol) && ValidType(prototype.returnType) && std::all_of(prototype.parameters.begin(), prototype.parameters.end(), ValidType); }
bool ValidAbiForModule(const std::string& abi, const std::string& architecture)
{ const bool x64 = abi == "x64" || abi == "win64"; const bool x86 = abi == "__cdecl" || abi == "__stdcall" || abi == "__thiscall" || abi == "__fastcall"; return (architecture == "x64" && x64) || (architecture == "x86" && x86); }
bool StringField(const std::string& object, const char* key, std::string& value)
{ const std::string token = std::string("\"") + key + "\":\""; const size_t begin = object.find(token); if (begin == std::string::npos) return false; const size_t start = begin + token.size(); std::string decoded; bool escaped=false; for(size_t i=start;i<object.size();++i){const char c=object[i];if(escaped){switch(c){case 'n':decoded+='\n';break;case 'r':decoded+='\r';break;case 't':decoded+='\t';break;case '"':case '\\':case '/':decoded+=c;break;default:return false;}escaped=false;}else if(c=='\\')escaped=true;else if(c=='"'){value=decoded;return true;}else decoded+=c;} return false; }
bool UIntField(const std::string& object, const char* key, uint64_t& value)
{ const std::string token = std::string("\"") + key + "\":"; const size_t begin = object.find(token); if (begin == std::string::npos) return false; size_t start = begin + token.size(), end = start; while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end]))) ++end; if (end == start) return false; value = 0; for (; start < end; ++start) { const uint64_t digit = static_cast<unsigned>(object[start]-'0'); if (value > (UINT64_MAX-digit)/10) return false; value=value*10+digit; } return true; }
bool BoolField(const std::string& object, const char* key, bool& value)
{ const std::string token = std::string("\"") + key + "\":"; const size_t begin=object.find(token); if(begin==std::string::npos) return false; const size_t start=begin+token.size(); if(object.compare(start,4,"true")==0){value=true;return true;} if(object.compare(start,5,"false")==0){value=false;return true;} return false; }
bool TypeObject(const std::string& object, TypeSpec& type)
{ std::string kind; uint64_t n=0; if(!StringField(object,"kind",kind) || !UIntField(object,"width",n) || n>UINT16_MAX) return false; const std::map<std::string,TypeKind> kinds={{"void",TypeKind::Void},{"bool",TypeKind::Bool},{"integer",TypeKind::Integer},{"floating",TypeKind::Floating},{"string",TypeKind::String},{"bytes",TypeKind::Bytes},{"pointer",TypeKind::Pointer},{"handle",TypeKind::Handle},{"structure",TypeKind::Structure}}; auto found=kinds.find(kind); if(found==kinds.end()) return false; type.kind=found->second; type.width=static_cast<uint16_t>(n); if(UIntField(object,"pointer_depth",n) && n<=UINT8_MAX) type.pointerDepth=static_cast<uint8_t>(n); BoolField(object,"signed",type.isSigned); StringField(object,"encoding",type.encoding); StringField(object,"ownership",type.ownership); StringField(object,"layout",type.layout); StringField(object,"release_adapter",type.releaseAdapter); UIntField(object,"element_count",type.elementCount); std::string direction; if(StringField(object,"direction",direction)){if(direction=="out")type.direction=ParameterDirection::Out;else if(direction=="inout")type.direction=ParameterDirection::InOut;} return ValidType(type); }
bool NextObject(const std::string& text, size_t& position, std::string& object)
{ const size_t start=text.find('{',position); if(start==std::string::npos)return false; size_t depth=0; bool quoted=false,escaped=false; for(size_t i=start;i<text.size();++i){const char c=text[i];if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}if(c=='"'){quoted=true;continue;}if(c=='{')++depth;else if(c=='}'&&--depth==0){object=text.substr(start,i-start+1);position=i+1;return true;}}return false; }
bool PrototypeObject(const std::string& object, PrototypeSpec& prototype)
{ std::string quality; if(!StringField(object,"abi",prototype.abi)||!StringField(object,"quality",quality))return false; StringField(object,"source",prototype.source); prototype.quality=quality=="exact-symbol"?PrototypeQuality::ExactSymbol:quality=="user-declared"?PrototypeQuality::UserDeclared:quality=="inferred"?PrototypeQuality::Inferred:PrototypeQuality::Unknown; size_t pos=object.find("\"return_type\":"); std::string type; if(pos==std::string::npos||!NextObject(object,pos,type)||!TypeObject(type,prototype.returnType))return false; pos=object.find("\"parameters\":["); if(pos!=std::string::npos){pos+=14; while(true){std::string item; if(!NextObject(object,pos,item))break; TypeSpec parsed; if(!TypeObject(item,parsed))return false; prototype.parameters.push_back(parsed); if(object.find(']',pos)!=std::string::npos&&object.find(']',pos)<object.find('{',pos))break;}} BoolField(object,"variadic",prototype.variadic); return ValidPrototype(prototype); }

bool StrictTopLevel(const std::string& document, std::vector<CallDiagnostic>& diagnostics)
{
    size_t first=0; while(first<document.size() && std::isspace(static_cast<unsigned char>(document[first]))) ++first;
    if(first==document.size() || document[first]!='{') { Diagnostic(diagnostics,"invalid-json","$","request must be one JSON object"); return false; }
    std::function<bool(size_t&, size_t)> value;
    std::function<bool(size_t&)> stringValue = [&](size_t& position) { if(position>=document.size()||document[position++]!='"') return false; bool escaped=false; while(position<document.size()){const char c=document[position++];if(escaped){if(c=='u'){if(position+4>document.size())return false;for(size_t i=0;i<4;++i){const char h=document[position++];if(!std::isxdigit(static_cast<unsigned char>(h)))return false;}}escaped=false;}else if(c=='\\')escaped=true;else if(c=='"')return true;else if(static_cast<unsigned char>(c)<0x20)return false;}return false; };
    auto validNumber = [](const std::string& token) { size_t i=0; if(i<token.size()&&token[i]=='-')++i; if(i==token.size())return false; if(token[i]=='0')++i; else {if(!std::isdigit(static_cast<unsigned char>(token[i])))return false;while(i<token.size()&&std::isdigit(static_cast<unsigned char>(token[i])))++i;} if(i<token.size()&&token[i]=='.'){++i;const size_t start=i;while(i<token.size()&&std::isdigit(static_cast<unsigned char>(token[i])))++i;if(i==start)return false;} if(i<token.size()&&(token[i]=='e'||token[i]=='E')){++i;if(i<token.size()&&(token[i]=='+'||token[i]=='-'))++i;const size_t start=i;while(i<token.size()&&std::isdigit(static_cast<unsigned char>(token[i])))++i;if(i==start)return false;}return i==token.size(); };
    value = [&](size_t& position, size_t depth) { if(depth>32)return false; while(position<document.size()&&std::isspace(static_cast<unsigned char>(document[position])))++position; if(position>=document.size())return false; const char c=document[position]; if(c=='"')return stringValue(position); if(c=='{'){++position;while(true){while(position<document.size()&&std::isspace(static_cast<unsigned char>(document[position])))++position;if(position<document.size()&&document[position]=='}'){++position;return true;}if(!stringValue(position))return false;while(position<document.size()&&std::isspace(static_cast<unsigned char>(document[position])))++position;if(position>=document.size()||document[position++]!=':')return false;if(!value(position,depth+1))return false;while(position<document.size()&&std::isspace(static_cast<unsigned char>(document[position])))++position;if(position<document.size()&&document[position]==','){++position;continue;}if(position<document.size()&&document[position]=='}'){++position;return true;}return false;}} if(c=='['){++position;while(true){while(position<document.size()&&std::isspace(static_cast<unsigned char>(document[position])))++position;if(position<document.size()&&document[position]==']'){++position;return true;}if(!value(position,depth+1))return false;while(position<document.size()&&std::isspace(static_cast<unsigned char>(document[position])))++position;if(position<document.size()&&document[position]==','){++position;continue;}if(position<document.size()&&document[position]==']'){++position;return true;}return false;}} const size_t start=position;while(position<document.size()&&!std::isspace(static_cast<unsigned char>(document[position]))&&document[position]!=','&&document[position]!=']'&&document[position]!='}')++position;const std::string token=document.substr(start,position-start);return token=="true"||token=="false"||token=="null"||validNumber(token); };
    size_t end=first; if(!value(end,0)) { Diagnostic(diagnostics,"invalid-json","$","request contains malformed JSON"); return false; }
    while(end<document.size() && std::isspace(static_cast<unsigned char>(document[end]))) ++end;
    if(end!=document.size()) { Diagnostic(diagnostics,"trailing-garbage","$","trailing text after request object is not allowed"); return false; }
    const std::set<std::string> allowed={"schema_version","action","correlation_id","selector","reference","module_sha256","module_path","module_timestamp","module_image_size","module_preferred_image_base","module_pdb_guid","module_pdb_age","authorization_provenance","internal_authorization","timeout_ms","allow_internal","has_prototype_override","prototype_override","arguments"};
    std::map<std::string,unsigned> counts; int depth=0; bool quoted=false,escaped=false;
    for(size_t i=first;i<end;++i){const char c=document[i];if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}if(c=='"'){size_t keyStart=i+1,keyEnd=keyStart;while(keyEnd<end&&document[keyEnd]!='"')++keyEnd;size_t colon=keyEnd+1;while(colon<end&&std::isspace(static_cast<unsigned char>(document[colon])))++colon;if(depth==1&&keyEnd<end&&colon<end&&document[colon]==':'){const std::string key=document.substr(keyStart,keyEnd-keyStart);if(!allowed.count(key))Diagnostic(diagnostics,"unknown-field",key,"unknown top-level field");if(++counts[key]>1)Diagnostic(diagnostics,"duplicate-field",key,"duplicate top-level field");}quoted=true;continue;}if(c=='{')++depth;else if(c=='}')--depth;}
    const size_t argumentsKey=document.find("\"arguments\""); if(argumentsKey!=std::string::npos){size_t colon=document.find(':',argumentsKey);while(colon!=std::string::npos&&++colon<end&&std::isspace(static_cast<unsigned char>(document[colon]))){}if(colon==std::string::npos||colon>=end||document[colon]!='[')Diagnostic(diagnostics,"invalid-json","arguments","arguments must be an array");}
    return diagnostics.empty();
}
}

bool ValidateCallRequest(const CallRequest& request, const FunctionCatalog& catalog,
    std::vector<CallDiagnostic>& diagnostics)
{
    diagnostics.clear();
    if (request.schemaVersion != CallRequest::kSchemaVersion) Diagnostic(diagnostics, "unsupported-schema", "schema_version", "supported version is 1");
    if (request.correlationId.empty() || request.correlationId.size() > 128) Diagnostic(diagnostics, "invalid-correlation-id", "correlation_id", "correlation ID must be 1..128 characters");
    if (request.selector.empty()) Diagnostic(diagnostics, "missing-selector", "selector", "function selector is required");
    const FunctionRecord* record = request.selector.empty() ? nullptr : catalog.Find(request.selector);
    if (request.allowInternal && (record == nullptr || !catalog.HasTrustedInternalAuthorization(record->startRva, request.authorizationProvenance) || request.moduleSha256 != catalog.Module().sha256 || request.modulePath != catalog.Module().canonicalPath || request.moduleTimestamp != catalog.Module().timestamp || request.moduleImageSize != catalog.Module().imageSize || request.modulePreferredImageBase != catalog.Module().preferredImageBase || request.modulePdbGuid != catalog.Module().pdbGuid || request.modulePdbAge != catalog.Module().pdbAge)) Diagnostic(diagnostics, "module-identity-required", "module_identity", "internal calls require catalog-owned profile authorization and complete identity");
    if (record == nullptr) Diagnostic(diagnostics, "selector-not-found-or-ambiguous", "selector", "selector must identify exactly one catalog record");
    if (record != nullptr && !request.allowInternal && record->exportNames.empty()) Diagnostic(diagnostics, "internal-policy-required", "selector", "internal targets require explicit policy");
    if (record != nullptr && record->callability == Callability::Forwarded) Diagnostic(diagnostics, "forwarded-target", "selector", "forwarded exports are not directly addressable");
    if (record != nullptr && record->callability == Callability::FrameworkManaged) Diagnostic(diagnostics, "framework-managed", "selector", "framework-managed entry points are blocked");
    if (record != nullptr && (record->callability == Callability::UnsupportedAbi || record->callability == Callability::NotAddressable)) Diagnostic(diagnostics, "target-not-callable", "selector", "catalog callability does not authorize invocation");
    if (record != nullptr && record->callability == Callability::ArchitectureMismatch) Diagnostic(diagnostics, "architecture-mismatch", "selector", "catalog architecture does not support this target");
    PrototypeSpec prototype;
    if (request.hasPrototypeOverride) prototype = request.prototypeOverride;
    else if (record != nullptr && record->hasPrototype) prototype = record->prototype;
    else Diagnostic(diagnostics, "prototype-required", "prototype", "an invocation-grade prototype is required");
    if (request.hasPrototypeOverride && prototype.quality != PrototypeQuality::UserDeclared && prototype.quality != PrototypeQuality::ExactSymbol) Diagnostic(diagnostics, "prototype-not-invocation-grade", "prototype_override", "only user-declared or exact-symbol prototypes may be used");
    if (!request.hasPrototypeOverride && record != nullptr && record->hasPrototype && record->prototype.quality != PrototypeQuality::UserDeclared && record->prototype.quality != PrototypeQuality::ExactSymbol) Diagnostic(diagnostics, "prototype-not-invocation-grade", "prototype", "catalog evidence is display-only");
    if (request.hasPrototypeOverride && !ValidPrototype(prototype)) Diagnostic(diagnostics, "invalid-prototype-override", "prototype_override", "override must be a complete invocation-grade prototype");
    if ((request.hasPrototypeOverride || (record != nullptr && record->hasPrototype)) && !ValidAbiForModule(prototype.abi, catalog.Module().architecture)) Diagnostic(diagnostics, "unsupported-abi", "prototype.abi", "ABI does not match module architecture");
    if (prototype.returnType.kind == TypeKind::Handle)
    {
        const uint16_t expectedWidth = catalog.Module().architecture == "x86" ? 32 : 64;
        if (prototype.returnType.width != expectedWidth)
            Diagnostic(diagnostics, "architecture-mismatch", "prototype.return_type.width", "handle width does not match module architecture");
    }
    if (!request.hasPrototypeOverride && record != nullptr && record->callability != Callability::Callable) Diagnostic(diagnostics, "target-not-callable", "selector", "catalog callability does not authorize invocation");
#if defined(_M_IX86)
    if (prototype.abi == "__thiscall")
    {
        if (request.arguments.empty())
            Diagnostic(diagnostics, "missing-object-pointer", "arguments[0]", "__thiscall requires a non-null opaque object pointer as its first argument");
        else
        {
            const CallArgument& object = request.arguments.front();
            bool validObject = object.type.kind == TypeKind::Pointer && object.type.pointerDepth == 1 &&
                object.type.direction == ParameterDirection::In && object.ownership.empty();
            if (validObject && object.value.rfind("opaque:", 0) == 0)
            {
                errno = 0;
                char* end = nullptr;
                const unsigned long long address = std::strtoull(object.value.c_str() + 7, &end, 0);
                validObject = errno != ERANGE && end != object.value.c_str() + 7 && *end == '\0' &&
                    address != 0 && address <= UINT32_MAX;
            }
            else validObject = false;
            if (!validObject)
                Diagnostic(diagnostics, "invalid-object-pointer", "arguments[0]", "__thiscall object pointer must be a non-null 32-bit opaque reference");
        }
    }
#endif
    if (record != nullptr && prototype.parameters.size() != request.arguments.size()) Diagnostic(diagnostics, "argument-count-mismatch", "arguments", "argument count does not match the selected prototype");
    if (record != nullptr && prototype.parameters.size() == request.arguments.size()) for (size_t index=0; index<request.arguments.size(); ++index)
    {
        const CallArgument& argument=request.arguments[index];
        if (!EqualType(argument.type, prototype.parameters[index])) Diagnostic(diagnostics, "argument-type-mismatch", "arguments["+std::to_string(index)+"].type", "argument type does not match the prototype");
        if (argument.type.kind == TypeKind::Bytes && argument.type.direction != ParameterDirection::In && argument.bufferSize == 0) Diagnostic(diagnostics, "missing-output-buffer", "arguments["+std::to_string(index)+"].buffer_size", "output buffers require an explicit size");
        if (!ArgumentValue(argument)) Diagnostic(diagnostics, "invalid-argument-value", "arguments["+std::to_string(index)+"].value", "value is invalid for its declared type");
        if (argument.bufferSize > kMaximumBufferBytes) Diagnostic(diagnostics, "buffer-too-large", "arguments["+std::to_string(index)+"].buffer_size", "buffer exceeds the hard cap");
        if (argument.type.elementCount != 0 && argument.type.width != 0 && argument.type.elementCount > kMaximumBufferBytes / argument.type.width) Diagnostic(diagnostics, "buffer-size-overflow", "arguments["+std::to_string(index)+"].type.element_count", "buffer size overflows the hard cap");
    }
    return diagnostics.empty();
}

void WriteCallRequestJson(std::ostream& output, const CallRequest& request)
{
    output << "{\"schema_version\":" << request.schemaVersion << ",\"action\":"; Json(output, request.action); output << ",\"correlation_id\":"; Json(output, request.correlationId); output << ",\"selector\":"; Json(output, request.selector); output << ",\"module_sha256\":"; Json(output, request.moduleSha256); output << ",\"module_path\":"; Json(output, request.modulePath); output << ",\"module_timestamp\":" << request.moduleTimestamp << ",\"module_image_size\":" << request.moduleImageSize << ",\"module_preferred_image_base\":" << request.modulePreferredImageBase << ",\"module_pdb_guid\":"; Json(output,request.modulePdbGuid); output << ",\"module_pdb_age\":" << request.modulePdbAge << ",\"authorization_provenance\":"; Json(output,request.authorizationProvenance); output << ",\"timeout_ms\":" << request.timeoutMs << ",\"allow_internal\":" << (request.allowInternal ? "true" : "false") << ",\"has_prototype_override\":" << (request.hasPrototypeOverride ? "true" : "false") << ",\"prototype_override\":"; if (request.hasPrototypeOverride) WritePrototype(output, request.prototypeOverride); else output << "null"; output << ",\"arguments\":[";
    for (size_t i=0;i<request.arguments.size();++i) { if(i) output << ','; output << "{\"type\":"; WriteType(output, request.arguments[i].type); output << ",\"value\":"; Json(output, request.arguments[i].value); output << ",\"buffer_size\":" << request.arguments[i].bufferSize << ",\"ownership\":"; Json(output, request.arguments[i].ownership); output << '}'; }
    output << "],\"reference\":"; Json(output, request.reference); output << "}";
}

void WriteCallResultJson(std::ostream& output, const CallResult& result)
{
    output << "{\"schema_version\":" << result.schemaVersion << ",\"action\":"; Json(output, result.action); output << ",\"correlation_id\":"; Json(output, result.correlationId); output << ",\"success\":" << (result.success ? "true" : "false") << ",\"status\":"; Json(output, result.status); output << ",\"return_value\":"; Json(output, result.returnValue); output << ",\"return_type\":"; if (result.returnType.kind == TypeKind::Unknown) output << "null"; else WriteType(output, result.returnType); output << ",\"duration_ms\":" << result.durationMs << ",\"resolved_module\":{\"path\":"; Json(output,result.resolvedModule.canonicalPath); output << ",\"sha256\":"; Json(output,result.resolvedModule.sha256); output << ",\"architecture\":"; Json(output,result.resolvedModule.architecture); output << ",\"timestamp\":" << result.resolvedModule.timestamp << ",\"image_size\":" << result.resolvedModule.imageSize << ",\"preferred_image_base\":" << result.resolvedModule.preferredImageBase << ",\"pdb_guid\":"; Json(output,result.resolvedModule.pdbGuid); output << ",\"pdb_age\":" << result.resolvedModule.pdbAge << "},\"prototype_used\":"; if (result.prototypeUsed.abi.empty()) output << "null"; else WritePrototype(output,result.prototypeUsed); output << ",\"output_values\":["; for(size_t i=0;i<result.outputValues.size();++i){if(i)output<<',';output<<"{\"type\":";WriteType(output,result.outputValues[i].type);output<<",\"value\":";Json(output,result.outputValues[i].value);output<<",\"buffer_size\":"<<result.outputValues[i].bufferSize<<",\"ownership\":";Json(output,result.outputValues[i].ownership);output<<"}";} output << "],\"worker_exit_code\":" << (result.hasWorkerExitCode ? std::to_string(result.workerExitCode) : "null") << ",\"diagnostics\":[";
    for (size_t i=0;i<result.diagnostics.size();++i) { if(i) output << ','; output << "{\"code\":"; Json(output,result.diagnostics[i].code); output << ",\"path\":"; Json(output,result.diagnostics[i].path); output << ",\"message\":"; Json(output,result.diagnostics[i].message); output << '}'; }
    output << "],\"issued_references\":["; for (size_t i=0;i<result.issuedReferences.size();++i) { if(i) output << ','; Json(output, result.issuedReferences[i]); } output << "],\"released_reference\":"; Json(output, result.releasedReference); output << "}";
}

void WriteCallResultText(std::ostream& output, const CallResult& result)
{
    output << "schema_version: " << result.schemaVersion << '\n'
        << "action: " << result.action << '\n'
        << "correlation_id: " << result.correlationId << '\n'
        << "status: " << result.status << '\n'
        << "success: " << (result.success ? "true" : "false") << '\n'
        << "return_value: " << result.returnValue << '\n'
        << "diagnostics: " << result.diagnostics.size() << '\n';
}

bool ParseCallRequestJson(const std::string& document, CallRequest& request, std::vector<CallDiagnostic>& diagnostics)
{
    request = {}; diagnostics.clear(); if(document.size()>4*1024*1024){Diagnostic(diagnostics,"size-limit","$","request is too large");return false;} if(!StrictTopLevel(document, diagnostics)) return false; uint64_t number=0; bool flag=false; std::string text;
    if(!UIntField(document,"schema_version",number)||number>UINT32_MAX) Diagnostic(diagnostics,"invalid-schema","schema_version","schema_version is invalid"); else request.schemaVersion=static_cast<uint32_t>(number);
    StringField(document,"action",request.action); if(request.action.empty()) request.action = "call";
    StringField(document,"reference",request.reference);
    if (request.action != "call" && request.action != "hello" && request.action != "list" && request.action != "describe" && request.action != "release" && request.action != "quit") Diagnostic(diagnostics,"unsupported-action","action","action is not supported");
    if(!StringField(document,"correlation_id",request.correlationId)) Diagnostic(diagnostics,"missing-field","correlation_id","correlation_id is required");
    if(!StringField(document,"selector",request.selector) && request.action == "call") Diagnostic(diagnostics,"missing-field","selector","selector is required for call actions");
    StringField(document,"module_sha256",request.moduleSha256);
    StringField(document,"module_path",request.modulePath); if(UIntField(document,"module_timestamp",number)&&number<=UINT32_MAX) request.moduleTimestamp=static_cast<uint32_t>(number); if(UIntField(document,"module_image_size",number)&&number<=UINT32_MAX) request.moduleImageSize=static_cast<uint32_t>(number); UIntField(document,"module_preferred_image_base",request.modulePreferredImageBase); StringField(document,"module_pdb_guid",request.modulePdbGuid); if(UIntField(document,"module_pdb_age",number)&&number<=UINT32_MAX) request.modulePdbAge=static_cast<uint32_t>(number); StringField(document,"authorization_provenance",request.authorizationProvenance); if(BoolField(document,"internal_authorization",flag)&&flag) Diagnostic(diagnostics,"untrusted-authorization","internal_authorization","boolean authorization is not accepted");
    if(UIntField(document,"timeout_ms",number)&&number<=UINT32_MAX) request.timeoutMs=static_cast<uint32_t>(number);
    if(BoolField(document,"allow_internal",flag)) request.allowInternal=flag;
    if(BoolField(document,"has_prototype_override",flag)&&flag) { request.hasPrototypeOverride=true; size_t pos=document.find("\"prototype_override\":"); std::string object; if(pos==std::string::npos||!NextObject(document,pos,object)||!PrototypeObject(object,request.prototypeOverride)) Diagnostic(diagnostics,"invalid-prototype-override","prototype_override","prototype override is invalid"); }
    size_t position=document.find("\"arguments\":["); if(position!=std::string::npos){position+=13; while(true){std::string object; if(!NextObject(document,position,object))break; const size_t typeStart=object.find("\"type\":"); if(typeStart==std::string::npos)break; size_t typePos=typeStart; std::string typeObject; if(!NextObject(object,typePos,typeObject))break; CallArgument argument; if(!TypeObject(typeObject,argument.type)||!StringField(object,"value",argument.value)) {Diagnostic(diagnostics,"invalid-argument","arguments","invalid typed argument");break;} UIntField(object,"buffer_size",argument.bufferSize); StringField(object,"ownership",argument.ownership); request.arguments.push_back(std::move(argument)); if(document.find(']',position)!=std::string::npos && document.find(']',position)<document.find('{',position))break; }}
    return diagnostics.empty();
}

bool ParseCallResultJson(const std::string& document, CallResult& result, std::vector<CallDiagnostic>& diagnostics)
{
    result={}; diagnostics.clear(); uint64_t number=0; bool flag=false;
    if(!UIntField(document,"schema_version",number)||number>UINT32_MAX) Diagnostic(diagnostics,"invalid-schema","schema_version","schema_version is invalid"); else result.schemaVersion=static_cast<uint32_t>(number);
    StringField(document,"action",result.action); if (result.action.empty()) result.action = "call";
    if(!StringField(document,"correlation_id",result.correlationId))Diagnostic(diagnostics,"missing-field","correlation_id","correlation_id is required");
    if(BoolField(document,"success",flag))result.success=flag;
    if(!StringField(document,"status",result.status))Diagnostic(diagnostics,"missing-field","status","status is required");
    StringField(document,"return_value",result.returnValue); StringField(document,"released_reference",result.releasedReference); size_t references=document.find("\"issued_references\":["); if (references != std::string::npos) { references += 21; while (references < document.size() && document[references] != ']') { if (document[references] != '\"') { ++references; continue; } const size_t end=document.find('\"', references + 1); if (end == std::string::npos) break; result.issuedReferences.push_back(document.substr(references + 1, end - references - 1)); references = end + 1; } } UIntField(document,"duration_ms",result.durationMs); if (UIntField(document,"worker_exit_code",number) && number <= UINT32_MAX) { result.hasWorkerExitCode = true; result.workerExitCode = static_cast<uint32_t>(number); }
    size_t pos=document.find("\"return_type\":"); std::string object;
    if(document.find("\"return_type\":null")!=std::string::npos) { result.returnType = {}; } else if(pos!=std::string::npos && NextObject(document,pos,object)) { if(!TypeObject(object,result.returnType)) Diagnostic(diagnostics,"invalid-return-type","return_type","return type is malformed"); } else Diagnostic(diagnostics,"missing-field","return_type","return type is required");
    pos=document.find("\"resolved_module\":"); if(pos!=std::string::npos && NextObject(document,pos,object)){StringField(object,"path",result.resolvedModule.canonicalPath);StringField(object,"sha256",result.resolvedModule.sha256);StringField(object,"architecture",result.resolvedModule.architecture);if(UIntField(object,"timestamp",number)&&number<=UINT32_MAX)result.resolvedModule.timestamp=static_cast<uint32_t>(number);if(UIntField(object,"image_size",number)&&number<=UINT32_MAX)result.resolvedModule.imageSize=static_cast<uint32_t>(number);UIntField(object,"preferred_image_base",result.resolvedModule.preferredImageBase);StringField(object,"pdb_guid",result.resolvedModule.pdbGuid);if(UIntField(object,"pdb_age",number)&&number<=UINT32_MAX)result.resolvedModule.pdbAge=static_cast<uint32_t>(number);}
    pos=document.find("\"prototype_used\":"); if(pos!=std::string::npos && document.find("null",pos) < document.find('{',pos)) { result.prototypeUsed = {}; } else if(pos!=std::string::npos && NextObject(document,pos,object)) { if(!PrototypeObject(object,result.prototypeUsed)) Diagnostic(diagnostics,"invalid-prototype","prototype_used","prototype is malformed"); }
    pos=document.find("\"output_values\":["); if(pos!=std::string::npos){pos+=17; while(true){while(pos<document.size()&&std::isspace(static_cast<unsigned char>(document[pos])))++pos; if(pos<document.size()&&document[pos]==']'){++pos;break;} if(!NextObject(document,pos,object))break; CallArgument value; size_t typePos=object.find("\"type\":"); std::string typeObject; if(typePos==std::string::npos||!NextObject(object,typePos,typeObject)||!TypeObject(typeObject,value.type)||!StringField(object,"value",value.value)){Diagnostic(diagnostics,"invalid-output","output_values","output value is invalid");break;} UIntField(object,"buffer_size",value.bufferSize); StringField(object,"ownership",value.ownership); result.outputValues.push_back(std::move(value)); if(document.find(']',pos)!=std::string::npos&&document.find(']',pos)<document.find('{',pos))break; }}
    pos=document.find("\"diagnostics\":["); if(pos!=std::string::npos){pos+=15; while(true){if(!NextObject(document,pos,object))break; CallDiagnostic item; if(StringField(object,"code",item.code)&&StringField(object,"path",item.path)&&StringField(object,"message",item.message))result.diagnostics.push_back(std::move(item)); else {Diagnostic(diagnostics,"invalid-diagnostic","diagnostics","diagnostic is incomplete");break;} if(document.find(']',pos)!=std::string::npos&&document.find(']',pos)<document.find('{',pos))break; }} return diagnostics.empty();
}
