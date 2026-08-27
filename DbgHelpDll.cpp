/**
 * @file DbgHelpDll.cpp
 * @brief Source file for the DbgHelpDll class.
 * @author Kerby
 * @date 2022-12-20
 */
#include "StdAfx.h"
#include "DbgHelpDll.h"

#include "PEImage.h"
#include "SysExports.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

typedef DWORD (WINAPI *PFNUNDECORATESYMBOLNAME)(PCSTR, PSTR, DWORD, DWORD);
static PFNUNDECORATESYMBOLNAME pfnUnDecorateSymbolName = NULL;

DbgHelpDll::DbgHelpDll(void)
{
}

DbgHelpDll::~DbgHelpDll(void)
{
	if (handle_ != nullptr) FreeLibrary(handle_);
}

bool DbgHelpDll::Load(void)
{
	handle_ = LoadLibrary(_T("DbgHelp"));
	if ( handle_ == NULL )
	{
		return false;
	}
	pfnUnDecorateSymbolName = (PFNUNDECORATESYMBOLNAME)GetProcAddress(handle_, "UnDecorateSymbolName");
	return pfnUnDecorateSymbolName != nullptr;
}

namespace
{
using SymInitializeFn = BOOL(WINAPI*)(HANDLE, PCSTR, BOOL);
using SymCleanupFn = BOOL(WINAPI*)(HANDLE);
using SymLoadModuleFn = DWORD64(WINAPI*)(HANDLE, HANDLE, PCSTR, PCSTR, DWORD64, DWORD, PMODLOAD_DATA, DWORD);
using SymUnloadModuleFn = BOOL(WINAPI*)(HANDLE, DWORD64);
using SymEnumSymbolsFn = BOOL(WINAPI*)(HANDLE, ULONG64, PCSTR, PSYM_ENUMERATESYMBOLS_CALLBACK, PVOID);
using SymSetOptionsFn = DWORD(WINAPI*)(DWORD);
using SymGetOptionsFn = DWORD(WINAPI*)();
using SymGetTypeInfoFn = BOOL(WINAPI*)(HANDLE, ULONG64, ULONG, IMAGEHLP_SYMBOL_TYPE_INFO, PVOID);

struct EnumerationState { std::vector<SymbolPrototypeEvidence>* symbols = nullptr; HANDLE process = nullptr; ULONG64 module = 0; SymGetTypeInfoFn getTypeInfo = nullptr; uint16_t pointerWidth = 64; };

constexpr ULONG kMaxTypeChildren = 128;
constexpr ULONG kMaxTypeDepth = 8;
constexpr ULONG kSymTagFunctionType = 13;
constexpr ULONG kSymTagPointerType = 14;
constexpr ULONG kSymTagBaseType = 16;
constexpr ULONG kSymTagTypedef = 17;
// CV_basic_type values are not exposed by every Windows SDK DbgHelp header.
constexpr ULONG kBaseVoid = 1;
constexpr ULONG kBaseChar = 2;
constexpr ULONG kBaseWChar = 3;
constexpr ULONG kBaseInt = 6;
constexpr ULONG kBaseUInt = 7;
constexpr ULONG kBaseFloat = 8;
constexpr ULONG kBaseBool = 10;
constexpr ULONG kBaseLong = 13;
constexpr ULONG kBaseULong = 14;
constexpr ULONG kBaseLongLong = 33;
constexpr ULONG kBaseULongLong = 34;

bool TypeInfo(const EnumerationState& state, ULONG id, IMAGEHLP_SYMBOL_TYPE_INFO request, PVOID value)
{ return id != 0 && state.getTypeInfo != nullptr && state.getTypeInfo(state.process, state.module, id, request, value) != FALSE; }

bool ReadPdbType(const EnumerationState& state, ULONG id, TypeSpec& type, ULONG depth)
{
    if (id == 0 || depth > kMaxTypeDepth) return false;
    ULONG tag = 0;
    if (!TypeInfo(state, id, TI_GET_SYMTAG, &tag)) return false;
    if (tag == kSymTagTypedef)
    {
        ULONG underlying = 0;
        return TypeInfo(state, id, TI_GET_TYPE, &underlying) && ReadPdbType(state, underlying, type, depth + 1);
    }
    if (tag == kSymTagPointerType)
    {
        ULONG pointee = 0;
        if (!TypeInfo(state, id, TI_GET_TYPE, &pointee)) return false;
        TypeSpec pointeeType;
        if (!ReadPdbType(state, pointee, pointeeType, depth + 1)) return false;
        type = {};
        type.kind = TypeKind::Pointer;
        type.width = state.pointerWidth;
        if (pointeeType.kind == TypeKind::Pointer)
        {
            if (pointeeType.pointerDepth >= kMaxTypeDepth) return false;
            type.pointerDepth = static_cast<uint8_t>(pointeeType.pointerDepth + 1);
        }
        else
        {
            type.pointerDepth = 1;
        }
        return type.pointerDepth <= kMaxTypeDepth;
    }
    if (tag != kSymTagBaseType) return false;
    ULONG baseType = 0;
    ULONG64 length = 0;
    if (!TypeInfo(state, id, TI_GET_BASETYPE, &baseType) || !TypeInfo(state, id, TI_GET_LENGTH, &length)) return false;
    if (length != 1 && length != 2 && length != 4 && length != 8) return false;
    type = {};
    type.width = static_cast<uint16_t>(length * 8);
    switch (baseType)
    {
    case kBaseVoid: type.kind = TypeKind::Void; type.width = 0; break;
    case kBaseBool: type.kind = TypeKind::Bool; type.width = 1; break;
    case kBaseChar: type.kind = TypeKind::Integer; type.width = 8; type.isSigned = true; break;
    case kBaseWChar: type.kind = TypeKind::Integer; type.width = 16; type.isSigned = false; break;
    case kBaseInt: case kBaseLong: case kBaseLongLong:
        type.kind = TypeKind::Integer; type.isSigned = true; break;
    case kBaseUInt: case kBaseULong: case kBaseULongLong:
        type.kind = TypeKind::Integer; type.isSigned = false; break;
    case kBaseFloat: type.kind = TypeKind::Floating; break;
    default: return false;
    }
    return (type.kind == TypeKind::Void) || type.width == 8 || type.width == 16 || type.width == 32 || type.width == 64;
}

bool ReadPdbAbi(const EnumerationState& state, ULONG functionType, std::string& abi)
{
    if (state.pointerWidth == 64)
    {
        abi = "x64";
        return true;
    }
    ULONG convention = 0;
    if (!TypeInfo(state, functionType, TI_GET_CALLING_CONVENTION, &convention)) return false;
    switch (convention)
    {
    case 0: // CV_CALL_NEAR_C
    case 1: // CV_CALL_FAR_C
        abi = "__cdecl";
        return true;
    case 4: // CV_CALL_NEAR_FAST
    case 5: // CV_CALL_FAR_FAST
        abi = "__fastcall";
        return true;
    case 7: // CV_CALL_NEAR_STD
    case 8: // CV_CALL_FAR_STD
        abi = "__stdcall";
        return true;
    case 9: // CV_CALL_THISCALL
        abi = "__thiscall";
        return true;
    default:
        return false;
    }
}

bool ReadPdbFunctionPrototype(const EnumerationState& state, const SYMBOL_INFO* info, PrototypeSpec& prototype)
{
    if (info == nullptr || info->TypeIndex == 0) return false;
    ULONG functionType = info->TypeIndex;
    ULONG tag = 0;
    if (!TypeInfo(state, functionType, TI_GET_SYMTAG, &tag) || tag != kSymTagFunctionType)
    {
        if (!TypeInfo(state, info->TypeIndex, TI_GET_TYPEID, &functionType) ||
            !TypeInfo(state, functionType, TI_GET_SYMTAG, &tag) ||
            tag != kSymTagFunctionType) return false;
    }
    if (!ReadPdbAbi(state, functionType, prototype.abi)) return false;
    ULONG returnType = 0;
    if (!TypeInfo(state, functionType, TI_GET_TYPE, &returnType) || !ReadPdbType(state, returnType, prototype.returnType, 0)) return false;
    ULONG count = 0;
    if (!TypeInfo(state, functionType, TI_GET_CHILDRENCOUNT, &count) || count > kMaxTypeChildren) return false;
    if (count == 0) return true;
    const size_t bytes = sizeof(TI_FINDCHILDREN_PARAMS) + (static_cast<size_t>(count) - 1) * sizeof(ULONG);
    std::vector<unsigned char> storage(bytes);
    auto* children = reinterpret_cast<TI_FINDCHILDREN_PARAMS*>(storage.data());
    children->Count = count;
    children->Start = 0;
    if (!TypeInfo(state, functionType, TI_FINDCHILDREN, children)) return false;
    for (ULONG index = 0; index < count; ++index)
    {
        ULONG parameterType = 0;
        if (!TypeInfo(state, children->ChildId[index], TI_GET_TYPE, &parameterType)) return false;
        TypeSpec parameter;
        if (!ReadPdbType(state, parameterType, parameter, 0)) return false;
        prototype.parameters.push_back(std::move(parameter));
    }
    return true;
}

bool ParseScalar(const std::string& text, TypeSpec& type)
{
    std::string value = text;
    while (!value.empty() && value.back() == ' ') value.pop_back();
    size_t pointers = 0;
    while (!value.empty() && value.back() == '*') { ++pointers; value.pop_back(); while (!value.empty() && value.back() == ' ') value.pop_back(); }
    if (value.rfind("const ", 0) == 0) value.erase(0, 6);
    if (value == "void") type.kind = TypeKind::Void;
    else if (value == "bool") { type.kind = TypeKind::Bool; type.width = 1; }
    else if (value == "char" || value == "int" || value == "long" || value == "short" || value == "__int8" || value == "__int16" || value == "__int32" || value == "__int64") { type.kind = TypeKind::Integer; type.width = value == "char" || value == "__int8" ? 8 : value == "short" || value == "__int16" ? 16 : value == "__int64" ? 64 : 32; type.isSigned = true; }
    else if (value == "float") { type.kind = TypeKind::Floating; type.width = 32; }
    else if (value == "double") { type.kind = TypeKind::Floating; type.width = 64; }
    else return false;
    if (pointers != 0) { if (type.kind == TypeKind::Void) return false; type.pointerDepth = static_cast<uint8_t>(pointers); }
    return true;
}

bool Recover(const char* name, SymbolPrototypeEvidence& result)
{
    if (name == nullptr) return false;
    char buffer[4096] = {};
    if (!pfnUnDecorateSymbolName(name, buffer, sizeof(buffer), UNDNAME_COMPLETE)) return false;
    SignatureParser parser; FunctionSpec function;
    if (!parser.Parse(buffer, function).full || function.m_CallType == "unknown" || function.m_ReturnType.empty()) return false;
    if (!ParseScalar(function.m_ReturnType, result.prototype.returnType)) return false;
    for (const std::string& parameter : function.m_ParamTypes) { TypeSpec type; if (!ParseScalar(parameter, type)) return false; result.prototype.parameters.push_back(type); }
    result.name = function.m_Name;
    result.prototype.abi = function.m_CallType;
    // Undecoration recovers a display signature, but does not prove the PDB's
    // type graph. It must remain display-only until SymGetTypeInfo extraction
    // supplies an invocation-grade declaration.
    result.prototype.source = "dbghelp-type-graph-display";
    result.prototype.quality = PrototypeQuality::Inferred;
    return true;
}

BOOL CALLBACK EnumCallback(PSYMBOL_INFO info, ULONG size, PVOID context)
{
    UNREFERENCED_PARAMETER(size);
    auto* state = static_cast<EnumerationState*>(context);
    // The SDK function-tag value is 5. Keep this adapter tolerant of SDKs
    // that omit the enum declaration from DbgHelp.h.
    if (info == nullptr || state == nullptr || state->symbols == nullptr || info->Tag != 5) return TRUE;
    SymbolPrototypeEvidence evidence;
    const bool hasDecoratedEvidence = Recover(info->Name, evidence);
    if (!hasDecoratedEvidence)
    {
        evidence.name = info->Name == nullptr ? std::string() : info->Name;
        evidence.prototype.abi = state->pointerWidth == 64 ? "x64" : "";
    }
    PrototypeSpec graphPrototype;
    if (ReadPdbFunctionPrototype(*state, info, graphPrototype))
    {
        graphPrototype.source = "dbghelp-pdb-type-graph";
        graphPrototype.quality = PrototypeQuality::ExactSymbol;
        evidence.prototype = std::move(graphPrototype);
    }
    else if (!hasDecoratedEvidence)
    {
        return TRUE;
    }
    evidence.rva = info->Address >= info->ModBase && info->Address - info->ModBase <= UINT32_MAX ? static_cast<uint32_t>(info->Address - info->ModBase) : 0;
    if (evidence.rva != 0) state->symbols->push_back(std::move(evidence));
    return TRUE;
}

bool ReadCodeView(const std::string& path, ModuleIdentity& identity, std::string& error)
{
    PEImage image; if (!PEImage::Load(path, image, error)) return false;
    const PeDataDirectory* directory = nullptr;
    for (const auto& item : image.Headers().dataDirectories) if (item.index == IMAGE_DIRECTORY_ENTRY_DEBUG && item.rva && item.size) directory = &item;
    if (directory == nullptr || directory->size < sizeof(IMAGE_DEBUG_DIRECTORY)) { error = "PDB CodeView identity is unavailable"; return false; }
    const uint32_t count = directory->size / sizeof(IMAGE_DEBUG_DIRECTORY);
    for (uint32_t index = 0; index < count; ++index)
    {
        const uint64_t offset = static_cast<uint64_t>(index) * sizeof(IMAGE_DEBUG_DIRECTORY);
        if (offset > UINT32_MAX - directory->rva) break;
        IMAGE_DEBUG_DIRECTORY debug = {};
        if (!image.ReadRva(directory->rva + static_cast<uint32_t>(offset), debug) || debug.Type != IMAGE_DEBUG_TYPE_CODEVIEW || debug.AddressOfRawData == 0 || debug.SizeOfData < 24) continue;
        uint32_t signature = 0; GUID guid = {}; uint32_t age = 0;
        if (debug.AddressOfRawData > UINT32_MAX - 24 || !image.ReadRva(debug.AddressOfRawData, signature) || signature != 0x53445352 || !image.ReadRva(debug.AddressOfRawData + 4, guid) || !image.ReadRva(debug.AddressOfRawData + 20, age)) continue;
        std::ostringstream text; text << std::hex << std::setfill('0') << std::uppercase << std::setw(8) << guid.Data1 << '-' << std::setw(4) << guid.Data2 << '-' << std::setw(4) << guid.Data3 << '-' << std::setw(2) << static_cast<unsigned>(guid.Data4[0]) << std::setw(2) << static_cast<unsigned>(guid.Data4[1]) << '-' << std::setw(2) << static_cast<unsigned>(guid.Data4[2]) << std::setw(2) << static_cast<unsigned>(guid.Data4[3]) << std::setw(2) << static_cast<unsigned>(guid.Data4[4]) << std::setw(2) << static_cast<unsigned>(guid.Data4[5]) << std::setw(2) << static_cast<unsigned>(guid.Data4[6]) << std::setw(2) << static_cast<unsigned>(guid.Data4[7]);
        identity.pdbGuid = text.str(); identity.pdbAge = age; return true;
    }
    error = "PDB CodeView identity is unavailable"; return false;
}
}

bool DbgHelpDll::EnumerateExactFunctionSymbols(const std::string& imagePath,
    const ModuleIdentity& expected, std::vector<SymbolPrototypeEvidence>& symbols,
    std::string& error)
{
    symbols.clear();
    if (expected.sha256.empty() || expected.architecture.empty()) { error = "module hash and architecture are required"; return false; }
    FunctionCatalog catalog;
    if (!FunctionCatalog::Load(imagePath, catalog, error)) return false;
    if (catalog.Module().sha256 != expected.sha256 || catalog.Module().architecture != expected.architecture) { error = "module identity mismatch"; return false; }
    ModuleIdentity actual = catalog.Module();
    if (!ReadCodeView(imagePath, actual, error)) return false;
    if (expected.pdbGuid.empty() || expected.pdbGuid != actual.pdbGuid || expected.pdbAge != actual.pdbAge) { error = "PDB CodeView GUID/age mismatch"; return false; }
    if (!Load()) { error = "Unable to load local DbgHelp"; return false; }
    auto initialize = reinterpret_cast<SymInitializeFn>(GetProcAddress(handle_, "SymInitialize"));
    auto cleanup = reinterpret_cast<SymCleanupFn>(GetProcAddress(handle_, "SymCleanup"));
    auto load = reinterpret_cast<SymLoadModuleFn>(GetProcAddress(handle_, "SymLoadModuleEx"));
    auto unload = reinterpret_cast<SymUnloadModuleFn>(GetProcAddress(handle_, "SymUnloadModule64"));
    auto enumerate = reinterpret_cast<SymEnumSymbolsFn>(GetProcAddress(handle_, "SymEnumSymbols"));
    auto setOptions = reinterpret_cast<SymSetOptionsFn>(GetProcAddress(handle_, "SymSetOptions"));
    auto getOptions = reinterpret_cast<SymGetOptionsFn>(GetProcAddress(handle_, "SymGetOptions"));
    auto getTypeInfo = reinterpret_cast<SymGetTypeInfoFn>(GetProcAddress(handle_, "SymGetTypeInfo"));
    if (!initialize || !cleanup || !load || !unload || !enumerate || !setOptions || !getOptions || !getTypeInfo) { error = "DbgHelp symbol APIs are unavailable"; return false; }
    const size_t separator = imagePath.find_last_of("\\/");
    const std::string searchPath = separator == std::string::npos ? "." : imagePath.substr(0, separator);
    HANDLE process = GetCurrentProcess(); if (!initialize(process, searchPath.c_str(), FALSE)) { error = "Unable to initialize local DbgHelp symbol session"; return false; }
    const DWORD oldOptions = getOptions(); setOptions(oldOptions & ~SYMOPT_UNDNAME);
    const DWORD64 base = load(process, nullptr, imagePath.c_str(), nullptr, 0, 0, nullptr, 0);
    if (base == 0) { setOptions(oldOptions); cleanup(process); error = "Unable to load matching local PDB"; return false; }
    const uint16_t pointerWidth = actual.architecture == "x86" ? static_cast<uint16_t>(32) : static_cast<uint16_t>(64);
    EnumerationState state{&symbols, process, base, getTypeInfo, pointerWidth}; const BOOL okay = enumerate(process, base, nullptr, EnumCallback, &state);
    for (SymbolPrototypeEvidence& item : symbols) item.module = actual;
    unload(process, base); setOptions(oldOptions); cleanup(process);
    if (!okay) { symbols.clear(); error = "DbgHelp symbol enumeration failed"; return false; }
    std::sort(symbols.begin(), symbols.end(), [](const auto& left, const auto& right) { return left.rva < right.rva; });
    return true;
}

DWORD DbgHelpDll::UnDecorateSymbolName(const char* DecoratedName, char* UnDecoratedName, DWORD UndecoratedLength, DWORD Flags)
{
	PCSTR pszDecoratedName = (PCSTR) DecoratedName;
	PSTR pszUndecoratedName = (PSTR) UnDecoratedName;
	return ( pfnUnDecorateSymbolName( pszDecoratedName, pszUndecoratedName, UndecoratedLength , Flags ) ) ;
}
