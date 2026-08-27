/**
 * @file SysExports.cpp
 * @brief Enumerates a loaded module's complete PE export table.
 */
#include "StdAfx.h"
#include "SysExports.h"
#include "DbgHelpDll.h"

#include <fstream>
#include <iomanip>
#include <unordered_map>

namespace
{
const char* OrdinalName(DWORD ordinal, std::string& storage)
{
    storage = "#" + std::to_string(ordinal);
    return storage.c_str();
}

void WriteExport(std::ostream& output, const FunctionSpec& function)
{
    output << "[export " << function.m_SerialID << "]\n";
    output << "ordinal = " << function.m_Ordinal << "\n";
    output << "rva = 0x" << std::hex << std::uppercase << function.m_Rva
           << std::dec << std::nouppercase << "\n";
    output << "name = " << function.m_Name << "\n";
    output << "export_names = ";
    if (function.m_ExportNames.empty())
    {
        output << "<ordinal-only>";
    }
    else
    {
        for (size_t index = 0; index < function.m_ExportNames.size(); ++index)
        {
            if (index != 0) output << ", ";
            output << function.m_ExportNames[index];
        }
    }
    output << "\n";

    if (function.IsForwarder())
    {
        output << "forwarder = " << function.m_Forwarder << "\n";
    }
    else
    {
        output << "address = 0x" << std::hex << std::uppercase << function.m_dwAddress
               << std::dec << std::nouppercase << "\n";
    }

    output << "signature = "
           << (function.HasRecoveredSignature()
                   ? function.m_Signature
                   : "<unavailable: not encoded in PE export table>")
           << "\n";
    output << "return_type = " << function.m_ReturnType << "\n";
    output << "calling_convention = " << function.m_CallType << "\n";

    output << "parameters = ";
    if (function.m_ParamTypes.empty())
    {
        output << "<unknown>";
    }
    else
    {
        for (size_t index = 0; index < function.m_ParamTypes.size(); ++index)
        {
            if (index != 0)
            {
                output << ", ";
            }
            output << function.m_ParamTypes[index];
        }
    }
    output << "\n\n";
}
}

SysExports::SysExports(void)
{
}

SysExports::~SysExports(void)
{
}

bool SysExports::ImportBindings(HMODULE module)
{
    m_Functions.clear();

    if (module == NULL)
    {
        module = ::GetModuleHandle(NULL);
    }

    const BYTE* imageBase = reinterpret_cast<const BYTE*>(module);
    const IMAGE_DOS_HEADER* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return false;
    }

    const IMAGE_NT_HEADERS* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        imageBase + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
    {
        return false;
    }

    const IMAGE_DATA_DIRECTORY& exportData =
        ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exportData.VirtualAddress == 0 || exportData.Size < sizeof(IMAGE_EXPORT_DIRECTORY))
    {
        return false;
    }

    const DWORD exportBegin = exportData.VirtualAddress;
    const DWORD exportEnd = exportBegin + exportData.Size;
    const IMAGE_EXPORT_DIRECTORY* exports =
        reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(imageBase + exportBegin);
    const DWORD* functionRvas =
        reinterpret_cast<const DWORD*>(imageBase + exports->AddressOfFunctions);
    const DWORD* nameRvas =
        reinterpret_cast<const DWORD*>(imageBase + exports->AddressOfNames);
    const WORD* nameOrdinals =
        reinterpret_cast<const WORD*>(imageBase + exports->AddressOfNameOrdinals);

    std::unordered_map<DWORD, std::vector<std::string>> namesByFunctionIndex;
    for (DWORD nameIndex = 0; nameIndex < exports->NumberOfNames; ++nameIndex)
    {
        const DWORD functionIndex = nameOrdinals[nameIndex];
        if (functionIndex >= exports->NumberOfFunctions)
        {
            continue;
        }

        namesByFunctionIndex[functionIndex].push_back(
            reinterpret_cast<const char*>(imageBase + nameRvas[nameIndex]));
    }

    SignatureParser parser;
    m_Functions.reserve(exports->NumberOfFunctions);
    for (DWORD functionIndex = 0; functionIndex < exports->NumberOfFunctions; ++functionIndex)
    {
        const DWORD functionRva = functionRvas[functionIndex];
        if (functionRva == 0)
        {
            continue;
        }

        FunctionSpec function;
        function.m_SerialID = static_cast<DWORD>(m_Functions.size());
        function.m_Ordinal = exports->Base + functionIndex;
        function.m_Rva = functionRva;

        const auto name = namesByFunctionIndex.find(functionIndex);
        if (name != namesByFunctionIndex.end())
        {
            function.m_ExportNames = name->second;
            function.m_DecoratedName = function.m_ExportNames.front();
            function.m_Name = function.m_DecoratedName;
        }
        else
        {
            std::string ordinalName;
            function.m_Name = OrdinalName(function.m_Ordinal, ordinalName);
        }

        if (functionRva >= exportBegin && functionRva < exportEnd)
        {
            function.m_Forwarder = reinterpret_cast<const char*>(imageBase + functionRva);
        }
        else
        {
            function.m_dwAddress = reinterpret_cast<uintptr_t>(imageBase + functionRva);
        }

        RecoverSignature(function, parser);
        m_Functions.push_back(function);
    }

    return true;
}

void SysExports::RecoverSignature(FunctionSpec& function, SignatureParser& parser)
{
    if (function.m_ExportNames.empty())
    {
        return;
    }

    static DbgHelpDll dbgHelp;
    static const bool dbgHelpLoaded = dbgHelp.Load();
    if (!dbgHelpLoaded)
    {
        return;
    }

    char undecoratedName[0x1000] = {};
    for (const std::string& exportName : function.m_ExportNames)
    {
        if (dbgHelp.UnDecorateSymbolName(
                exportName.c_str(), undecoratedName,
                static_cast<DWORD>(sizeof(undecoratedName)), UNDNAME_COMPLETE) != 0 &&
            exportName != undecoratedName)
        {
            function.m_DecoratedName = exportName;
            break;
        }
        undecoratedName[0] = '\0';
    }

    // Plain C export names do not contain type information.
    if (undecoratedName[0] == '\0')
    {
        return;
    }

    function.m_Signature = undecoratedName;

    FunctionSpec parsed = function;
    parsed.m_ParamTypes.clear();
    const parse_info<> result = parser.Parse(undecoratedName, parsed);
    if (result.full)
    {
        function.m_Name = parsed.m_Name;
        function.m_ReturnType = parsed.m_ReturnType;
        function.m_CallType = parsed.m_CallType;
        function.m_ParamTypes = parsed.m_ParamTypes;
    }
}

void SysExports::PrintFunctionInfo() const
{
    std::cout << "Number of exports: " << m_Functions.size() << "\n";

    for (const FunctionSpec& function : m_Functions)
    {
        std::cout << "================================\n";
        std::cout << "ID: " << function.m_SerialID << "\n";
        std::cout << "Ordinal: " << function.m_Ordinal << "\n";
        std::cout << "RVA: 0x" << std::hex << std::uppercase << function.m_Rva
                  << std::dec << std::nouppercase << "\n";
        std::cout << "Name: " << function.m_Name << "\n";

        if (!function.m_DecoratedName.empty() &&
            function.m_DecoratedName != function.m_Name)
        {
            std::cout << "Export name: " << function.m_DecoratedName << "\n";
        }

        if (function.m_ExportNames.size() > 1)
        {
            std::cout << "Aliases: ";
            for (size_t index = 0; index < function.m_ExportNames.size(); ++index)
            {
                if (index != 0) std::cout << ", ";
                std::cout << function.m_ExportNames[index];
            }
            std::cout << "\n";
        }

        if (function.IsForwarder())
        {
            std::cout << "Forwarder: " << function.m_Forwarder << "\n";
        }
        else
        {
            std::cout << "Address: 0x" << std::hex << std::uppercase
                      << function.m_dwAddress << std::dec << std::nouppercase << "\n";
        }

        if (function.HasRecoveredSignature())
        {
            std::cout << "Signature: " << function.m_Signature << "\n";
        }
        else
        {
            std::cout << "Signature: unavailable in PE export table\n";
        }
    }
}

bool SysExports::DumpFunctionInfo(
    const std::string& filePath, const std::string& modulePath) const
{
    std::ofstream output(filePath, std::ios::out | std::ios::trunc);
    if (!output)
    {
        return false;
    }

    output << "FuBi export signature dump\n";
    output << "module = " << modulePath << "\n";
    output << "export_count = " << m_Functions.size() << "\n\n";
    for (const FunctionSpec& function : m_Functions)
    {
        WriteExport(output, function);
    }

    return output.good();
}
