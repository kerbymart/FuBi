// fubi.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <iostream>
#include <dbghelp.h>
#include "Fubi.h"

using namespace std;

class Foo {
public: 
	static const DWORD bar = 0x1234;
	string mystring;
	__declspec(dllexport) void foo();
	void foobar();
};

void Foo::foo(){
	printf("foo called");
}

void Foo::foobar(){
	printf("foobar called");
}

int _tmain(int argc, _TCHAR* argv[])
{
	Foo foo;
	foo.mystring =  "all your base are belong to us";

	HINSTANCE hDLL;
 	LPCWSTR  thedll = L"Test.dll";
	hDLL = LoadLibrary(argv[1]);

	DWORD testvar = 0x1;

	if ( !hDLL ){
		MessageBox(NULL, _T("Unable to load dll."), _T("Fatal Error"), MB_ICONERROR);
		return -1;
	}
	cout <<"DLL loaded successfully...\n";

	// Instantiate the Fubi object
	Fubi fubi;
	// Bind with the DLL
	fubi.sys.ImportBindigs( hDLL );
	fubi.sys.PrintFunctionInfo();

	////////////////////////////////////////////////////////////////////
	// Sample call to function using the function table
	// Requirements : 
	// -> DWORD address
	// -> args
	// -> arg_size 
	////////////////////////////////////////////////////////////////////
	cout << "********************************************************\n";
	cout << "Please Type the name of the function you want to call...\n";
	cout << "********************************************************\n";
	
	string str;
	string fn_name; // function name
	vector<DWORD> params; // parameter stack

	DWORD res=0;
	
	string res_type("ToDo");
	while ( getline(cin, str) )
		{
		    if (str.empty() || str[0] == 'q' || str[0] == 'Q')
				break;		
			cout << "\n";
			res = fubi.Call_function(str, NULL);
			cout << "RESULT (" << res_type << ")" << " = " << res << "\n";
		}

	system("PAUSE");
	return 0;
}