#include "stdafx.h"

#include "CallContract.h"
#include "InvocationEngine.h"

#include <fstream>
#include <iostream>
#include <sstream>

// The worker is deliberately a separate executable. It accepts one bounded
// request file and writes one structured result file, so a supervisor can
// terminate this process without leaving target code in its own address space.
int main(int argc, char* argv[])
{
    if (argc != 4) return 2;
    std::ifstream input(argv[2], std::ios::binary | std::ios::ate);
    if (!input) return 3;
    const std::streamoff size=input.tellg();
    if (size < 0 || size > 4 * 1024 * 1024) return 3;
    std::string document(static_cast<size_t>(size), '\0'); input.seekg(0); if (!document.empty()) input.read(&document[0], static_cast<std::streamsize>(document.size()));
    CallRequest request; std::vector<CallDiagnostic> diagnostics;
    CallResult result; result.status="validation-failed";
    if (!ParseCallRequestJson(document, request, diagnostics)) { result.correlationId=request.correlationId; result.diagnostics=diagnostics; }
    else
    {
        FunctionCatalog catalog; std::string error;
        if (!FunctionCatalog::Load(argv[1], catalog, error) || !ValidateCallRequest(request, catalog, diagnostics)) { result.correlationId=request.correlationId; result.resolvedModule=catalog.Module(); result.diagnostics=diagnostics; if(!error.empty()) result.diagnostics.push_back({"worker-validation-failed","call",error}); }
        else if (!InvokeX64Export(argv[1], request, catalog, result, error) && !error.empty()) result.diagnostics.push_back({"worker-failed","call",error});
    }
    std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
    if (!output) return 4;
    WriteCallResultJson(output, result);
    return result.success ? 0 : 1;
}
