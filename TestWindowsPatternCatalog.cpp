#define BOOST_TEST_MODULE WindowsPatternCatalogTests
#include <boost/test/included/unit_test.hpp>
#include "WindowsPatternCatalog.h"
BOOST_AUTO_TEST_CASE(RecognizersRequireExactDocumentedBytes){BOOST_CHECK(MatchWindowsPattern({0x48,0x83,0xEC,0x28},0,"win-x64-stack-prologue"));BOOST_CHECK(!MatchWindowsPattern({0x48,0x83,0xEC,0x20},0,"win-x64-stack-prologue"));BOOST_CHECK(!MatchWindowsPattern({0x49,0x83,0xEC,0x28},0,"win-x64-stack-prologue"));BOOST_CHECK(MatchWindowsPattern({0x55,0x8B,0xEC},0,"win-x86-frame-prologue"));BOOST_CHECK(!MatchWindowsPattern({0x55,0x89,0xE5},0,"win-x86-frame-prologue"));}
BOOST_AUTO_TEST_CASE(UnknownAndTruncatedPatternsAreRejected){BOOST_CHECK(!MatchWindowsPattern({0x48,0x83,0xEC},0,"win-x64-stack-prologue"));BOOST_CHECK(!MatchWindowsPattern({0x55,0x8B,0xEC},1,"win-x86-frame-prologue"));BOOST_CHECK(!MatchWindowsPattern({0x55,0x8B,0xEC},0,"unknown"));}
