#define BOOST_TEST_MODULE StringOutputTests
#include <boost/test/included/unit_test.hpp>

#include "CallContract.h"
#include "FunctionCatalog.h"
#include "InvocationEngine.h"

#include <windows.h>
#include <algorithm>

namespace
{
std::string FixturePath()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string value(path);
    return value.substr(0, value.find_last_of("\\/") + 1) + "static_fixture.dll";
}

CallRequest Request(const char* selector, const TypeSpec& type,
    uint64_t bufferSize, const std::string& value = {})
{
    CallRequest request;
    request.correlationId = selector;
    request.selector = selector;
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = "x64";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32, true};
    request.prototypeOverride.parameters = {type};
    request.arguments = {{{type, value}}};
    request.arguments[0].bufferSize = bufferSize;
    return request;
}
}

BOOST_AUTO_TEST_CASE(BoundedNarrowAndUtf16StringOutput)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));

    TypeSpec narrow{TypeKind::String, 8, false, 1, ParameterDirection::Out, 0, "utf8"};
    CallRequest narrowRequest = Request("WriteNarrowString", narrow, 32);
    CallResult result;
    BOOST_REQUIRE_MESSAGE(InvokeX64Export(FixturePath(), narrowRequest, catalog, result, error), error);
    BOOST_REQUIRE_EQUAL(result.outputValues.size(), 1);
    BOOST_CHECK_EQUAL(result.outputValues.front().value, "fubi-output");

    TypeSpec wide = narrow;
    wide.encoding = "utf16";
    CallRequest wideRequest = Request("WriteWideString", wide, 32);
    error.clear();
    BOOST_REQUIRE_MESSAGE(InvokeX64Export(FixturePath(), wideRequest, catalog, result, error), error);
    BOOST_REQUIRE_EQUAL(result.outputValues.size(), 1);
    BOOST_CHECK_EQUAL(result.outputValues.front().value, "fubi-wide");

    narrow.direction = ParameterDirection::InOut;
    CallRequest inoutRequest = Request("WriteNarrowString", narrow, 32, "seed");
    error.clear();
    BOOST_REQUIRE_MESSAGE(InvokeX64Export(FixturePath(), inoutRequest, catalog, result, error), error);
    BOOST_CHECK_EQUAL(result.outputValues.front().value, "fubi-output");
#else
    BOOST_TEST_MESSAGE("string output test skipped for non-x64 build");
#endif
}

BOOST_AUTO_TEST_CASE(StringOutputRequiresBoundedValidBuffers)
{
    TypeSpec type{TypeKind::String, 8, false, 1, ParameterDirection::Out, 0, "utf8"};
    CallRequest request;
    request.correlationId = "string-validation";
    request.selector = "WriteNarrowString";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = "x64";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32, true};
    request.prototypeOverride.parameters = {type};
    request.arguments = {{{type, ""}}};
    std::vector<CallDiagnostic> diagnostics;
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.code == "invalid-argument-value";
    }));

    request.arguments[0].bufferSize = 1;
    request.arguments[0].type.encoding = "bad";
    diagnostics.clear();
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.code == "invalid-argument-value";
    }));

    request.arguments[0].type.encoding = "utf16";
    request.arguments[0].bufferSize = 3;
    diagnostics.clear();
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.code == "invalid-argument-value";
    }));
}
