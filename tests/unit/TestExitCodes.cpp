#define BOOST_TEST_MODULE ExitCodeContractTests
#include <boost/test/included/unit_test.hpp>
#include "ExitCodes.h"

BOOST_AUTO_TEST_CASE(AllExitCodesAreStable)
{
    BOOST_CHECK_EQUAL(FubiExitCode::Success, 0);
    BOOST_CHECK_EQUAL(FubiExitCode::Usage, 2);
    BOOST_CHECK_EQUAL(FubiExitCode::CatalogLoadFailed, 3);
    BOOST_CHECK_EQUAL(FubiExitCode::SelectorNotFound, 4);
    BOOST_CHECK_EQUAL(FubiExitCode::SelectorAmbiguous, 5);
    BOOST_CHECK_EQUAL(FubiExitCode::ProfileLoadFailed, 6);
    BOOST_CHECK_EQUAL(FubiExitCode::SymbolLoadFailed, 7);
    BOOST_CHECK_EQUAL(FubiExitCode::ValidationFailed, 8);
    BOOST_CHECK_EQUAL(FubiExitCode::InvocationFailed, 9);
}
