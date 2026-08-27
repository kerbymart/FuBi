#pragma once

namespace FubiExitCode
{
constexpr int Success = 0;
constexpr int Usage = 2;
constexpr int CatalogLoadFailed = 3;
constexpr int ProfileLoadFailed = 6;
constexpr int SymbolLoadFailed = 7;
constexpr int ValidationFailed = 8;
constexpr int InvocationFailed = 9;
constexpr int SelectorNotFound = 4;
constexpr int SelectorAmbiguous = 5;
}
