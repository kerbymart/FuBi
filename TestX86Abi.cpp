#define BOOST_TEST_MODULE X86AbiWorkerTests
#include <boost/test/included/unit_test.hpp>

#include "CallContract.h"
#include "FunctionCatalog.h"
#include "ProcessInvocation.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
std::string DirectoryOfExecutable()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string result(path);
    return result.substr(0, result.find_last_of("\\/") + 1);
}

std::string FixturePath()
{
    return DirectoryOfExecutable() + "x86_abi_fixture.dll";
}

std::string WorkerPath()
{
    return DirectoryOfExecutable() + "FubiInvocationWorker_x86.exe";
}

void RemoveMarker()
{
    DeleteFileA((DirectoryOfExecutable() + "x86_abi_fixture.executed").c_str());
}

bool HasDiagnostic(const CallResult& result, const char* code)
{
    for (const CallDiagnostic& diagnostic : result.diagnostics)
        if (diagnostic.code == code) return true;
    return false;
}

CallRequest Request(const char* selector, const char* abi, TypeSpec returnType,
    std::vector<CallArgument> arguments = {})
{
    CallRequest request;
    request.correlationId = selector;
    request.selector = selector;
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = abi;
    request.prototypeOverride.returnType = returnType;
    for (const CallArgument& argument : arguments)
        request.prototypeOverride.parameters.push_back(argument.type);
    request.arguments = std::move(arguments);
    return request;
}

CallArgument IntegerArgument(int value)
{
    return {{TypeKind::Integer, 32, true}, std::to_string(value)};
}

void CheckCall(const FunctionCatalog& catalog, CallRequest request,
    const std::string& expected)
{
    CallResult result;
    std::string error;
    BOOST_REQUIRE_MESSAGE(InvokeX64ExportProcess(FixturePath(), request,
        catalog, result, error), error);
    BOOST_CHECK(result.success);
    BOOST_CHECK_EQUAL(result.returnValue, expected);
}
}

BOOST_AUTO_TEST_CASE(ScalarReturnWidthsAndRepeatedConventionCalls)
{
    RemoveMarker();
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    BOOST_CHECK_EQUAL(GetFileAttributesA((DirectoryOfExecutable() +
        "x86_abi_fixture.executed").c_str()), INVALID_FILE_ATTRIBUTES);

    CheckCall(catalog, Request("CdeclReturn8", "__cdecl",
        {TypeKind::Integer, 8, false}), "165");
    CheckCall(catalog, Request("CdeclReturn16", "__cdecl",
        {TypeKind::Integer, 16, false}), "48879");
    CheckCall(catalog, Request("CdeclReturn32", "__cdecl",
        {TypeKind::Integer, 32, false}), "3735928559");
    CheckCall(catalog, Request("CdeclReturn64", "__cdecl",
        {TypeKind::Integer, 64, false}), "18364758544493064720");
    CheckCall(catalog, Request("StdcallReturn64", "__stdcall",
        {TypeKind::Integer, 64, false}), "81985529216486895");

    std::vector<CallArgument> arguments;
    for (int value = 1; value <= 8; ++value)
        arguments.push_back(IntegerArgument(value));
    for (int repeat = 0; repeat < 12; ++repeat)
    {
        CheckCall(catalog, Request("CdeclSum8", "__cdecl",
            {TypeKind::Integer, 32, true}, arguments), "36");
        CheckCall(catalog, Request("StdcallSum8", "__stdcall",
            {TypeKind::Integer, 32, true}, arguments), "36");
    }
    BOOST_CHECK_EQUAL(GetFileAttributesA((DirectoryOfExecutable() +
        "x86_abi_fixture.executed").c_str()), INVALID_FILE_ATTRIBUTES);
}

BOOST_AUTO_TEST_CASE(ArchitectureRejectionHappensBeforeTargetLoad)
{
    RemoveMarker();
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    CallRequest request = Request("CdeclReturn32", "x64",
        {TypeKind::Integer, 32, false});
    CallResult result;
    BOOST_CHECK(!InvokeX64ExportProcess(FixturePath(), request, catalog,
        result, error));
    BOOST_CHECK_EQUAL(result.status, "validation-failed");
    BOOST_CHECK(HasDiagnostic(result, "unsupported-abi"));
    BOOST_CHECK_EQUAL(GetFileAttributesA((DirectoryOfExecutable() +
        "x86_abi_fixture.executed").c_str()), INVALID_FILE_ATTRIBUTES);
}

BOOST_AUTO_TEST_CASE(MissingWorkerIsReportedBeforeTargetLoad)
{
    RemoveMarker();
    const std::string worker = WorkerPath();
    const std::string hidden = worker + ".missing";
    DeleteFileA(hidden.c_str());
    BOOST_REQUIRE(MoveFileA(worker.c_str(), hidden.c_str()) != FALSE);

    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    CallRequest request = Request("CdeclReturn32", "__cdecl",
        {TypeKind::Integer, 32, false});
    CallResult result;
    const bool invoked = InvokeX64ExportProcess(FixturePath(), request,
        catalog, result, error);
    BOOST_CHECK(!invoked);
    BOOST_CHECK_EQUAL(result.status, "worker-failed");
    BOOST_CHECK(HasDiagnostic(result, "worker-architecture"));
    BOOST_CHECK_EQUAL(GetFileAttributesA((DirectoryOfExecutable() +
        "x86_abi_fixture.executed").c_str()), INVALID_FILE_ATTRIBUTES);
    BOOST_REQUIRE(MoveFileA(hidden.c_str(), worker.c_str()) != FALSE);
}
