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
    out << "{\"kind\":"; Json(out, TypeKindName(type.kind)); out << ",\"width\":" << type.width << ",\"signed\":" << (type.isSigned ? "true" : "false") << ",\"pointer_depth\":" << static_cast<unsigned>(type.pointerDepth) << ",\"direction\":"; Json(out, type.direction == ParameterDirection::In ? "in" : type.direction == ParameterDirection::Out ? "out" : "inout"); out << ",\"element_count\":" << type.elementCount << ",\"encoding\":"; Json(out, type.encoding); out << ",\"ownership\":"; Json(out, type.ownership); out << ",\"layout\":"; Json(out, type.layout); out << '}';
}
void WritePrototype(std::ostream& out, const PrototypeSpec& prototype)
{
    out << "{\"abi\":"; Json(out, prototype.abi); out << ",\"return_type\":"; WriteType(out, prototype.returnType); out << ",\"parameters\":["; for (size_t i=0;i<prototype.parameters.size();++i) { if(i) out << ','; WriteType(out, prototype.parameters[i]); } out << "],\"variadic\":" << (prototype.variadic ? "true" : "false") << ",\"quality\":"; Json(out, PrototypeQualityName(prototype.quality)); out << '}';
}
bool EqualType(const TypeSpec& left, const TypeSpec& right)
{ return left.kind == right.kind && left.width == right.width && left.isSigned == right.isSigned && left.pointerDepth == right.pointerDepth && left.direction == right.direction && left.elementCount == right.elementCount && left.encoding == right.encoding && left.ownership == right.ownership && left.layout == right.layout; }
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
    case TypeKind::String: return argument.type.pointerDepth > 0 && !argument.value.empty();
    case TypeKind::Pointer: return argument.type.pointerDepth > 0 && argument.value.rfind("opaque:", 0) == 0;
    default: return false;
    }
}
bool ValidType(const TypeSpec& type)
{
    if (type.pointerDepth > 8) return false;
    switch (type.kind) { case TypeKind::Bool: return type.width == 1; case TypeKind::Integer: return type.width == 8 || type.width == 16 || type.width == 32 || type.width == 64; case TypeKind::Floating: return type.width == 32 || type.width == 64; case TypeKind::String: case TypeKind::Pointer: return type.pointerDepth != 0; case TypeKind::Structure: return type.width != 0; case TypeKind::Void: return type.width == 0 && type.pointerDepth == 0; default: return false; }
}
bool ValidPrototype(const PrototypeSpec& prototype)
{ return !prototype.abi.empty() && (prototype.quality == PrototypeQuality::UserDeclared || prototype.quality == PrototypeQuality::ExactSymbol) && ValidType(prototype.returnType) && std::all_of(prototype.parameters.begin(), prototype.parameters.end(), ValidType); }
bool ValidAbiForModule(const std::string& abi, const std::string& architecture)
{ const bool x64 = abi == "x64" || abi == "win64"; const bool x86 = abi == "__cdecl" || abi == "__stdcall" || abi == "__thiscall" || abi == "__fastcall"; return (architecture == "x64" && x64) || (architecture == "x86" && x86); }
bool StringField(const std::string& object, const char* key, std::string& value)
{ const std::string token = std::string("\"") + key + "\":\""; const size_t begin = object.find(token); if (begin == std::string::npos) return false; const size_t start = begin + token.size(); const size_t end = object.find('"', start); if (end == std::string::npos) return false; value = object.substr(start, end - start); return true; }
bool UIntField(const std::string& object, const char* key, uint64_t& value)
{ const std::string token = std::string("\"") + key + "\":"; const size_t begin = object.find(token); if (begin == std::string::npos) return false; size_t start = begin + token.size(), end = start; while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end]))) ++end; if (end == start) return false; value = 0; for (; start < end; ++start) { const uint64_t digit = static_cast<unsigned>(object[start]-'0'); if (value > (UINT64_MAX-digit)/10) return false; value=value*10+digit; } return true; }
bool BoolField(const std::string& object, const char* key, bool& value)
{ const std::string token = std::string("\"") + key + "\":"; const size_t begin=object.find(token); if(begin==std::string::npos) return false; const size_t start=begin+token.size(); if(object.compare(start,4,"true")==0){value=true;return true;} if(object.compare(start,5,"false")==0){value=false;return true;} return false; }
bool TypeObject(const std::string& object, TypeSpec& type)
{ std::string kind; uint64_t n=0; if(!StringField(object,"kind",kind) || !UIntField(object,"width",n) || n>UINT16_MAX) return false; const std::map<std::string,TypeKind> kinds={{"void",TypeKind::Void},{"bool",TypeKind::Bool},{"integer",TypeKind::Integer},{"floating",TypeKind::Floating},{"string",TypeKind::String},{"pointer",TypeKind::Pointer},{"structure",TypeKind::Structure}}; auto found=kinds.find(kind); if(found==kinds.end()) return false; type.kind=found->second; type.width=static_cast<uint16_t>(n); if(UIntField(object,"pointer_depth",n) && n<=UINT8_MAX) type.pointerDepth=static_cast<uint8_t>(n); BoolField(object,"signed",type.isSigned); StringField(object,"encoding",type.encoding); StringField(object,"ownership",type.ownership); StringField(object,"layout",type.layout); UIntField(object,"element_count",type.elementCount); std::string direction; if(StringField(object,"direction",direction)){if(direction=="out")type.direction=ParameterDirection::Out;else if(direction=="inout")type.direction=ParameterDirection::InOut;} return ValidType(type); }
bool NextObject(const std::string& text, size_t& position, std::string& object)
{ const size_t start=text.find('{',position); if(start==std::string::npos)return false; size_t depth=0; bool quoted=false,escaped=false; for(size_t i=start;i<text.size();++i){const char c=text[i];if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}if(c=='"'){quoted=true;continue;}if(c=='{')++depth;else if(c=='}'&&--depth==0){object=text.substr(start,i-start+1);position=i+1;return true;}}return false; }
bool PrototypeObject(const std::string& object, PrototypeSpec& prototype)
{ std::string quality; if(!StringField(object,"abi",prototype.abi)||!StringField(object,"quality",quality))return false; prototype.quality=quality=="exact-symbol"?PrototypeQuality::ExactSymbol:quality=="user-declared"?PrototypeQuality::UserDeclared:quality=="inferred"?PrototypeQuality::Inferred:PrototypeQuality::Unknown; size_t pos=object.find("\"return_type\":"); std::string type; if(pos==std::string::npos||!NextObject(object,pos,type)||!TypeObject(type,prototype.returnType))return false; pos=object.find("\"parameters\":["); if(pos!=std::string::npos){pos+=14; while(true){std::string item; if(!NextObject(object,pos,item))break; TypeSpec parsed; if(!TypeObject(item,parsed))return false; prototype.parameters.push_back(parsed); if(object.find(']',pos)!=std::string::npos&&object.find(']',pos)<object.find('{',pos))break;}} BoolField(object,"variadic",prototype.variadic); return ValidPrototype(prototype); }
}

bool ValidateCallRequest(const CallRequest& request, const FunctionCatalog& catalog,
    std::vector<CallDiagnostic>& diagnostics)
{
    diagnostics.clear();
    if (request.schemaVersion != CallRequest::kSchemaVersion) Diagnostic(diagnostics, "unsupported-schema", "schema_version", "supported version is 1");
    if (request.correlationId.empty() || request.correlationId.size() > 128) Diagnostic(diagnostics, "invalid-correlation-id", "correlation_id", "correlation ID must be 1..128 characters");
    if (request.selector.empty()) Diagnostic(diagnostics, "missing-selector", "selector", "function selector is required");
    const FunctionRecord* record = request.selector.empty() ? nullptr : catalog.Find(request.selector);
    if (record == nullptr) Diagnostic(diagnostics, "selector-not-found-or-ambiguous", "selector", "selector must identify exactly one catalog record");
    if (record != nullptr && !request.allowInternal && record->exportNames.empty()) Diagnostic(diagnostics, "internal-policy-required", "selector", "internal targets require explicit policy");
    if (record != nullptr && record->callability == Callability::Forwarded) Diagnostic(diagnostics, "forwarded-target", "selector", "forwarded exports are not directly addressable");
    if (record != nullptr && record->callability == Callability::ArchitectureMismatch) Diagnostic(diagnostics, "architecture-mismatch", "selector", "catalog architecture does not support this target");
    PrototypeSpec prototype;
    if (request.hasPrototypeOverride) prototype = request.prototypeOverride;
    else if (record != nullptr && record->hasPrototype) prototype = record->prototype;
    else Diagnostic(diagnostics, "prototype-required", "prototype", "an invocation-grade prototype is required");
    if (request.hasPrototypeOverride && prototype.quality != PrototypeQuality::UserDeclared && prototype.quality != PrototypeQuality::ExactSymbol) Diagnostic(diagnostics, "prototype-not-invocation-grade", "prototype_override", "only user-declared or exact-symbol prototypes may be used");
    if (!request.hasPrototypeOverride && record != nullptr && record->hasPrototype && record->prototype.quality != PrototypeQuality::UserDeclared && record->prototype.quality != PrototypeQuality::ExactSymbol) Diagnostic(diagnostics, "prototype-not-invocation-grade", "prototype", "catalog evidence is display-only");
    if (request.hasPrototypeOverride && !ValidPrototype(prototype)) Diagnostic(diagnostics, "invalid-prototype-override", "prototype_override", "override must be a complete invocation-grade prototype");
    if ((request.hasPrototypeOverride || (record != nullptr && record->hasPrototype)) && !ValidAbiForModule(prototype.abi, catalog.Module().architecture)) Diagnostic(diagnostics, "unsupported-abi", "prototype.abi", "ABI does not match module architecture");
    if (!request.hasPrototypeOverride && record != nullptr && record->callability != Callability::Callable) Diagnostic(diagnostics, "target-not-callable", "selector", "catalog callability does not authorize invocation");
    if (record != nullptr && prototype.parameters.size() != request.arguments.size()) Diagnostic(diagnostics, "argument-count-mismatch", "arguments", "argument count does not match the selected prototype");
    if (record != nullptr && prototype.parameters.size() == request.arguments.size()) for (size_t index=0; index<request.arguments.size(); ++index)
    {
        const CallArgument& argument=request.arguments[index];
        if (!EqualType(argument.type, prototype.parameters[index])) Diagnostic(diagnostics, "argument-type-mismatch", "arguments["+std::to_string(index)+"].type", "argument type does not match the prototype");
        if (argument.type.direction != ParameterDirection::In && argument.value.empty()) Diagnostic(diagnostics, "missing-output-buffer", "arguments["+std::to_string(index)+"].value", "output arguments require a bounded value or buffer descriptor");
        if (!ArgumentValue(argument)) Diagnostic(diagnostics, "invalid-argument-value", "arguments["+std::to_string(index)+"].value", "value is invalid for its declared type");
        if (argument.bufferSize > kMaximumBufferBytes) Diagnostic(diagnostics, "buffer-too-large", "arguments["+std::to_string(index)+"].buffer_size", "buffer exceeds the hard cap");
        if (argument.type.elementCount != 0 && argument.type.width != 0 && argument.type.elementCount > kMaximumBufferBytes / argument.type.width) Diagnostic(diagnostics, "buffer-size-overflow", "arguments["+std::to_string(index)+"].type.element_count", "buffer size overflows the hard cap");
    }
    return diagnostics.empty();
}

void WriteCallRequestJson(std::ostream& output, const CallRequest& request)
{
    output << "{\"schema_version\":" << request.schemaVersion << ",\"correlation_id\":"; Json(output, request.correlationId); output << ",\"selector\":"; Json(output, request.selector); output << ",\"timeout_ms\":" << request.timeoutMs << ",\"allow_internal\":" << (request.allowInternal ? "true" : "false") << ",\"has_prototype_override\":" << (request.hasPrototypeOverride ? "true" : "false") << ",\"prototype_override\":"; if (request.hasPrototypeOverride) WritePrototype(output, request.prototypeOverride); else output << "null"; output << ",\"arguments\":[";
    for (size_t i=0;i<request.arguments.size();++i) { if(i) output << ','; output << "{\"type\":"; WriteType(output, request.arguments[i].type); output << ",\"value\":"; Json(output, request.arguments[i].value); output << ",\"buffer_size\":" << request.arguments[i].bufferSize << ",\"ownership\":"; Json(output, request.arguments[i].ownership); output << '}'; }
    output << "]}";
}

void WriteCallResultJson(std::ostream& output, const CallResult& result)
{
    output << "{\"schema_version\":" << result.schemaVersion << ",\"correlation_id\":"; Json(output, result.correlationId); output << ",\"success\":" << (result.success ? "true" : "false") << ",\"status\":"; Json(output, result.status); output << ",\"return_value\":"; Json(output, result.returnValue); output << ",\"diagnostics\":[";
    for (size_t i=0;i<result.diagnostics.size();++i) { if(i) output << ','; output << "{\"code\":"; Json(output,result.diagnostics[i].code); output << ",\"path\":"; Json(output,result.diagnostics[i].path); output << ",\"message\":"; Json(output,result.diagnostics[i].message); output << '}'; }
    output << "]}";
}

bool ParseCallRequestJson(const std::string& document, CallRequest& request, std::vector<CallDiagnostic>& diagnostics)
{
    request = {}; diagnostics.clear(); if(document.size()>4*1024*1024){Diagnostic(diagnostics,"size-limit","$","request is too large");return false;} uint64_t number=0; bool flag=false; std::string text;
    if(!UIntField(document,"schema_version",number)||number>UINT32_MAX) Diagnostic(diagnostics,"invalid-schema","schema_version","schema_version is invalid"); else request.schemaVersion=static_cast<uint32_t>(number);
    if(!StringField(document,"correlation_id",request.correlationId)) Diagnostic(diagnostics,"missing-field","correlation_id","correlation_id is required");
    if(!StringField(document,"selector",request.selector)) Diagnostic(diagnostics,"missing-field","selector","selector is required");
    if(UIntField(document,"timeout_ms",number)&&number<=UINT32_MAX) request.timeoutMs=static_cast<uint32_t>(number);
    if(BoolField(document,"allow_internal",flag)) request.allowInternal=flag;
    if(BoolField(document,"has_prototype_override",flag)&&flag) { request.hasPrototypeOverride=true; size_t pos=document.find("\"prototype_override\":"); std::string object; if(pos==std::string::npos||!NextObject(document,pos,object)||!PrototypeObject(object,request.prototypeOverride)) Diagnostic(diagnostics,"invalid-prototype-override","prototype_override","prototype override is invalid"); }
    size_t position=document.find("\"arguments\":["); if(position!=std::string::npos){position+=13; while(true){std::string object; if(!NextObject(document,position,object))break; const size_t typeStart=object.find("\"type\":"); if(typeStart==std::string::npos)break; size_t typePos=typeStart; std::string typeObject; if(!NextObject(object,typePos,typeObject))break; CallArgument argument; if(!TypeObject(typeObject,argument.type)||!StringField(object,"value",argument.value)) {Diagnostic(diagnostics,"invalid-argument","arguments","invalid typed argument");break;} UIntField(object,"buffer_size",argument.bufferSize); StringField(object,"ownership",argument.ownership); request.arguments.push_back(std::move(argument)); if(document.find(']',position)!=std::string::npos && document.find(']',position)<document.find('{',position))break; }}
    return diagnostics.empty();
}

bool ParseCallResultJson(const std::string& document, CallResult& result, std::vector<CallDiagnostic>& diagnostics)
{
    result={}; diagnostics.clear(); uint64_t number=0; bool flag=false; if(!UIntField(document,"schema_version",number)||number>UINT32_MAX) Diagnostic(diagnostics,"invalid-schema","schema_version","schema_version is invalid"); else result.schemaVersion=static_cast<uint32_t>(number); if(!StringField(document,"correlation_id",result.correlationId))Diagnostic(diagnostics,"missing-field","correlation_id","correlation_id is required"); if(BoolField(document,"success",flag))result.success=flag; if(!StringField(document,"status",result.status))Diagnostic(diagnostics,"missing-field","status","status is required"); StringField(document,"return_value",result.returnValue); return diagnostics.empty();
}
