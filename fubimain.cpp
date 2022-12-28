/**
* @file fubimain.cpp
* @brief Defines the entry point for the console application.
*      This file contains the main function of the application, which loads a DLL, instantiates a Fubi object,
*      and calls functions from the DLL using the Fubi object's function table.
* @author Kerby
* @date 2022-12-20
*/

#include "stdafx.h"
#include <iostream>
#include "Fubi.h"

using namespace std;

/**
 * @brief The main entry point of the console application. This function loads the specified DLL,
 *        instantiates a Fubi object, and calls functions from the DLL using the Fubi object's function table.
 * @param argc The number of arguments passed to the application.
 * @param argv An array of arguments passed to the application.
 * @return Returns 0 on successful execution, or -1 if the DLL could not be loaded.
 */
int _tmain(int argc, _TCHAR* argv[])
{
	HINSTANCE hDLL;
	hDLL = LoadLibrary(argv[1]);

	if ( !hDLL ){
		MessageBox(NULL, _T("Unable to load dll."), _T("Fatal Error"), MB_ICONERROR);
		return -1;
	}

	// Instantiate the Fubi object
	Fubi fubi;

	// Bind with the DLL
    fubi.sys.ImportBindings(hDLL);
	fubi.sys.PrintFunctionInfo();

    // Prompt the user to input the name of the function they want to call
    cout << "Please enter the name of the function you want to call:\n";
	
	string str;
	string fn_name; // function name
	vector<DWORD> params; // parameter stack

    Result result;
    while ( getline(cin, str) )
    {
        if (str.empty() || str[0] == 'q' || str[0] == 'Q')
            break;
        cout << "\n";
        result = fubi.Call_function(str, NULL);
        auto res_type = result.res_type;
        auto res = result.res;
        cout << "RESULT (" << res_type << ")" << " = " << res << "\n";
    }

	system("PAUSE");
	return 0;
}