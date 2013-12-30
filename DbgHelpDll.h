#pragma once

class DbgHelpDll
{
public:
	DbgHelpDll(void);
	bool Load(void);
public:
	~DbgHelpDll(void);
public:
	DWORD UnDecorateSymbolName(const char* DecoratedName, char* UnDecoratedName, DWORD UndecoratedLength, DWORD Flags);
};
