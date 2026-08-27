#define BOOST_TEST_MODULE SessionReferenceTests
#include <boost/test/included/unit_test.hpp>

#include "SessionReferences.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(IssuesOpaqueNonNumericReferencesAndResolvesWithinSession)
{
    SessionReferences references;
    const std::string first = references.Issue(0x123456789abcdef0ULL);
    const std::string second = references.Issue(0x42);

    BOOST_CHECK_EQUAL(first, "opaque:session-1");
    BOOST_CHECK_EQUAL(second, "opaque:session-2");
    BOOST_CHECK(SessionReferences::IsWellFormed(first));
    BOOST_CHECK(first.find("0x") == std::string::npos);

    uint64_t value = 0;
    BOOST_REQUIRE(references.Resolve(first, value));
    BOOST_CHECK_EQUAL(value, 0x123456789abcdef0ULL);
    BOOST_REQUIRE(references.Resolve(second, value));
    BOOST_CHECK_EQUAL(value, 0x42ULL);
}

BOOST_AUTO_TEST_CASE(ReleaseIsOneShotAndDoesNotAffectAnotherReference)
{
    SessionReferences references;
    const std::string released = references.Issue(1);
    const std::string retained = references.Issue(2);

    BOOST_CHECK(references.Release(released));
    BOOST_CHECK(!references.Release(released));
    uint64_t value = 0;
    BOOST_CHECK(!references.Resolve(released, value));
    BOOST_REQUIRE(references.Resolve(retained, value));
    BOOST_CHECK_EQUAL(value, 2ULL);
}

BOOST_AUTO_TEST_CASE(MalformedAndForeignReferencesAreRejected)
{
    SessionReferences first;
    SessionReferences second;
    const std::string local = first.Issue(7);
    uint64_t value = 0;

    const char* malformed[] = {
        "opaque:0x1234", "opaque:session-", "opaque:session-0",
        "opaque:session-01", "opaque:session-1x", "opaque:session-18446744073709551616"
    };
    for (const char* reference : malformed)
    {
        BOOST_CHECK(!SessionReferences::IsWellFormed(reference));
        BOOST_CHECK(!first.Resolve(reference, value));
        BOOST_CHECK(!first.Release(reference));
    }

    BOOST_CHECK(!second.Resolve(local, value));
    BOOST_CHECK(!second.Release(local));
    BOOST_CHECK(first.Resolve(local, value));
}

BOOST_AUTO_TEST_CASE(HandlesRequireIdentityAndConfiguredReleaseAdapter)
{
    SessionReferences references;
    uint64_t released = 0;
    const std::string token = references.IssueHandle(0x42, {64, "module-hash", "x64", "owned",
        [&](uint64_t value) { released = value; return true; }});
    BOOST_CHECK_EQUAL(token, "opaque:session-1");
    uint64_t value = 0;
    BOOST_CHECK(!references.Resolve(token, value));
    BOOST_REQUIRE(references.ResolveHandle(token, value, "module-hash", "x64"));
    BOOST_CHECK_EQUAL(value, 0x42ULL);
    BOOST_CHECK(!references.ResolveHandle(token, value, "other-hash", "x64"));
    BOOST_CHECK(!references.ReleaseHandle(token, "module-hash", "x86"));
    BOOST_CHECK(references.ReleaseHandle(token, "module-hash", "x64"));
    BOOST_CHECK_EQUAL(released, 0x42ULL);
    BOOST_CHECK(!references.ReleaseHandle(token, "module-hash", "x64"));
}

BOOST_AUTO_TEST_CASE(UnconfiguredOrInvalidHandlesFailClosed)
{
    SessionReferences references;
    BOOST_CHECK(references.IssueHandle(1, {32, "hash", "x86", "owned", {}}).empty() == false);
    BOOST_CHECK(references.IssueHandle(2, {16, "hash", "x86", "owned", {}}).empty());
    BOOST_CHECK(references.IssueHandle(3, {64, "hash", "x64", "unknown", {}}).empty());
    const std::string token = references.IssueHandle(4, {64, "hash", "x64", "borrowed", {}});
    BOOST_CHECK(!references.ReleaseHandle(token, "hash", "x64"));
    uint64_t value = 0;
    BOOST_CHECK(references.ResolveHandle(token, value, "hash", "x64"));
}
