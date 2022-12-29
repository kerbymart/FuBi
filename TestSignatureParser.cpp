/**
 * @file TestSignatureParser.cpp
 * @brief
 *
 * @author Kerby
 * @date 2022-12-20
 */

#define BOOST_TEST_MAIN
#if !defined( WIN32 )
#define BOOST_TEST_DYN_LINK
#endif

#include <boost/test/unit_test.hpp>
//#include "SysExports.h"

// Create a test suite
BOOST_AUTO_TEST_SUITE(SignatureParserTestSuite)

// Define a test case for parsing a valid function signature
    BOOST_AUTO_TEST_CASE(ParseValidSignature) {
    }

// Define a test case for parsing an invalid function signature
    BOOST_AUTO_TEST_CASE(ParseInvalidSignature) {
    }

// End the test suite
BOOST_AUTO_TEST_SUITE_END()
