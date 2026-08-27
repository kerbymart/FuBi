#include "stdafx.h"
#include "PrototypeProfile.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace
{
constexpr size_t kMaxDocumentBytes = 4 * 1024 * 1024;
constexpr size_t kMaxObjectMembers = 64;
constexpr size_t kMaxArrayItems = 4096;
constexpr size_t kMaxStringBytes = 64 * 1024;

struct JsonValue
{
    enum class Kind { Null, Boolean, Number, String, Object, Array } kind = Kind::Null;
    bool boolean = false;
    uint64_t number = 0;
    std::string string;
    std::map<std::string, JsonValue> object;
    std::vector<JsonValue> array;
};

class JsonReader
{
public:
    explicit JsonReader(const std::string& input) : input_(input) {}
    bool Read(JsonValue& value, std::string& error)
    {
        Skip();
        if (!Value(value, error)) return false;
        Skip();
        if (position_ != input_.size()) { error = "trailing data"; return false; }
        return true;
    }
private:
    void Skip() { while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_; }
    bool Value(JsonValue& value, std::string& error, size_t depth = 0)
    {
        if (depth > 32) { error = "maximum JSON nesting depth exceeded"; return false; }
        Skip();
        if (position_ >= input_.size()) { error = "expected value"; return false; }
        const char c = input_[position_];
        if (c == '{') return Object(value, error, depth);
        if (c == '[') return Array(value, error, depth);
        if (c == '"') { value.kind = JsonValue::Kind::String; return String(value.string, error); }
        if (input_.compare(position_, 4, "true") == 0) { position_ += 4; value.kind = JsonValue::Kind::Boolean; value.boolean = true; return true; }
        if (input_.compare(position_, 5, "false") == 0) { position_ += 5; value.kind = JsonValue::Kind::Boolean; value.boolean = false; return true; }
        if (input_.compare(position_, 4, "null") == 0) { position_ += 4; value.kind = JsonValue::Kind::Null; return true; }
        return Number(value, error);
    }
    bool String(std::string& result, std::string& error)
    {
        if (position_ >= input_.size() || input_[position_++] != '"') { error = "expected string"; return false; }
        result.clear();
        while (position_ < input_.size())
        {
            const char c = input_[position_++];
            if (c == '"') return true;
            if (c == '\\')
            {
                if (position_ >= input_.size()) { error = "unterminated escape"; return false; }
                const char escaped = input_[position_++];
                switch (escaped) { case '"': case '\\': case '/': result += escaped; break; case 'b': result += '\b'; break; case 'f': result += '\f'; break; case 'n': result += '\n'; break; case 'r': result += '\r'; break; case 't': result += '\t'; break; default: error = "unsupported string escape"; return false; }
            }
            else if (static_cast<unsigned char>(c) < 0x20) { error = "control character in string"; return false; }
            else result += c;
            if (result.size() > kMaxStringBytes) { error = "string is too large"; return false; }
        }
        error = "unterminated string"; return false;
    }
    bool Number(JsonValue& value, std::string& error)
    {
        const size_t begin = position_;
        if (position_ < input_.size() && input_[position_] == '-') { error = "negative numbers are not supported"; return false; }
        while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        if (begin == position_) { error = "expected unsigned integer"; return false; }
        uint64_t result = 0;
        for (size_t i = begin; i < position_; ++i) { const uint64_t digit = static_cast<unsigned>(input_[i] - '0'); if (result > (UINT64_MAX - digit) / 10) { error = "number overflow"; return false; } result = result * 10 + digit; }
        value.kind = JsonValue::Kind::Number; value.number = result; return true;
    }
    bool Object(JsonValue& value, std::string& error, size_t depth)
    {
        ++position_; value.kind = JsonValue::Kind::Object; Skip();
        if (position_ < input_.size() && input_[position_] == '}') { ++position_; return true; }
        while (position_ < input_.size())
        {
            if (value.object.size() >= kMaxObjectMembers) { error = "object has too many fields"; return false; }
            std::string key; if (!String(key, error)) return false; Skip();
            if (position_ >= input_.size() || input_[position_++] != ':') { error = "expected ':'"; return false; }
            JsonValue item; if (!Value(item, error, depth + 1)) return false;
            if (!value.object.emplace(std::move(key), std::move(item)).second) { error = "duplicate object field"; return false; }
            Skip(); if (position_ >= input_.size()) break;
            if (input_[position_] == '}') { ++position_; return true; }
            if (input_[position_++] != ',') { error = "expected ','"; return false; }
            Skip();
        }
        error = "unterminated object"; return false;
    }
    bool Array(JsonValue& value, std::string& error, size_t depth)
    {
        ++position_; value.kind = JsonValue::Kind::Array; Skip();
        if (position_ < input_.size() && input_[position_] == ']') { ++position_; return true; }
        while (position_ < input_.size())
        {
            if (value.array.size() >= kMaxArrayItems) { error = "array has too many items"; return false; }
            JsonValue item; if (!Value(item, error, depth + 1)) return false; value.array.push_back(std::move(item)); Skip();
            if (position_ >= input_.size()) break;
            if (input_[position_] == ']') { ++position_; return true; }
            if (input_[position_++] != ',') { error = "expected ','"; return false; }
            Skip();
        }
        error = "unterminated array"; return false;
    }
    const std::string& input_; size_t position_ = 0;
};

void Error(std::vector<ProfileValidationError>& errors, const std::string& code, const std::string& path, const std::string& message)
{ errors.push_back({code, path, message}); }
const JsonValue* Field(const JsonValue& object, const char* name)
{ auto found = object.object.find(name); return found == object.object.end() ? nullptr : &found->second; }
bool ObjectValue(const JsonValue* value, const std::string& path, std::vector<ProfileValidationError>& errors)
{ if (value == nullptr || value->kind != JsonValue::Kind::Object) { Error(errors, "invalid-type", path, "expected object"); return false; } return true; }
bool StringValue(const JsonValue* value, std::string& result, const std::string& path, std::vector<ProfileValidationError>& errors)
{ if (value == nullptr || value->kind != JsonValue::Kind::String) { Error(errors, "invalid-type", path, "expected string"); return false; } result = value->string; return true; }
bool NumberValue(const JsonValue* value, uint32_t& result, const std::string& path, std::vector<ProfileValidationError>& errors)
{ if (value == nullptr || value->kind != JsonValue::Kind::Number || value->number > UINT32_MAX) { Error(errors, "invalid-type", path, "expected uint32"); return false; } result = static_cast<uint32_t>(value->number); return true; }
bool BooleanValue(const JsonValue* value, bool& result, const std::string& path, std::vector<ProfileValidationError>& errors)
{ if (value == nullptr || value->kind != JsonValue::Kind::Boolean) { Error(errors, "invalid-type", path, "expected boolean"); return false; } result = value->boolean; return true; }
bool HasOnly(const JsonValue& object, const std::set<std::string>& fields, const std::string& path, std::vector<ProfileValidationError>& errors)
{ bool okay = true; for (const auto& item : object.object) if (!fields.count(item.first)) { Error(errors, "unknown-field", path + "." + item.first, "field is not supported"); okay = false; } return okay; }
bool HexHash(const std::string& hash) { if (hash.size() != 64) return false; return std::all_of(hash.begin(), hash.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }); }
bool PdbGuid(const std::string& guid) { return guid.size() == 36 && std::all_of(guid.begin(), guid.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) || c == '-'; }) && guid[8] == '-' && guid[13] == '-' && guid[18] == '-' && guid[23] == '-'; }
bool SupportedAbi(const std::string& abi) { return abi == "x64" || abi == "win64" || abi == "__cdecl" || abi == "__stdcall" || abi == "__thiscall" || abi == "__fastcall"; }
bool ValidType(const TypeSpec& type)
{
    if (type.pointerDepth > 8) return false;
    switch (type.kind)
    {
    case TypeKind::Void: return type.width == 0 && type.pointerDepth == 0;
    case TypeKind::Bool: return type.width == 1;
    case TypeKind::Integer: return std::set<uint16_t>{8, 16, 32, 64}.count(type.width) != 0;
    case TypeKind::Floating: return std::set<uint16_t>{32, 64}.count(type.width) != 0;
    case TypeKind::String: return type.pointerDepth == 1;
    case TypeKind::Bytes: return type.pointerDepth == 1 && type.width == 8;
    case TypeKind::Pointer: return type.pointerDepth >= 1;
    case TypeKind::Structure: return type.width != 0;
    default: return false;
    }
}
bool EqualType(const TypeSpec& a, const TypeSpec& b)
{ return a.kind == b.kind && a.width == b.width && a.isSigned == b.isSigned && a.pointerDepth == b.pointerDepth && a.direction == b.direction && a.elementCount == b.elementCount && a.encoding == b.encoding && a.ownership == b.ownership && a.layout == b.layout; }
bool EqualPrototype(const PrototypeSpec& a, const PrototypeSpec& b)
{ if (a.abi != b.abi || a.variadic != b.variadic || !EqualType(a.returnType, b.returnType) || a.parameters.size() != b.parameters.size()) return false; for (size_t i = 0; i < a.parameters.size(); ++i) if (!EqualType(a.parameters[i], b.parameters[i])) return false; return true; }
bool CompletePrototype(const PrototypeSpec& prototype)
{
    if (!SupportedAbi(prototype.abi) || prototype.returnType.kind == TypeKind::Unknown) return false;
    return ValidType(prototype.returnType) && std::all_of(prototype.parameters.begin(), prototype.parameters.end(), [](const TypeSpec& type) { return ValidType(type); });
}

bool ParseType(const JsonValue& value, TypeSpec& type, const std::string& path, std::vector<ProfileValidationError>& errors)
{
    if (!ObjectValue(&value, path, errors)) return false;
    HasOnly(value, {"kind", "width", "signed", "pointer_depth", "direction", "element_count", "encoding", "ownership", "layout"}, path, errors);
    std::string kind; if (!StringValue(Field(value, "kind"), kind, path + ".kind", errors)) return false;
    const std::map<std::string, TypeKind> kinds = {{"void",TypeKind::Void},{"bool",TypeKind::Bool},{"integer",TypeKind::Integer},{"floating",TypeKind::Floating},{"string",TypeKind::String},{"bytes",TypeKind::Bytes},{"pointer",TypeKind::Pointer},{"structure",TypeKind::Structure}};
    auto found = kinds.find(kind); if (found == kinds.end()) { Error(errors, "unsupported-type", path + ".kind", "type kind is not supported"); return false; } type.kind = found->second;
    uint32_t number = 0; if (Field(value, "width") != nullptr && NumberValue(Field(value, "width"), number, path + ".width", errors)) { if (number > UINT16_MAX) Error(errors, "range", path + ".width", "width exceeds uint16"); else type.width = static_cast<uint16_t>(number); }
    if (Field(value, "signed") != nullptr) BooleanValue(Field(value, "signed"), type.isSigned, path + ".signed", errors);
    if (Field(value, "pointer_depth") != nullptr && NumberValue(Field(value, "pointer_depth"), number, path + ".pointer_depth", errors)) { if (number > UINT8_MAX) Error(errors, "range", path + ".pointer_depth", "pointer depth is too large"); else type.pointerDepth = static_cast<uint8_t>(number); }
    std::string text; if (Field(value, "direction") != nullptr && StringValue(Field(value, "direction"), text, path + ".direction", errors)) { if (text == "in") type.direction = ParameterDirection::In; else if (text == "out") type.direction = ParameterDirection::Out; else if (text == "inout") type.direction = ParameterDirection::InOut; else Error(errors, "unsupported-value", path + ".direction", "direction is not supported"); }
    if (Field(value, "element_count") != nullptr) { const JsonValue* count = Field(value, "element_count"); if (count->kind != JsonValue::Kind::Number || count->number > UINT64_MAX) Error(errors, "invalid-type", path + ".element_count", "expected uint64"); else type.elementCount = count->number; }
    if (Field(value, "encoding") != nullptr) StringValue(Field(value, "encoding"), type.encoding, path + ".encoding", errors);
    if (Field(value, "ownership") != nullptr) StringValue(Field(value, "ownership"), type.ownership, path + ".ownership", errors);
    if (Field(value, "layout") != nullptr) StringValue(Field(value, "layout"), type.layout, path + ".layout", errors);
    if (!ValidType(type)) Error(errors, "invalid-type-shape", path, "type width or pointer depth is invalid");
    return true;
}
}

bool ParsePrototypeProfile(const std::string& document, PrototypeProfile& profile, std::vector<ProfileValidationError>& errors)
{
    profile = {}; errors.clear();
    if (document.size() > kMaxDocumentBytes) { Error(errors, "size-limit", "$", "profile is too large"); return false; }
    JsonValue root; std::string parseError; if (!JsonReader(document).Read(root, parseError)) { Error(errors, "invalid-json", "$", parseError); return false; }
    if (!ObjectValue(&root, "$", errors)) return false;
    HasOnly(root, {"schema_version", "module", "functions"}, "$", errors);
    if (!NumberValue(Field(root, "schema_version"), profile.schemaVersion, "$.schema_version", errors) || profile.schemaVersion != 1) Error(errors, "unsupported-schema", "$.schema_version", "supported version is 1");
    const JsonValue* module = Field(root, "module"); if (ObjectValue(module, "$.module", errors)) { HasOnly(*module, {"path", "sha256", "architecture", "timestamp", "image_size", "preferred_image_base", "pdb_guid", "pdb_age"}, "$.module", errors); StringValue(Field(*module, "sha256"), profile.module.sha256, "$.module.sha256", errors); StringValue(Field(*module, "architecture"), profile.module.architecture, "$.module.architecture", errors); if (Field(*module,"path")) StringValue(Field(*module,"path"), profile.module.canonicalPath, "$.module.path", errors); if (Field(*module,"timestamp")) NumberValue(Field(*module,"timestamp"), profile.module.timestamp, "$.module.timestamp", errors); if (Field(*module,"image_size")) NumberValue(Field(*module,"image_size"), profile.module.imageSize, "$.module.image_size", errors); if (Field(*module,"preferred_image_base")) { const JsonValue* base=Field(*module,"preferred_image_base"); if (base->kind != JsonValue::Kind::Number) Error(errors,"invalid-type","$.module.preferred_image_base","expected uint64"); else profile.module.preferredImageBase=base->number; } if (Field(*module,"pdb_guid")) StringValue(Field(*module,"pdb_guid"), profile.module.pdbGuid, "$.module.pdb_guid", errors); if (Field(*module,"pdb_age")) NumberValue(Field(*module,"pdb_age"), profile.module.pdbAge, "$.module.pdb_age", errors); }
    if (!HexHash(profile.module.sha256)) Error(errors, "invalid-hash", "$.module.sha256", "expected 64 hexadecimal characters");
    if (profile.module.architecture != "x86" && profile.module.architecture != "x64") Error(errors, "unsupported-architecture", "$.module.architecture", "architecture must be x86 or x64");
    if (!profile.module.pdbGuid.empty() && !PdbGuid(profile.module.pdbGuid)) Error(errors, "invalid-pdb-guid", "$.module.pdb_guid", "expected a GUID in canonical form");
    if (profile.module.pdbGuid.empty() && profile.module.pdbAge != 0) Error(errors, "invalid-pdb-age", "$.module.pdb_age", "PDB age requires a GUID");
    const JsonValue* functions = Field(root, "functions"); if (functions == nullptr || functions->kind != JsonValue::Kind::Array) { Error(errors, "invalid-type", "$.functions", "expected array"); return false; }
    for (size_t index=0; index<functions->array.size(); ++index) { const JsonValue& item=functions->array[index]; const std::string path="$.functions["+std::to_string(index)+"]"; if (!ObjectValue(&item,path,errors)) continue; HasOnly(item,{"rva","selector","abi","return_type","parameters","variadic","framework_managed"},path,errors); ProfileFunction function; NumberValue(Field(item,"rva"),function.rva,path+".rva",errors); if(Field(item,"selector")) StringValue(Field(item,"selector"),function.selector,path+".selector",errors); StringValue(Field(item,"abi"),function.prototype.abi,path+".abi",errors); if(!SupportedAbi(function.prototype.abi)) Error(errors,"unsupported-abi",path+".abi","ABI is not supported"); const JsonValue* ret=Field(item,"return_type"); if(!ret) Error(errors,"incomplete-prototype",path+".return_type","return type is required"); else ParseType(*ret,function.prototype.returnType,path+".return_type",errors); const JsonValue* params=Field(item,"parameters"); if(params==nullptr||params->kind!=JsonValue::Kind::Array) Error(errors,"invalid-type",path+".parameters","expected array"); else for(size_t p=0;p<params->array.size();++p) { TypeSpec type; ParseType(params->array[p],type,path+".parameters["+std::to_string(p)+"]",errors); function.prototype.parameters.push_back(std::move(type)); } if(Field(item,"variadic")) BooleanValue(Field(item,"variadic"),function.prototype.variadic,path+".variadic",errors); if(Field(item,"framework_managed")) BooleanValue(Field(item,"framework_managed"),function.frameworkManaged,path+".framework_managed",errors); function.prototype.source="profile"; function.prototype.quality=PrototypeQuality::UserDeclared; profile.functions.push_back(std::move(function)); }
    return errors.empty();
}

bool ValidatePrototypeProfile(const PrototypeProfile& profile, const FunctionCatalog& catalog, std::vector<ProfileValidationError>& errors)
{
    errors.clear(); const ModuleIdentity& module=catalog.Module(); if(profile.schemaVersion!=1) Error(errors,"unsupported-schema","schema_version","supported version is 1"); if(profile.module.sha256!=module.sha256) Error(errors,"module-hash-mismatch","module.sha256","profile does not match catalog module"); if(profile.module.architecture!=module.architecture) Error(errors,"architecture-mismatch","module.architecture","profile does not match catalog module"); if(!profile.module.pdbGuid.empty() && module.pdbGuid.empty()) Error(errors,"pdb-identity-unavailable","module.pdb_guid","catalog has no CodeView identity"); if(!profile.module.pdbGuid.empty() && (profile.module.pdbGuid != module.pdbGuid || profile.module.pdbAge != module.pdbAge)) Error(errors,"pdb-identity-mismatch","module.pdb_guid","profile does not match CodeView identity"); std::set<uint32_t> rvas;
    for(size_t i=0;i<profile.functions.size();++i) { const ProfileFunction& item=profile.functions[i]; const std::string path="functions["+std::to_string(i)+"]"; if(!rvas.insert(item.rva).second) Error(errors,"duplicate-selector",path+".rva","RVA appears more than once"); if(!CompletePrototype(item.prototype)) Error(errors,"incomplete-prototype",path,"prototype is incomplete or uses an unsupported ABI"); const FunctionRecord* record=nullptr; for(const auto& candidate:catalog.Functions()) if(candidate.startRva==item.rva) {record=&candidate;break;} if(record==nullptr) {Error(errors,"unknown-rva",path+".rva","RVA is not in the catalog");continue;} if(!record->executable && record->forwarder.empty()) Error(errors,"non-executable-rva",path+".rva","RVA is not executable"); const bool x64=module.architecture=="x64"; if((x64 && item.prototype.abi!="x64" && item.prototype.abi!="win64") || (!x64 && (item.prototype.abi=="x64"||item.prototype.abi=="win64"))) Error(errors,"architecture-mismatch",path+".abi","ABI does not match module architecture"); if(!item.selector.empty()) { const auto matches=catalog.FindAll(item.selector); if(matches.size()!=1 || matches.front()->startRva!=item.rva) Error(errors,"ambiguous-selector",path+".selector","selector does not identify this RVA uniquely"); } }
    return errors.empty();
}

bool MergePrototypeEvidence(FunctionRecord& record, const PrototypeSpec& prototype, std::string source, PrototypeQuality quality)
{
    if (quality != PrototypeQuality::ExactSymbol && quality != PrototypeQuality::UserDeclared) quality = PrototypeQuality::Inferred;
    if ((quality == PrototypeQuality::ExactSymbol || quality == PrototypeQuality::UserDeclared) && !CompletePrototype(prototype)) return false;
    if (!record.hasPrototype) { record.prototype=prototype; record.prototype.source=std::move(source); record.prototype.quality=quality; record.hasPrototype=true; if (quality==PrototypeQuality::ExactSymbol || quality==PrototypeQuality::UserDeclared) { if(record.callability==Callability::RequiresPrototype) { record.callability=Callability::Callable; record.callabilityReasons={CallabilityReason(record.callability)}; } } return true; }
    if (EqualPrototype(record.prototype, prototype)) { if(record.prototype.source.find(source)==std::string::npos) record.prototype.source += "," + source; if(quality>record.prototype.quality) record.prototype.quality=quality; return true; }
    record.prototypeConflicts.push_back(std::move(source)); record.prototypeConflictEvidence.push_back(prototype); return false;
}
