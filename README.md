# Fubi

Fubi is a command-line utility for calling functions from a dynamic-link library (DLL). It loads a DLL, instantiates a Fubi object, and calls functions from the DLL using the Fubi object's function table.

## Project Details

Programming Language: C++<br>
Dependencies: Boost library, DbgHelp library<br>
Compiler: Visual Studio<br>

## Architecture
The Fubi project consists of the following files:<br>
**Fubi.h**: Header file for the Fubi class, which contains the function table and methods for calling functions from a DLL.<br>
**Fubi.cpp**: Implementation file for the Fubi class.<br>
**SysExports.h**: Header file for the SysExports class, which handles the import and management of functions in a DLL.<br>
**SysExports.cpp**: Implementation file for the SysExports class.<br>
**SignatureParser.h**: Header file for the SignatureParser class, which parses function signatures using the Boost Spirit library.<br>
**SignatureParser.cpp**: Implementation file for the SignatureParser class.<br>
**DbgHelpDll.h**: Header file for the DbgHelpDll class, which loads the DbgHelp library and provides methods for unmangling function signatures.<br>
**DbgHelpDll.cpp**: Implementation file for the DbgHelpDll class.<br>
**stdafx.h**: Standard include file for precompiled headers.<br>
**stdafx.cpp**: Source file that includes just the standard includes and generates the precompiled header.<br>
**fubimain.cpp**: Main entry point for the Fubi application.<br>
**CMakeLists.txt**: CMake build configuration file for this project. Contains the necessary commands and settings for building the project using CMake.<br>

## Building the Project
To build the Fubi project, you will need to have CMake installed. Then, follow these steps:

- Open a command prompt and navigate to the directory where the Fubi source files are located.
- Run the cmake command to generate the necessary build files for your platform, specifying the path to the source directory as the argument:

```
$ mkdir build
$ cd build
$ cmake -G "Visual Studio 16 2019" ..
$ cmake --build .
```

## Running Fubi
To run fubi.exe and load a DLL, open a terminal window and navigate to the directory where the executable is located. Then run the following command, replacing "path/to/dll" with the path to the DLL you want to load:
```
$ fubi.exe path/to/dll
```

