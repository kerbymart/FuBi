#define BOOST_TEST_MODULE InvocationAdapterTests
#include <boost/test/included/unit_test.hpp>

#include "ProcessInvocation.h"

#include <windows.h>
#include <sstream>

namespace
{
std::string FixturePath()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    const std::string value(path);
    return value.substr(0, value.find_last_of("\\/") + 1) + "export_fixture.dll";
}

class FakeInvocationAdapter final : public InvocationAdapter
{
public:
    unsigned calls = 0;
    NormalizedCall received;

    bool Invoke(const NormalizedCall& call, CallResult& result,
        std::string& error) override
    {
        ++calls;
        received = call;
        error.clear();
        result.success = true;
        result.status = "completed";
        result.returnType = call.prototype.returnType;
        result.returnValue = "fake-result";
        return true;
    }
};

CallRequest ValidRequest(const FunctionCatalog& catalog)
{
    CallRequest request;
    request.correlationId = "adapter-test";
    request.selector = "NamedExport";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = catalog.Module().architecture == "x64" ? "x64" : "__cdecl";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32};
    request.prototypeOverride.parameters = {{TypeKind::Integer, 32}};
    request.arguments = {{{TypeKind::Integer, 32}, "7"}};
    return request;
}
}

BOOST_AUTO_TEST_CASE(InvalidRequestsNeverReachAdapter)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    CallRequest request = ValidRequest(catalog);
    request.arguments[0].value = "not-an-integer";
    FakeInvocationAdapter adapter;
    CallResult result;
    BOOST_CHECK(!DispatchCall(request, catalog, adapter, result, error));
    BOOST_CHECK_EQUAL(adapter.calls, 0U);
    BOOST_CHECK_EQUAL(result.status, "validation-failed");
}

BOOST_AUTO_TEST_CASE(ValidRequestsUseNormalizedContractAndDeterministicResult)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    const CallRequest request = ValidRequest(catalog);
    FakeInvocationAdapter adapter;
    CallResult result;
    BOOST_REQUIRE(DispatchCall(request, catalog, adapter, result, error));
    BOOST_CHECK_EQUAL(adapter.calls, 1U);
    BOOST_CHECK_EQUAL(adapter.received.request.correlationId, request.correlationId);
    BOOST_CHECK_EQUAL(adapter.received.request.arguments.size(), 1U);
    BOOST_CHECK_EQUAL(adapter.received.prototype.abi, request.prototypeOverride.abi);

    std::ostringstream first;
    std::ostringstream second;
    WriteCallResultJson(first, result);
    WriteCallResultJson(second, result);
    BOOST_CHECK_EQUAL(first.str(), second.str());

    std::ostringstream text;
    WriteCallResultText(text, result);
    BOOST_CHECK_EQUAL(text.str(),
        "schema_version: 1\naction: call\ncorrelation_id: adapter-test\n"
        "status: completed\nsuccess: true\nreturn_value: fake-result\n"
        "diagnostics: 0\n");
}
