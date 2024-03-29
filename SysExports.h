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

	typedef std::vector <std::string> ParamVec;
	std::vector<std::string> params; // temporary parameters vector

    DWORD m_SerialID;				// Serial id based on EXE import order
    std::string  m_Name;			// Function name
    DWORD		 m_dwAddress;		// Function address
    std::string  m_ReturnType;		// Return Type
    std::string  m_CallType;		// Call Type
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
		base_types = "void","bool","int","char","long","short","double",
                     "float", "wchar_t", "char16_t", "char32_t",
                     "__int8", "__int16", "__int32","__int64", "long long";
		call_types = "__cdecl","__stdcall","__thiscall","__fastcall";

        std::string callTypeString;
        std::string returnTypeString;
        std::string returnTypeClass;
        std::vector <std::string> paramTypes;

        std::vector<std::string> v;
			// Grammar rules
			rule<> 
				rettype, datatype, calltype, params, group, function, class_name;
			rule<>
				rettype_signed, rettype_unsigned, rettype_class;
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
                        =  ( rettype_unsigned | rettype_signed | rettype_class )[assign_a(returnTypeString)]
                           | eps_p[assign_a(returnTypeString, "")];

                rettype_signed
					= lexeme_d[
								 ( base_types >> space_p >> ( ( str_p("const") >> space_p >> ch_p('*') ) | 
									( str_p("const") | ch_p('*') ) ) |
									( base_types )
								 )	
							  ];

				rettype_unsigned
					= lexeme_d[
                                ((str_p("unsigned") | str_p("virtual") | str_p("static"))
								>> space_p) >> rettype_signed
							  ];

                rettype_class = lexeme_d[
                                            (str_p("class") | str_p("struct"))
                                            >> space_p >> class_name
                                        ][assign_a(returnTypeClass)];

                /**
                 * This grammar will match class names such as Foo, Bar_1, and Baz::Quux_2.
                 * It will also match compound class names like Foo::Bar::Baz::Quux.
                 * It will not match class names that start with a number, such as 1Foo.
                 */
                class_name =
                        (alpha_p | ch_p('_'))
                        >> *(alnum_p | ch_p('_'))
                        >> *(str_p("::")
                        >> (alpha_p | ch_p('_'))
                        >> *(alnum_p | ch_p('_')));

                /**
                 * This grammar will match and extract data types such as
                 * "int", "char*", "const char*", and "__int64", etc.
                 */
                datatype
                    = lexeme_d[
                        // match either "__" or any alphabetic character
                        (str_p("__") | alpha_p)

                        // match any number of characters that are either alphanumeric, a space, or an asterisk
                        // or match "const" followed by a space and an asterisk
                        >> *(alnum_p | space_p | ch_p('*') | (str_p("const") >> space_p >> ch_p('*')))

                        // apply the semantic action "push_back_a" to the parameter "paramTypes"
                        ][push_back_a(paramTypes)];

				function
					= ( lexeme_d[
					        (alpha_p | ch_p('_'))
					            >> *(alnum_p | ch_p('_'))
					            >> str_p("::")
					            >> *((alpha_p | ch_p('_'))
					            >> *(alnum_p | ch_p('_'))
					            >> str_p("::"))
					            >> ((alpha_p | ch_p('_'))
					            >> *(alnum_p | ch_p('_')))[assign_a(spec.m_Name)]
                                ]
					   );

				params 
					= *( datatype % ch_p(',') );		

		parse_info<> r = parse(str,
			(
				top
			));

        spec.m_CallType = callTypeString;
        // Hack to set the return type, since the `rettype` grammar rule wont spit out `rettype_class`
        spec.m_ReturnType = returnTypeClass.empty() ? returnTypeString : returnTypeClass;
        for (const auto& paramType : paramTypes) {
            spec.m_ParamTypes.push_back(paramType);
        }
		return r;
	}
private:
    static void debug(const std::string& message)
    {
        std::cout << "DEBUG: " << message << std::endl;
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
