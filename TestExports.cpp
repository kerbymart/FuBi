#define BOOST_TEST_MODULE ExportEnumerationTests
#include <boost/test/included/unit_test.hpp>

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

#include "SysExports.h"

namespace
{
std::wstring FixturePath()
{
    wchar_t executablePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, executablePath, MAX_PATH);
    std::wstring path(executablePath);
    const std::wstring::size_type separator = path.find_last_of(L"\\/");
    return path.substr(0, separator + 1) + L"export_fixture.dll";
}

std::string DumpPath()
{
	char executablePath[MAX_PATH] = {};
	GetModuleFileNameA(NULL, executablePath, MAX_PATH);
	std::string path(executablePath);
	return path.substr(0, path.find_last_of("\\/") + 1) + "export_fixture.dump.txt";
}

const FunctionSpec* FindOrdinal(const SysExports& exports, DWORD ordinal)
{
    const auto found = std::find_if(
        exports.m_Functions.begin(), exports.m_Functions.end(),
        [ordinal](const FunctionSpec& function) { return function.m_Ordinal == ordinal; });
    return found == exports.m_Functions.end() ? nullptr : &*found;
}
}

BOOST_AUTO_TEST_CASE(EnumeratesNamedOrdinalAndForwardedExports)
{
    HMODULE fixture = LoadLibraryW(FixturePath().c_str());
    BOOST_REQUIRE_MESSAGE(fixture != NULL, "Could not load export fixture DLL");

    SysExports exports;
    BOOST_REQUIRE(exports.ImportBindings(fixture));

    const FunctionSpec* named = FindOrdinal(exports, 1);
    BOOST_REQUIRE(named != nullptr);
    BOOST_CHECK_EQUAL(named->m_Name, "NamedExport");
    BOOST_REQUIRE_EQUAL(named->m_ExportNames.size(), 1U);
    BOOST_CHECK_EQUAL(named->m_ExportNames[0], "NamedExport");

    const FunctionSpec* ordinalOnly = FindOrdinal(exports, 2);
    BOOST_REQUIRE(ordinalOnly != nullptr);
    BOOST_CHECK_EQUAL(ordinalOnly->m_Name, "#2");
    BOOST_CHECK(ordinalOnly->m_DecoratedName.empty());

    const FunctionSpec* forwarded = FindOrdinal(exports, 3);
    BOOST_REQUIRE(forwarded != nullptr);
    BOOST_CHECK(forwarded->IsForwarder());
    BOOST_CHECK_EQUAL(forwarded->m_Forwarder, "KERNEL32.Sleep");

    const auto signature = std::find_if(
        exports.m_Functions.begin(), exports.m_Functions.end(),
        [](const FunctionSpec& function) { return function.HasRecoveredSignature(); });
    BOOST_REQUIRE(signature != exports.m_Functions.end());
    BOOST_CHECK_EQUAL(signature->m_Name, "AddNumbers");
    BOOST_CHECK_EQUAL(signature->m_ReturnType, "int");
    BOOST_CHECK_EQUAL(signature->m_CallType, "__cdecl");
    BOOST_REQUIRE_EQUAL(signature->m_ParamTypes.size(), 2U);
    BOOST_CHECK_EQUAL(signature->m_ParamTypes[0], "int");
    BOOST_CHECK_EQUAL(signature->m_ParamTypes[1], "int");

	const std::string dumpPath = DumpPath();
	BOOST_REQUIRE(exports.DumpFunctionInfo(dumpPath, "export_fixture.dll"));

	std::ifstream dump(dumpPath);
    std::ostringstream contents;
    contents << dump.rdbuf();
    BOOST_CHECK(contents.str().find("export_count = 4") != std::string::npos);
    BOOST_CHECK(contents.str().find("name = #2") != std::string::npos);
    BOOST_CHECK(contents.str().find("forwarder = KERNEL32.Sleep") != std::string::npos);
    BOOST_CHECK(contents.str().find("signature = int __cdecl AddNumbers(int,int)") != std::string::npos);

    FreeLibrary(fixture);
	DeleteFileA(dumpPath.c_str());
}
