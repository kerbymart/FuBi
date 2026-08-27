extern "C" int NamedExport()
{
    return 42;
}

__declspec(dllexport) int AddNumbers(int left, int right)
{
    return left + right;
}
