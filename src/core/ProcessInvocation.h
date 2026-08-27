#pragma once

#include "CallContract.h"

#include <string>

// This value is produced only after request validation.  Adapters receive it
// by const reference so they cannot observe or mutate CLI/parser state.
struct NormalizedCall
{
    CallRequest request;
    ModuleIdentity module;
    PrototypeSpec prototype;
};

class InvocationAdapter
{
public:
    virtual ~InvocationAdapter() = default;
    virtual bool Invoke(const NormalizedCall& call, CallResult& result,
        std::string& error) = 0;
};

// Validates and freezes the command boundary before an adapter is reached.
bool NormalizeCall(const CallRequest& request, const FunctionCatalog& catalog,
    NormalizedCall& call, std::vector<CallDiagnostic>& diagnostics);

// Dispatches a call through an injected adapter. Invalid requests never reach
// InvocationAdapter::Invoke.
bool DispatchCall(const CallRequest& request, const FunctionCatalog& catalog,
    InvocationAdapter& adapter, CallResult& result, std::string& error);

// Selects and validates the worker executable before any runtime target load.
// The target architecture must be either x64 or x86; the selected executable
// must exist and report the same PE bitness.
bool SelectInvocationWorker(const std::string& targetArchitecture,
    std::string& workerPath, std::string& error);

bool InvokeX64ExportProcess(const std::string& imagePath, const CallRequest& request,
    const FunctionCatalog& catalog, CallResult& result, std::string& error,
    bool allowPointerResults = false);

class WorkerInvocationAdapter final : public InvocationAdapter
{
public:
    WorkerInvocationAdapter(const std::string& imagePath,
        const FunctionCatalog& catalog, bool allowPointerResults = false)
        : imagePath_(imagePath), catalog_(catalog),
          allowPointerResults_(allowPointerResults) {}

    bool Invoke(const NormalizedCall& call, CallResult& result,
        std::string& error) override;

private:
    const std::string& imagePath_;
    const FunctionCatalog& catalog_;
    bool allowPointerResults_;
};

// Owns one isolated worker for the lifetime of a JSONL session. The worker
// keeps the target module loaded and owns all reference payloads, so an
// opaque reference can never cross a process or architecture boundary.
class PersistentWorkerSession final
{
public:
    PersistentWorkerSession(const std::string& imagePath,
        const FunctionCatalog& catalog);
    ~PersistentWorkerSession();

    PersistentWorkerSession(const PersistentWorkerSession&) = delete;
    PersistentWorkerSession& operator=(const PersistentWorkerSession&) = delete;

    bool Start(std::string& error);
    bool Invoke(const CallRequest& request, CallResult& result,
        std::string& error);
    void Stop();
    bool IsRunning() const;

private:
    std::string imagePath_;
    const FunctionCatalog& catalog_;
    void* process_ = nullptr;
    void* thread_ = nullptr;
    void* input_ = nullptr;
    void* output_ = nullptr;
};
