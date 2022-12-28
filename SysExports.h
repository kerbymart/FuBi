/**
 * @file SysExports.h
 * @brief Header file for the SysExports class, which handles the import
 *        and management of functions in a dynamic-link library (DLL).
 * @author Kerby
 * @date 2022-12-20
 */

#pragma once

#include <boost/spirit/include/classic_core.hpp>
#include <boost/spirit/include/classic_parse_tree.hpp>
#include <boost/spirit/include/classic_attribute.hpp>
#include <boost/spirit/include/classic_push_back_actor.hpp>
#include <boost/spirit/include/classic_symbols.hpp>
#include <boost/variant.hpp>

#include <iostream>
#include <vector>
#include <map>
#include <unordered_map>
#include <string>

using namespace boost::spirit::classic;
using namespace phoenix;

/**
 * @brief A struct to store information about a single exported function.
 * @var m_SerialID The serial id based on the EXE import order.
 * @var m_Name The name of the function.
 * @var m_dwAddress The address of the function.
 * @var m_ReturnType The return type of the function.
 * @var m_CallType The calling convention of the function.
 * @var m_ParamTypes The types of the function's parameters.
 */
struct FunctionSpec
{
    // simple variable spec
    enum eVarType
    {
        VAR_VOID, VAR_BOOL, VAR_INT,
        VAR_FLOAT, VAR_STRING, VAR_USER, VAR_UNKNOWN,
    };
    // possible calling conventions
    enum eCallType
    {
        CALL_CDECL, CALL_STDCALL, CALL_THISCALL,
        CALL_THISCALLVARARG, CALL_UNKNOWN
    };

	typedef std::vector <eVarType> ParamVec;
	std::vector<std::string> params; // temporary parameters vector

    DWORD m_SerialID;				// Serial id based on EXE import order
    std::string  m_Name;			// Function name
    DWORD		 m_dwAddress;		// Function address
    eVarType     m_ReturnType;		// Return Type
    eCallType    m_CallType;		// Call Type
    ParamVec     m_ParamTypes;		// Parameter type(s)
};
typedef std::vector<std::map<DWORD, FunctionSpec>>FunctionByAddrMap;

/**
 *  @brief A class that parses a function signature string and stores
 *  the resulting information in a FunctionSpec struct.
 */
class SignatureParser
{
public:
	parse_info<> eParse; // flag
	/*
	 * @brief Parses a function signature string and stores the resulting information in a FunctionSpec struct.
	 * @param str The function signature string to parse.
	 * @param spec  The FunctionSpec struct to store the parsed information in.
	 * @return The parse_info object containing information about the success or failure of the parse operation.
	 */
	parse_info<> Parse(char const* str, FunctionSpec& spec)
	{	
		symbols<> base_types, call_types, access_types;
		access_types = "public:","private","protected";
		base_types = "void","bool","int","char","long","short","double";
		call_types = "__cdecl","__stdcall","__thiscall","__fastcall";

        std::string callTypeString;
        std::string returnTypeString;
        std::vector <std::string> paramTypes;

        std::vector<std::string> v;
			// Grammar rules
			rule<> 
				rettype, datatype, calltype, params, group, function;
			rule<>
				rettype_signed, rettype_unsigned;
			rule<>
				top;
			// Begin Grammar
				top 
					= ( (access_types >> *space_p 
							>> rettype >> *space_p 
								>> calltype >> *space_p 
									>> function >> group ) | 
						 (rettype >> *space_p 
							 >> calltype >> *space_p 
								 >> function >> group )
					   );
				group 
					= '(' >> params >> ')';

				calltype
					= lexeme_d[
								str_p("__")
									>> *(alnum_p)][assign_a(callTypeString)
							  ];
				rettype
					=  ( rettype_unsigned | rettype_signed )[assign_a(returnTypeString)];

				rettype_signed 
					= lexeme_d[
								 ( base_types >> space_p >> ( ( str_p("const") >> space_p >> ch_p('*') ) | 
									( str_p("const") | ch_p('*') ) ) |
									( base_types )
								 )	
							  ];
				rettype_unsigned
					= lexeme_d[
								str_p("unsigned") >> space_p >> rettype_signed
							  ];
				datatype 
					= lexeme_d[
								(alpha_p) 
									>> *(alnum_p | space_p | ch_p('*'))
							  ][push_back_a(paramTypes)];
				function 
					= ( lexeme_d[
								(alpha_p | ch_p('_')) 
									>> *(alnum_p | ch_p('_')) >> str_p("::") 
									>> ((alpha_p | ch_p('_')) >> *(alnum_p | ch_p('_')))[assign_a(spec.m_Name)]
							  ] |
		
					    lexeme_d[
								(alpha_p | ch_p('_')) 
									>> *(alnum_p | ch_p('_') /* | ch_p(':')temporary*/)
							  ][assign_a(spec.m_Name)]
					   );

				params 
					= *( datatype % ch_p(',') );		

		parse_info<> r = parse(str,
			(
				top
			));

        std::map<std::string, FunctionSpec::eCallType> callTypeMap = {
                {"__cdecl", FunctionSpec::CALL_CDECL},
                {"__stdcall", FunctionSpec::CALL_STDCALL},
                {"__thiscall", FunctionSpec::CALL_THISCALL},
                {"__fastcall", FunctionSpec::CALL_THISCALLVARARG}
        };

        std::unordered_map<std::string, FunctionSpec::eVarType> varTypeMap = {
                {"void", FunctionSpec::VAR_VOID},
                {"bool", FunctionSpec::VAR_BOOL},
                {"int", FunctionSpec::VAR_INT},
                {"float", FunctionSpec::VAR_FLOAT},
                {"string", FunctionSpec::VAR_STRING},
                {"user", FunctionSpec::VAR_USER},
                {"unknown", FunctionSpec::VAR_UNKNOWN}
        };

        spec.m_CallType = callTypeMap[callTypeString];
        spec.m_ReturnType = varTypeMap[returnTypeString];
        for (const auto& paramType : paramTypes) {
            spec.m_ParamTypes.push_back(varTypeMap[paramType]);
        }
		return r;
	}
};

typedef std::vector<FunctionSpec> functionVec;

class SysExports
{
public:
	SysExports(void);
public:
	~SysExports(void);
public:
	bool ImportBindings(HMODULE module = NULL);
public:
	bool ImportFunction(const char* mangledName, DWORD address, SignatureParser* parser);
	functionVec m_Functions; /* temporary */
	void PrintFunctionInfo();
};
