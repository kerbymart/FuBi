#define BOOST_TEST_MODULE CallContractTests
#include <boost/test/included/unit_test.hpp>

#include "CallContract.h"

#include <windows.h>
#include <algorithm>
#include <sstream>

namespace
{
std::string FixturePath()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string value(path);
    return value.substr(0, value.find_last_of("\\/") + 1) + "export_fixture.dll";
}
}

BOOST_AUTO_TEST_CASE(ValidTypedRequestAndDeterministicResult)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    CallRequest request;
    request.correlationId = "call-1";
    request.selector = "NamedExport";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = catalog.Module().architecture == "x64" ? "x64" : "__cdecl";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32};
    request.prototypeOverride.parameters = {{TypeKind::Integer, 32}, {TypeKind::Integer, 32}};
    request.arguments = {{{TypeKind::Integer, 32}, "1"}, {{TypeKind::Integer, 32}, "2"}};
    std::vector<CallDiagnostic> diagnostics;
    BOOST_CHECK(ValidateCallRequest(request, catalog, diagnostics));
    std::ostringstream first, second;
    WriteCallRequestJson(first, request);
    WriteCallRequestJson(second, request);
    BOOST_CHECK_EQUAL(first.str(), second.str());
    CallResult result;
    result.correlationId = request.correlationId;
    result.diagnostics = diagnostics;
    std::ostringstream resultJson;
    WriteCallResultJson(resultJson, result);
    BOOST_CHECK(resultJson.str().find("not-executed") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(ValidationRejectsBadRangeCountAndDisplayOnlyPrototype)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    CallRequest request;
    request.correlationId = "call-2";
    request.selector = "NamedExport";
    request.arguments = {{{TypeKind::Integer, 8}, "256"}};
    std::vector<CallDiagnostic> diagnostics;
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const CallDiagnostic& item) { return item.code == "prototype-required"; }));
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::Inferred;
    request.prototypeOverride.abi = "x64";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32};
    request.prototypeOverride.parameters = {{TypeKind::Integer, 8}};
    diagnostics.clear();
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const CallDiagnostic& item) { return item.code == "prototype-not-invocation-grade"; }));
}
