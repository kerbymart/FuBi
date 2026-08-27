#pragma once

#include "CallContract.h"

#include <string>

// Selects and validates the worker executable before any runtime target load.
// The target architecture must be either x64 or x86; the selected executable
// must exist and report the same PE bitness.
bool SelectInvocationWorker(const std::string& targetArchitecture,
    std::string& workerPath, std::string& error);

bool InvokeX64ExportProcess(const std::string& imagePath, const CallRequest& request,
    const FunctionCatalog& catalog, CallResult& result, std::string& error,
    bool allowPointerResults = false);
