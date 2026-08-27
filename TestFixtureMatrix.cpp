#define BOOST_TEST_MODULE FixtureMatrixTests
#include <boost/test/included/unit_test.hpp>

#include "CallContract.h"
#include "FunctionCatalog.h"
#include "InvocationEngine.h"

#include <windows.h>

#include <algorithm>
#include <string>

namespace
{
std::string FixturePath()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    const std::string value(path);
    return value.substr(0, value.find_last_of("\\/") + 1) + "export_fixture.dll";
}

std::string HandleFixturePath()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    const std::string value(path);
    return value.substr(0, value.find_last_of("\\/") + 1) + "handle_fixture.dll";
}

CallRequest LengthRequest(const char* selector, const char* encoding,
    const std::string& value, uint64_t bufferSize)
{
    TypeSpec type{TypeKind::String, 8, false, 1, ParameterDirection::In, 0,
        encoding};
    CallRequest request;
    request.correlationId = selector;
    request.selector = selector;
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = "x64";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32, true};
    request.prototypeOverride.parameters = {type};
    request.arguments = {{{type, value, bufferSize}}};
    return request;
}

int InvokeLength(const char* selector, const char* encoding,
    const std::string& value, uint64_t bufferSize, FunctionCatalog& catalog)
{
    CallResult result;
    std::string error;
    const CallRequest request = LengthRequest(selector, encoding, value, bufferSize);
    BOOST_REQUIRE_MESSAGE(InvokeX64Export(FixturePath(), request, catalog, result, error), error);
    return std::stoi(result.returnValue);
}
}

BOOST_AUTO_TEST_CASE(CStringAndUtf16LengthFixtures)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));

    BOOST_CHECK_EQUAL(InvokeLength("NarrowStringLength", "cstr", "fubi", 5, catalog), 4);
    BOOST_CHECK_EQUAL(InvokeLength("NarrowStringLength", "utf8", "", 1, catalog), 0);
    BOOST_CHECK_EQUAL(InvokeLength("WideStringLength", "wstr", "h\xC3\xA9llo", 12, catalog), 5);
    BOOST_CHECK_EQUAL(InvokeLength("WideStringLength", "utf16", "", 2, catalog), 0);
#else
    BOOST_TEST_MESSAGE("fixture matrix test skipped for non-x64 build");
#endif
}

BOOST_AUTO_TEST_CASE(StringLengthRejectsInsufficientTerminatorCapacity)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    const CallRequest request = LengthRequest("NarrowStringLength", "cstr", "fubi", 4);
    CallResult result;
    BOOST_CHECK(!InvokeX64Export(FixturePath(), request, catalog, result, error));
    BOOST_CHECK(error.find("exceeds its buffer size") != std::string::npos);
#endif
}

BOOST_AUTO_TEST_CASE(StringLengthRejectsOversizedPayloadBeforeInvocation)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));

    const std::string oversized(16 * 1024 * 1024 + 1, 'x');
    const CallRequest request = LengthRequest(
        "NarrowStringLength", "cstr", oversized, oversized.size() + 1);
    std::vector<CallDiagnostic> diagnostics;
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.code == "invalid-argument-value";
    }));
#endif
}

BOOST_AUTO_TEST_CASE(PointerEchoRejectsMismatchedPointerWidth)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));

    CallRequest request;
    request.correlationId = "pointer-width";
    request.selector = "PointerEcho";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = "x64";
    request.prototypeOverride.returnType = {TypeKind::Pointer, 64, false, 1};
    request.prototypeOverride.parameters = {{TypeKind::Pointer, 64, false, 1}};
    request.arguments = {{{TypeKind::Pointer, 32, false, 1}, "opaque:0x1"}};

    std::vector<CallDiagnostic> diagnostics;
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.code == "argument-type-mismatch";
    }));
#endif
}

BOOST_AUTO_TEST_CASE(HandleEchoRejectsRawReferenceBeforeInvocation)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(HandleFixturePath(), catalog, error));

    TypeSpec handle{TypeKind::Handle, 64, false, 0, ParameterDirection::In,
        0, {}, "borrowed"};
    CallRequest request;
    request.correlationId = "handle-reference";
    request.selector = "EchoHandle";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = "x64";
    request.prototypeOverride.returnType = handle;
    request.prototypeOverride.parameters = {handle};
    request.arguments = {{{handle, "opaque:0x1"}}};

    std::vector<CallDiagnostic> diagnostics;
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.code == "raw-handle-rejected";
    }));
#endif
}
