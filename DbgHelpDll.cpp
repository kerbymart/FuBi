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

struct EnumerationState { std::vector<SymbolPrototypeEvidence>* symbols = nullptr; HANDLE process = nullptr; ULONG64 module = 0; SymGetTypeInfoFn getTypeInfo = nullptr; };

bool HasBoundedTypeGraph(const EnumerationState& state, const SYMBOL_INFO* info)
{
    if (state.getTypeInfo == nullptr || info == nullptr || info->TypeIndex == 0) return false;
    ULONG typeId = 0; if (!state.getTypeInfo(state.process, state.module, info->TypeIndex, TI_GET_TYPEID, &typeId) || typeId == 0) return false;
    ULONG children = 0; if (!state.getTypeInfo(state.process, state.module, typeId, TI_GET_CHILDRENCOUNT, &children) || children > 128) return false;
    ULONG tag = 0; if (!state.getTypeInfo(state.process, state.module, typeId, TI_GET_SYMTAG, &tag)) return false;
    ULONG baseType = 0; ULONG64 length = 0; state.getTypeInfo(state.process, state.module, typeId, TI_GET_BASETYPE, &baseType); state.getTypeInfo(state.process, state.module, typeId, TI_GET_LENGTH, &length);
    // Function and pointer nodes must expose a bounded child/type relation;
    // base scalar nodes must expose a nonzero size. This deliberately avoids
    // guessing aggregate layouts from names.
    if (tag == 13 || tag == 14) { ULONG child = 0; if (!state.getTypeInfo(state.process, state.module, typeId, TI_GET_TYPE, &child) || child == 0) return false; }
    if (tag == 16 && length == 0) return false;
    return tag == 13 || tag == 14 || tag == 16;
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
    result.prototype.source = "dbghelp-type-graph-v1";
    result.prototype.quality = PrototypeQuality::ExactSymbol;
    return true;
}

BOOL CALLBACK EnumCallback(PSYMBOL_INFO info, ULONG size, PVOID context)
{
    UNREFERENCED_PARAMETER(size);
    auto* state = static_cast<EnumerationState*>(context);
    // DIA's SymTagFunction value is 5. Keep this adapter tolerant of SDKs
    // that omit the DIA enum declaration from DbgHelp.h.
    if (info == nullptr || state == nullptr || state->symbols == nullptr || info->Tag != 5) return TRUE;
    SymbolPrototypeEvidence evidence;
    if (!HasBoundedTypeGraph(*state, info) || !Recover(info->Name, evidence)) return TRUE;
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
    EnumerationState state{&symbols, process, base, getTypeInfo}; const BOOL okay = enumerate(process, base, nullptr, EnumCallback, &state);
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
