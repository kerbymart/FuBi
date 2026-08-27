#pragma once

#include "CallContract.h"

#include <string>

bool InvokeX64ExportProcess(const std::string& imagePath, const CallRequest& request,
    const FunctionCatalog& catalog, CallResult& result, std::string& error);
