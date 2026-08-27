#include "stdafx.h"
#include "CallContract.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>

namespace
{
void Diagnostic(std::vector<CallDiagnostic>& out, const char* code, const std::string& path, const char* message)
{ out.push_back({code, path, message}); }
void Json(std::ostream& out, const std::string& value)
{
    out << '"';
    for (unsigned char c : value) { if (c == '"' || c == '\\') out << '\\' << c; else if (c == '\n') out << "\\n"; else if (c == '\r') out << "\\r"; else if (c == '\t') out << "\\t"; else if (c < 0x20) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(c) << std::dec; else out << c; }
    out << '"';
}
bool EqualType(const TypeSpec& left, const TypeSpec& right)
{ return left.kind == right.kind && left.width == right.width && left.isSigned == right.isSigned && left.pointerDepth == right.pointerDepth && left.encoding == right.encoding; }
bool Integer(const std::string& text, uint16_t width, bool signedValue)
{
    if (text.empty() || width == 0 || width > 64) return false; errno = 0; char* end = nullptr; const long long value = std::strtoll(text.c_str(), &end, 0); if (errno == ERANGE || end == text.c_str() || *end != '\0') return false;
    if (!signedValue && text.front() == '-') return false;
    if (width >= 64) return true;
    const long long maximum = signedValue ? ((1LL << (width - 1)) - 1) : ((1LL << width) - 1);
    const long long minimum = signedValue ? -(1LL << (width - 1)) : 0;
    return value >= minimum && value <= maximum;
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
    PrototypeSpec prototype;
    if (request.hasPrototypeOverride) prototype = request.prototypeOverride;
    else if (record != nullptr && record->hasPrototype) prototype = record->prototype;
    else Diagnostic(diagnostics, "prototype-required", "prototype", "an invocation-grade prototype is required");
    if (request.hasPrototypeOverride && prototype.quality != PrototypeQuality::UserDeclared && prototype.quality != PrototypeQuality::ExactSymbol) Diagnostic(diagnostics, "prototype-not-invocation-grade", "prototype_override", "only user-declared or exact-symbol prototypes may be used");
    if (!request.hasPrototypeOverride && record != nullptr && record->hasPrototype && record->prototype.quality != PrototypeQuality::UserDeclared && record->prototype.quality != PrototypeQuality::ExactSymbol) Diagnostic(diagnostics, "prototype-not-invocation-grade", "prototype", "catalog evidence is display-only");
    if (request.hasPrototypeOverride && prototype.abi.empty()) Diagnostic(diagnostics, "missing-abi", "prototype_override.abi", "ABI is required");
    if (record != nullptr && prototype.parameters.size() != request.arguments.size()) Diagnostic(diagnostics, "argument-count-mismatch", "arguments", "argument count does not match the selected prototype");
    if (record != nullptr && prototype.parameters.size() == request.arguments.size()) for (size_t index=0; index<request.arguments.size(); ++index)
    {
        const CallArgument& argument=request.arguments[index];
        if (!EqualType(argument.type, prototype.parameters[index])) Diagnostic(diagnostics, "argument-type-mismatch", "arguments["+std::to_string(index)+"].type", "argument type does not match the prototype");
        if (argument.type.direction != ParameterDirection::In && argument.value.empty()) Diagnostic(diagnostics, "missing-output-buffer", "arguments["+std::to_string(index)+"].value", "output arguments require a bounded value or buffer descriptor");
        if (!ArgumentValue(argument)) Diagnostic(diagnostics, "invalid-argument-value", "arguments["+std::to_string(index)+"].value", "value is invalid for its declared type");
    }
    return diagnostics.empty();
}

void WriteCallRequestJson(std::ostream& output, const CallRequest& request)
{
    output << "{\"schema_version\":" << request.schemaVersion << ",\"correlation_id\":"; Json(output, request.correlationId); output << ",\"selector\":"; Json(output, request.selector); output << ",\"timeout_ms\":" << request.timeoutMs << ",\"allow_internal\":" << (request.allowInternal ? "true" : "false") << ",\"arguments\":[";
    for (size_t i=0;i<request.arguments.size();++i) { if(i) output << ','; output << "{\"kind\":"; Json(output, TypeKindName(request.arguments[i].type.kind)); output << ",\"width\":" << request.arguments[i].type.width << ",\"value\":"; Json(output, request.arguments[i].value); output << '}'; }
    output << "]}";
}

void WriteCallResultJson(std::ostream& output, const CallResult& result)
{
    output << "{\"schema_version\":" << result.schemaVersion << ",\"correlation_id\":"; Json(output, result.correlationId); output << ",\"success\":" << (result.success ? "true" : "false") << ",\"status\":"; Json(output, result.status); output << ",\"return_value\":"; Json(output, result.returnValue); output << ",\"diagnostics\":[";
    for (size_t i=0;i<result.diagnostics.size();++i) { if(i) output << ','; output << "{\"code\":"; Json(output,result.diagnostics[i].code); output << ",\"path\":"; Json(output,result.diagnostics[i].path); output << ",\"message\":"; Json(output,result.diagnostics[i].message); output << '}'; }
    output << "]}";
}
