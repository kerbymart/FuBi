#pragma once

#include "StaticExportCatalog.h"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

struct PrototypeProfile;
struct ProfileValidationError;

enum class TypeKind
{
    Unknown,
    Void,
    Bool,
    Integer,
    Floating,
    String,
    Pointer,
    Structure
};

enum class ParameterDirection
{
    In,
    Out,
    InOut
};

struct TypeSpec
{
    TypeKind kind = TypeKind::Unknown;
    uint16_t width = 0;
    bool isSigned = false;
    uint8_t pointerDepth = 0;
    ParameterDirection direction = ParameterDirection::In;
    uint64_t elementCount = 0;
    std::string encoding;
    std::string ownership;
    std::string layout;
};

enum class PrototypeQuality
{
    Unknown,
    Inferred,
    UserDeclared,
    ExactSymbol
};

struct PrototypeSpec
{
    std::string abi;
    TypeSpec returnType;
    std::vector<TypeSpec> parameters;
    bool variadic = false;
    std::string source;
    PrototypeQuality quality = PrototypeQuality::Unknown;
};

struct ModuleIdentity
{
    std::string canonicalPath;
    std::string sha256;
    std::string architecture;
    uint32_t timestamp = 0;
    uint32_t imageSize = 0;
    uint64_t preferredImageBase = 0;
};

struct FunctionId
{
    std::string moduleSha256;
    uint32_t rva = 0;
    std::string exportName;
    uint32_t ordinal = 0;
    std::string symbol;
};

enum class Callability
{
    Callable,
    RequiresPrototype,
    UnsupportedAbi,
    UnsafeInternal,
    FrameworkManaged,
    Forwarded,
    NotAddressable,
    ArchitectureMismatch
};

struct FunctionRecord
{
    FunctionId id;
    std::string displayName;
    std::vector<std::string> aliases;
    uint32_t startRva = 0;
    uint32_t endRva = 0;
    std::vector<std::string> addressSources;
    std::vector<std::string> boundarySources;
    PrototypeSpec prototype;
    bool hasPrototype = false;
    std::vector<std::string> prototypeConflicts;
    Callability callability = Callability::NotAddressable;
    std::vector<std::string> callabilityReasons;
    bool executable = false;
    std::string forwarder;
    std::vector<std::string> forwarders;
    std::vector<std::string> exportNames;
    std::vector<uint32_t> exportOrdinals;
};

const char* CallabilityName(Callability value);
const char* CallabilityReason(Callability value);
const char* TypeKindName(TypeKind value);
const char* PrototypeQualityName(PrototypeQuality value);

class FunctionCatalog
{
public:
    static constexpr uint32_t kSchemaVersion = 1;

    static bool Load(const std::string& path, FunctionCatalog& catalog,
        std::string& error);

    const ModuleIdentity& Module() const { return module_; }
    const std::vector<FunctionRecord>& Functions() const { return functions_; }

    void WriteText(std::ostream& output, bool callableOnly = false) const;
    void WriteJson(std::ostream& output, bool callableOnly = false) const;
    void WriteJsonDescribe(std::ostream& output, const FunctionRecord& record) const;

    std::vector<const FunctionRecord*> FindAll(const std::string& selector) const;
    const FunctionRecord* Find(const std::string& selector) const;
    bool ApplyProfile(const PrototypeProfile& profile,
        std::vector<ProfileValidationError>& errors);

private:
    ModuleIdentity module_;
    std::vector<FunctionRecord> functions_;
};
