#include <cstdio>

extern "C" __declspec(dllexport) int EmitStdout()
{
    std::printf("target-output-must-not-reach-protocol\\n");
    std::fflush(stdout);
    return 17;
}
