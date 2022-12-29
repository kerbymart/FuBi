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
#include <windows.h>
#include <iostream>
#include "SysExports.h"

// Create a test suite
BOOST_AUTO_TEST_SUITE(SignatureParserTestSuite)

    // Define a test case for parsing a valid function signature
    BOOST_AUTO_TEST_CASE(ParseValidSignature) {
        // Create an instance of the SignatureParser
        SignatureParser parser;
        FunctionSpec spec;
        spec.m_SerialID = 0;
        spec.m_dwAddress = 0;

        // Define the input string and expected output
        std::string input = "public: bool __thiscall FooClass::Foo(void)";
        std::string expectedName = "Foo";
        std::vector<std::string> expectedParamTypes = {"void"};

        parse_info<> r = parser.Parse(input.c_str(), spec);
        std::cout << "Function ID:      " << spec.m_SerialID << std::endl;
        std::cout << "Function Address: " << spec.m_dwAddress << std::endl;
        std::cout << "Function name:    " << spec.m_Name << std::endl;
        std::cout << "Return Type:      " << spec.m_ReturnType << std::endl;
        std::cout << "Call Type:        " << spec.m_CallType << std::endl;
        for ( FunctionSpec::ParamVec::size_type i = 0; i < spec.m_ParamTypes.size() ; ++i ){
            std::cout << "Parameter:    " << spec.m_ParamTypes[i] << "'\n";
        }
        if ( !r.full ){
            BOOST_FAIL("Parsing was not successful");
        }

        // Check that the parsed name and parameter types match the expected values
        BOOST_CHECK_EQUAL(spec.m_Name, expectedName);
//        BOOST_CHECK_EQUAL_COLLECTIONS(spec.m_ParamTypes.begin(), spec.m_ParamTypes.end(),
//                                      expectedParamTypes.begin(), expectedParamTypes.end());
    }

    BOOST_AUTO_TEST_CASE(ParseClassReturnType) {
        // Create an instance of the SignatureParser
        SignatureParser parser;
        FunctionSpec spec;
        spec.m_SerialID = 0;
        spec.m_dwAddress = 0;

        // Define the input string and expected output
        std::string input = "public: class Foo::Bar::Baz __thiscall Foo::Bar::Baz::bar(__int64,double)";
        std::string expectedName = "bar";
        std::vector<std::string> expectedParamTypes = {"__int64"};

        try {
            parse_info<> r = parser.Parse(input.c_str(), spec);
            std::cout << "Function ID:      " << spec.m_SerialID << std::endl;
            std::cout << "Function Address: " << spec.m_dwAddress << std::endl;
            std::cout << "Function name:    " << spec.m_Name << std::endl;
            std::cout << "Return Type:      " << spec.m_ReturnType << std::endl;
            std::cout << "Call Type:        " << spec.m_CallType << std::endl;
            for ( FunctionSpec::ParamVec::size_type i = 0; i < spec.m_ParamTypes.size() ; ++i ){
                std::cout << "Parameter:    " << spec.m_ParamTypes[i] << "'\n";
            }
            if ( !r.full ){
                std::string errorInput = input.substr(0, r.stop - input.c_str());
                //std::cout << "Parse error at position " << r.stop << ": " << errorInput << std::endl;
                std::cout << "Parse error at position " << r.stop << std::endl;
                BOOST_FAIL("Parsing was not successful");
            }
        }  catch (const std::invalid_argument& e) {
            e.what();
        }
    }


    // Define a test case for parsing an invalid function signature
    BOOST_AUTO_TEST_CASE(ParseInvalidSignature) {
    }

// End the test suite
BOOST_AUTO_TEST_SUITE_END()
