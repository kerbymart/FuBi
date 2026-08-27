#pragma once

#include "CallContract.h"

#include <string>

// Performs the only target-loading operation in this milestone. All request
// validation and static identity checks must succeed before this function.
bool InvokeX64Export(const std::string& imagePath, const CallRequest& request,
    const FunctionCatalog& catalog, CallResult& result, std::string& error);
