//////////////////////////////////////////////////////////////////////////////
//
// File     :  SysExports.h
// Author(s):  Kerby Martino
//
// Summary  :  FuBi manager class
//
// Copyright © 2002 Kerby Martino  All rights reserved.
//----------------------------------------------------------------------------
//  $Revision:: $              $Date:$
//----------------------------------------------------------------------------
//////////////////////////////////////////////////////////////////////////////
#pragma once
///////////////////////////////////////////////////////////////////////////////
#include <boost/spirit/core.hpp>
#include <boost/spirit.hpp> 
#include <boost/spirit/attribute.hpp>
#include <boost/spirit/actor/push_back_actor.hpp>
#include <boost/spirit/symbols.hpp>
#include <iostream>
#include <vector>
#include <map>
#include <string>
///////////////////////////////////////////////////////////////////////////////
using namespace boost::spirit;
using namespace phoenix;
///////////////////////////////////////////////////////////////////////////////
struct FunctionSpec
{
    // simple variable spec
    enum eVarType
    {
        VAR_VOID, VAR_BOOL, VAR_INT,
        VAR_FLOAT, VAR_STRING, VAR_USER,VAR_UNKNOWN,
    };
    // possible calling conventions
    enum eCallType
    {
        CALL_CDECL, CALL_STDCALL, CALL_THISCALL,
        CALL_THISCALLVARARG,
    };
	typedef std::vector </*eVarType*/std::string> ParamVec;
	std::vector<std::string> params; // temporary parameters vector

	DWORD m_SerialID;				// Serial id based on EXE import order
    std::string  m_Name;			// Function name
	DWORD		 m_dwAddress;		// Function address
	std::string /*eVarType*/     m_ReturnType;		// Return Type
    std::string /*eCallType*/    m_CallType;		// Call Type
    ParamVec     m_ParamTypes;		// Parameter type(s)
};
typedef std::vector<std::map<DWORD, FunctionSpec>>FunctionByAddrMap;

class SignatureParser
{
public:
	parse_info<> eParse; // flag
	parse_info<> Parse(char const* str, FunctionSpec& spec)
	{	
		symbols<> base_types, call_types, access_types;
		access_types = "public:","private","protected";
		base_types = "void","bool","int","char","long","short","double";
		call_types = "__cdecl","__stdcall","__thiscall","__fastcall";
		
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
									>> *(alnum_p)][assign_a(spec.m_CallType)
							  ];
				rettype 
					=  ( rettype_unsigned | rettype_signed )[assign_a(spec.m_ReturnType)];

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
							  ][push_back_a(spec.m_ParamTypes)];
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
	bool ImportBindigs(HMODULE module = NULL);
public:
	bool ImportFunction(const char* mangledName, DWORD address, SignatureParser* parser);
	functionVec m_Functions; /* temporaty */
	void PrintFunctionInfo();
};
