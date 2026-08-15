// SPDX-License-Identifier: GPL-3.0-or-later
// Linux behavioural probe for the framework-free DPF CrashLog.

#include "../CrashLog.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
static_assert(std::atomic<int>::is_always_lock_free,
              "the probe's signal counter must be lock-free");
std::atomic<int> gHostCalls { 0 };
volatile sig_atomic_t gUpperCalls = 0;
volatile sig_atomic_t gReturningCalls = 0;
volatile sig_atomic_t gJumped = 0;
sigjmp_buf gJumpBuffer;
struct sigaction gUpperPrevious {};

void hostHandler(int) noexcept
{
    gHostCalls.fetch_add(1, std::memory_order_relaxed);
}

void hostExit42(int) noexcept
{
    ::_exit(42);
}

void returningHandler(int) noexcept
{
    ++gReturningCalls;
}

void jumpHandler(int) noexcept
{
    gHostCalls.fetch_add(1, std::memory_order_relaxed);
    if (gJumped == 0)
    {
        gJumped = 1;
        ::siglongjmp(gJumpBuffer, 1);
    }
}

void invokeAction(const struct sigaction& action, int sig,
                  siginfo_t* info, void* context) noexcept
{
    if ((action.sa_flags & SA_SIGINFO) != 0)
    {
        if (action.sa_sigaction != nullptr)
            action.sa_sigaction(sig, info, context);
    }
    else if (action.sa_handler != nullptr
             && action.sa_handler != SIG_DFL
             && action.sa_handler != SIG_IGN)
    {
        action.sa_handler(sig);
    }
}

void upperHandler(int sig, siginfo_t* info, void* context) noexcept
{
    ++gUpperCalls;
    invokeAction(gUpperPrevious, sig, info, context);
}

void setSimpleHandler(int sig, void (*handler)(int))
{
    struct sigaction action {};
    action.sa_handler = handler;
    ::sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    if (::sigaction(sig, &action, nullptr) != 0)
    {
        std::perror("sigaction");
        std::exit(2);
    }
}

void setSiginfoHandler(int sig,
                       void (*handler)(int, siginfo_t*, void*),
                       struct sigaction* previous = nullptr)
{
    struct sigaction action {};
    action.sa_sigaction = handler;
    ::sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    if (::sigaction(sig, &action, previous) != 0)
    {
        std::perror("sigaction");
        std::exit(2);
    }
}

struct sigaction currentAction(int sig)
{
    struct sigaction action {};
    if (::sigaction(sig, nullptr, &action) != 0)
    {
        std::perror("sigaction query");
        std::exit(2);
    }
    return action;
}

bool isCrashHandler(const struct sigaction& action)
{
    return (action.sa_flags & SA_SIGINFO) != 0
        && action.sa_sigaction == &DuskCrashLog::detail::crashHandler;
}

bool sameSimpleHandler(const struct sigaction& action, void (*handler)(int))
{
    return (action.sa_flags & SA_SIGINFO) == 0 && action.sa_handler == handler;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

std::size_t countText(const std::string& haystack, const std::string& needle)
{
    std::size_t count = 0;
    std::size_t at = 0;
    while ((at = haystack.find(needle, at)) != std::string::npos)
    {
        ++count;
        at += needle.size();
    }
    return count;
}

struct ChildResult
{
    bool timedOut = false;
    int status = 0;
};

ChildResult waitForChild(pid_t child, int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeoutMs);
    int status = 0;
    for (;;)
    {
        const pid_t waited = ::waitpid(child, &status, WNOHANG);
        if (waited == child)
            return { false, status };
        if (waited < 0)
        {
            std::perror("waitpid");
            return { false, -1 };
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            ::kill(child, SIGKILL);
            (void) ::waitpid(child, &status, 0);
            return { true, status };
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

bool expect(bool condition, const std::string& message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

using ModuleFunction = void (*)();

struct Module
{
    void* handle = nullptr;
    ModuleFunction install = nullptr;
    ModuleFunction uninstall = nullptr;

    bool open(const std::string& path)
    {
        handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr)
        {
            std::cerr << "dlopen failed: " << ::dlerror() << '\n';
            return false;
        }
        install = reinterpret_cast<ModuleFunction>(::dlsym(handle, "crashlog_module_install"));
        uninstall = reinterpret_cast<ModuleFunction>(::dlsym(handle, "crashlog_module_uninstall"));
        return expect(install != nullptr && uninstall != nullptr, "module exports are present");
    }

    void close()
    {
        if (handle != nullptr)
        {
            (void) ::dlclose(handle);
            handle = nullptr;
        }
    }
};

std::size_t mappedLines(const std::string& modulePath)
{
    std::ifstream maps("/proc/self/maps");
    std::string line;
    std::size_t count = 0;
    const std::string canonical = std::filesystem::weakly_canonical(modulePath).string();
    while (std::getline(maps, line))
        if (line.find(canonical) != std::string::npos)
            ++count;
    return count;
}

const char* modulePath(const char* name)
{
    const char* const value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        std::cerr << "missing environment variable " << name << '\n';
        std::exit(2);
    }
    return value;
}

bool testHostHandlerStillRuns(bool mutant)
{
    const pid_t child = ::fork();
    if (child == 0)
    {
        gHostCalls = 0;
        setSimpleHandler(SIGABRT, &hostHandler);
        DuskCrashLog::install("host-chain", "1");
        if (mutant)
            DuskCrashLog::detail::previousActions()[DuskCrashLog::detail::signalSlot(SIGABRT)].sa_handler = SIG_DFL;
        ::raise(SIGABRT);
        DuskCrashLog::uninstall("host-chain", "1");
        ::_exit(gHostCalls.load() == 1 ? 0 : 96);
    }
    if (child < 0)
        return expect(false, "host-handler probe forked");

    const ChildResult result = waitForChild(child, 2000);
    bool ok = expect(!result.timedOut, "host-handler probe completed");
    if (!result.timedOut && WIFSIGNALED(result.status))
        ok = expect(false, "a broken chain terminated at the default signal disposition") && ok;
    else if (!result.timedOut && WIFEXITED(result.status) && WEXITSTATUS(result.status) == 96)
        ok = expect(false, "the pre-existing host handler ran zero or multiple times") && ok;
    else if (!result.timedOut)
        ok = expect(WIFEXITED(result.status) && WEXITSTATUS(result.status) == 0,
                    "host-handler child returned its success status") && ok;
    ok = expect(readFile(DuskCrashLog::getLogFile()).find("host-chain v1") != std::string::npos,
                "the Dusk handler logged before chaining") && ok;
    return ok;
}

bool testNoSigkill(bool mutant)
{
    const pid_t child = ::fork();
    if (child == 0)
    {
        setSimpleHandler(SIGSEGV, &hostExit42);
        DuskCrashLog::install("no-sigkill", "1");
        if (mutant)
            ::kill(::getpid(), SIGKILL);
        ::raise(SIGSEGV);
        ::_exit(90);
    }
    const ChildResult result = waitForChild(child, 2000);
    return expect(!result.timedOut, "fault handling completed")
        && expect(WIFEXITED(result.status) && WEXITSTATUS(result.status) == 42,
                  "the host handler, not SIGKILL, chose the process exit");
}

bool testUninstallRestores(bool mutant)
{
    setSimpleHandler(SIGABRT, &hostHandler);
    DuskCrashLog::install("restore", "1");
    if (!mutant)
        DuskCrashLog::uninstall("restore", "1");
    const struct sigaction action = currentAction(SIGABRT);
    return expect(sameSimpleHandler(action, &hostHandler),
                  "last uninstall restored the previous disposition");
}

bool testChained(bool mutant)
{
    gHostCalls = 0;
    setSimpleHandler(SIGABRT, &hostHandler);
    Module first;
    Module second;
    if (!first.open(modulePath("CRASHLOG_TEST_MODULE_A"))
        || !second.open(modulePath("CRASHLOG_TEST_MODULE_B")))
        return false;
    if (!mutant)
        first.install();
    second.install();
    ::raise(SIGABRT);
    const std::string log = readFile(DuskCrashLog::getLogFile());
    bool ok = expect(gHostCalls.load() == 1, "the host terminus ran after the DSO chain")
           && expect(countText(log, "Module A v1.0.0") == 1,
                     "the first plugin binary logged exactly once")
           && expect(countText(log, "Module B v1.0.0") == 1,
                     "the second plugin binary logged exactly once");
    second.uninstall();
    if (!mutant)
        first.uninstall();
    second.close();
    first.close();
    return ok;
}

bool testRefcount(bool mutant)
{
    setSimpleHandler(SIGABRT, &hostHandler);
    DuskCrashLog::install("refcount", "1");
    if (!mutant)
        DuskCrashLog::install("refcount", "1");
    DuskCrashLog::uninstall("refcount", "1");
    bool ok = expect(DuskCrashLog::detail::installCount() == 1,
                     "one registration remains after the first uninstall")
           && expect(isCrashHandler(currentAction(SIGABRT)),
                     "the handler stays armed while one registration remains");
    if (!mutant)
        DuskCrashLog::uninstall("refcount", "1");
    return ok;
}

bool testReentrant(bool mutant)
{
    const pid_t child = ::fork();
    if (child == 0)
    {
        setSimpleHandler(SIGABRT, &hostExit42);
        DuskCrashLog::install("reentrant", "1");
        const std::uintptr_t me = DuskCrashLog::detail::currentThreadId();
        if (DuskCrashLog::detail::enterHandler(me)
            != DuskCrashLog::detail::HandlerEntry::Acquired)
            ::_exit(91);
        if (mutant)
            DuskCrashLog::detail::leaveHandler(me);
        ::raise(SIGABRT);
        ::_exit(92);
    }
    const ChildResult result = waitForChild(child, 2000);
    return expect(!result.timedOut, "recursive entry terminated promptly")
        && expect(WIFSIGNALED(result.status) && WTERMSIG(result.status) == SIGABRT,
                  "same-thread recursion restored the default instead of re-entering the host");
}

bool testSigIgnResumable(bool mutant)
{
    const pid_t child = ::fork();
    if (child == 0)
    {
        setSimpleHandler(SIGABRT, SIG_IGN);
        DuskCrashLog::install("sigign-resumable", "1");
        if (mutant)
            DuskCrashLog::detail::previousActions()[DuskCrashLog::detail::signalSlot(SIGABRT)].sa_handler = SIG_DFL;
        ::raise(SIGABRT);
        DuskCrashLog::uninstall("sigign-resumable", "1");
        ::_exit(0);
    }
    const ChildResult result = waitForChild(child, 2000);
    return expect(!result.timedOut, "ignored resumable signal returned promptly")
        && expect(WIFEXITED(result.status) && WEXITSTATUS(result.status) == 0,
                  "SIG_IGN was honoured for resumable SIGABRT");
}

bool testSigIgnBoundedNoSpin(bool mutant)
{
    const pid_t child = ::fork();
    if (child == 0)
    {
        setSimpleHandler(SIGSEGV, SIG_IGN);
        if (mutant)
            setSimpleHandler(SIGSEGV, &returningHandler);
        else
            DuskCrashLog::install("sigign-fault", "1");
        volatile int* const bad = reinterpret_cast<volatile int*>(0);
        *bad = 7;
        ::_exit(93);
    }
    const auto started = std::chrono::steady_clock::now();
    const ChildResult result = waitForChild(child, 1500);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    std::error_code ec;
    const std::uintmax_t bytes = std::filesystem::file_size(DuskCrashLog::getLogFile(), ec);
    const std::string log = readFile(DuskCrashLog::getLogFile());
    std::cout << "MEASURE\tsig-ign-bounded-no-spin\telapsed-ms=" << elapsedMs
              << "\tlog-bytes=" << (ec ? 0 : bytes)
              << "\trecords=" << countText(log, "Dusk Audio crash log") << '\n';
    DuskCrashLog::detail::recordsWritten().store(
        DuskCrashLog::detail::kMaxRecordsPerProcess, std::memory_order_relaxed);
    DuskCrashLog::detail::suppressionNoticeWritten().store(false, std::memory_order_relaxed);
    const bool noticePending = DuskCrashLog::detail::reportWritePending();
    const bool noticeClaimed = DuskCrashLog::detail::claimSuppressionNotice();
    const bool writePendingAfterNotice = DuskCrashLog::detail::reportWritePending();
    std::cout << "MEASURE\tsig-ign-bounded-no-spin\tnotice-pending=" << noticePending
              << "\tnotice-claimed=" << noticeClaimed
              << "\tpost-budget-write-pending=" << writePendingAfterNotice << '\n';
    bool ok = expect(!result.timedOut, "non-resumable SIG_IGN did not spin");
    ok = expect(WIFSIGNALED(result.status) && WTERMSIG(result.status) == SIGSEGV,
                "non-resumable SIG_IGN fell through to SIG_DFL") && ok;
    ok = expect(ec || bytes < 256 * 1024, "fault-loop output stayed bounded") && ok;
    ok = expect(countText(log, "Dusk Audio crash log") <= 64,
                "the record cap was not exceeded") && ok;
    ok = expect(noticePending && noticeClaimed && !writePendingAfterNotice,
                "the spent budget stops further open attempts after one notice") && ok;
    return ok;
}

bool testConcurrentThreads(bool mutant)
{
    constexpr int count = DuskCrashLog::detail::kMaxConcurrentHandlers;
    std::atomic<int> ready { 0 };
    std::atomic<int> entered { 0 };
    std::atomic<int> acquired { 0 };
    std::atomic<bool> go { false };
    std::atomic<bool> release { false };
    std::vector<std::thread> workers;
    workers.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        workers.emplace_back([&]
        {
            ready.fetch_add(1, std::memory_order_relaxed);
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            const std::uintptr_t id = mutant ? 1 : DuskCrashLog::detail::currentThreadId();
            const auto entry = DuskCrashLog::detail::enterHandler(id);
            if (entry == DuskCrashLog::detail::HandlerEntry::Acquired)
                acquired.fetch_add(1, std::memory_order_relaxed);
            entered.fetch_add(1, std::memory_order_release);
            while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
            if (entry == DuskCrashLog::detail::HandlerEntry::Acquired)
                DuskCrashLog::detail::leaveHandler(id);
        });
    }
    while (ready.load(std::memory_order_acquire) != count) std::this_thread::yield();
    go.store(true, std::memory_order_release);
    while (entered.load(std::memory_order_acquire) != count) std::this_thread::yield();
    release.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    if (!expect(acquired.load() == count,
                "the fixed table gives each concurrent thread an independent slot"))
        return false;

    gHostCalls = 0;
    setSimpleHandler(SIGABRT, &hostHandler);
    DuskCrashLog::install("concurrent", "1");
    ready.store(0);
    go.store(false);
    workers.clear();
    for (int i = 0; i < count; ++i)
    {
        workers.emplace_back([&]
        {
            ready.fetch_add(1, std::memory_order_relaxed);
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            (void) ::pthread_kill(::pthread_self(), SIGABRT);
        });
    }
    while (ready.load(std::memory_order_acquire) != count) std::this_thread::yield();
    go.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    DuskCrashLog::uninstall("concurrent", "1");
    const std::string log = readFile(DuskCrashLog::getLogFile());
    std::cout << "MEASURE\tconcurrent-threads\tacquired=" << acquired.load()
              << "\thost-calls=" << gHostCalls.load()
              << "\trecords=" << countText(log, "Dusk Audio crash log") << '\n';
    return expect(gHostCalls.load() == count, "all concurrent faults reached the host handler")
        && expect(countText(log, "Dusk Audio crash log") == count,
                  "all concurrent faults produced bounded records");
}

bool testReinstallAfterPartialUninstall(bool mutant)
{
    gHostCalls = 0;
    gUpperCalls = 0;
    setSimpleHandler(SIGABRT, &hostHandler);
    setSimpleHandler(SIGSYS, &hostHandler);
    DuskCrashLog::install("partial-reinstall", "1");
    setSiginfoHandler(SIGABRT, &upperHandler, &gUpperPrevious);
    DuskCrashLog::uninstall("partial-reinstall", "1");

    const int sysSlot = DuskCrashLog::detail::signalSlot(SIGSYS);
    if (mutant)
        DuskCrashLog::detail::installedForSignal()[sysSlot] = true;
    DuskCrashLog::install("partial-reinstall", "1");

    bool ok = expect(isCrashHandler(currentAction(SIGSYS)),
                     "reinstall re-armed a signal restored by partial uninstall")
           && expect(currentAction(SIGABRT).sa_sigaction == &upperHandler,
                     "reinstall did not duplicate itself under the upper handler");
    if (!ok)
        return false;

    ::raise(SIGABRT);
    ::raise(SIGSYS);
    ok = expect(gUpperCalls == 1, "the upper handler stayed on top")
      && expect(gHostCalls.load() == 2, "both signal paths still reached the host");

    (void) ::sigaction(SIGABRT, &gUpperPrevious, nullptr);
    DuskCrashLog::uninstall("partial-reinstall", "1");
    return ok;
}

bool testPartialUnlink(bool mutant)
{
    const std::string path = mutant ? modulePath("CRASHLOG_TEST_MODULE_NO_PIN")
                                    : modulePath("CRASHLOG_TEST_MODULE_A");
    bool ok = expect(mappedLines(path) == 0, "module starts unmapped");
    {
        Module control;
        if (!control.open(path)) return false;
        control.install();
        control.uninstall();
        control.close();
    }
    const std::size_t controlAfterClose = mappedLines(path);
    ok = expect(controlAfterClose == 0,
                "fully unlinked DPF module unloads (control measurement)") && ok;

    DuskCrashLog::install("pin-warning-probe", "1");
    DuskCrashLog::uninstall("pin-warning-probe", "1");
    DuskCrashLog::detail::writeModulePinFailureNote();
    DuskCrashLog::detail::writeModulePinFailureNote();
    const std::string warning = "could not pin crash-handler module after partial unlink";
    const std::size_t warningCount = countText(readFile(DuskCrashLog::getLogFile()), warning);
    std::cout << "MEASURE\tpartial-unlink\tmodule-pin-warnings=" << warningCount << '\n';
    ok = expect(warningCount == 1,
                "a repeated module-pin failure writes one bounded warning") && ok;

    gHostCalls = 0;
    gUpperCalls = 0;
    setSimpleHandler(SIGABRT, &hostHandler);
    Module partial;
    if (!partial.open(path)) return false;
    partial.install();
    setSiginfoHandler(SIGABRT, &upperHandler, &gUpperPrevious);
    partial.uninstall();
    const std::size_t beforeClose = mappedLines(path);
    partial.close();
    const std::size_t afterClose = mappedLines(path);
    std::cout << "MEASURE\tpartial-unlink\tcontrol-after-close=" << controlAfterClose
              << "\tpartial-before-close=" << beforeClose
              << "\tpartial-after-close=" << afterClose << '\n';

    ok = expect(beforeClose > 0, "partially linked module was mapped before dlclose") && ok;
    ok = expect(afterClose > 0,
                "partial unlink pinned the DPF module across dlclose") && ok;
    ok = expect(currentAction(SIGABRT).sa_sigaction == &upperHandler,
                "partial uninstall left the upper handler intact") && ok;
    if (!ok)
        return false;

    ::raise(SIGABRT);
    const std::string log = readFile(DuskCrashLog::getLogFile());
    return expect(gUpperCalls == 1 && gHostCalls.load() == 1,
                  "saved handler pointer remained callable and chained to the host")
        && expect(log.find("Module A v1.0.0") != std::string::npos,
                  "the pinned module still wrote its record");
}

bool testSiglongjmpRecovery(bool mutant)
{
    const pid_t child = ::fork();
    if (child == 0)
    {
        gHostCalls = 0;
        gJumped = 0;
        setSimpleHandler(SIGABRT, &jumpHandler);
        DuskCrashLog::install("siglongjmp", "1");
        if (::sigsetjmp(gJumpBuffer, 1) == 0)
            ::raise(SIGABRT);
        if (gHostCalls.load() != 1)
            ::_exit(94);
        if (mutant)
            (void) DuskCrashLog::detail::enterHandler(DuskCrashLog::detail::currentThreadId());
        ::raise(SIGABRT);
        DuskCrashLog::uninstall("siglongjmp", "1");
        ::_exit(gHostCalls.load() == 2 ? 0 : 95);
    }
    const ChildResult result = waitForChild(child, 2000);
    return expect(!result.timedOut, "siglongjmp recovery completed")
        && expect(WIFEXITED(result.status) && WEXITSTATUS(result.status) == 0,
                  "the guard slot was released before a non-returning host handler");
}

bool testFullRegistry(bool mutant)
{
    constexpr int lastSlot = DuskCrashLog::detail::kMaxPlugins - 1;
    const int count = mutant ? DuskCrashLog::detail::kMaxPlugins - 1
                             : DuskCrashLog::detail::kMaxPlugins;
    for (int i = 0; i < count; ++i)
        DuskCrashLog::install("Registry " + std::to_string(i), "1");
    DuskCrashLog::install("Registry overflow", "1");
    std::cout << "MEASURE\tfull-registry\tentry-count="
              << DuskCrashLog::detail::entryCount().load()
              << "\tcapacity=" << DuskCrashLog::detail::kMaxPlugins << '\n';

    bool ok = expect(DuskCrashLog::detail::entryCount().load() == DuskCrashLog::detail::kMaxPlugins,
                     "all fixed registry slots are usable")
           && expect(std::string(DuskCrashLog::detail::entries()[lastSlot])
                         == "Registry " + std::to_string(lastSlot) + " v1",
                     "the final registry entry is complete");

    const std::filesystem::path report = DuskCrashLog::getLogFolder() / "full-registry.txt";
    const int fd = ::open(report.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return expect(false, "full-registry report opened");
    DuskCrashLog::detail::writeReport(fd, "signal: ", SIGABRT);
    ::close(fd);
    const std::string text = readFile(report);
    ok = expect(countText(text, "  - Registry ") == DuskCrashLog::detail::kMaxPlugins,
                "a full registry is emitted without truncation") && ok;
    ok = expect(text.find("Registry overflow") == std::string::npos,
                "overflow registration stays outside fixed storage") && ok;
    for (int i = 0; i < count + 1; ++i)
        DuskCrashLog::uninstall("", "");
    return ok;
}

bool testScoped(bool mutant)
{
    setSimpleHandler(SIGABRT, &hostHandler);
    bool armed = false;
    if (mutant)
    {
        (void) new DuskCrashLog::ScopedRegistration("scoped", "1");
        armed = isCrashHandler(currentAction(SIGABRT));
    }
    else
    {
        DuskCrashLog::ScopedRegistration registration("scoped", "1");
        armed = isCrashHandler(currentAction(SIGABRT))
             && DuskCrashLog::detail::installCount() == 1;
    }
    return expect(armed, "ScopedRegistration arms during its lifetime")
        && expect(sameSimpleHandler(currentAction(SIGABRT), &hostHandler),
                  "ScopedRegistration releases and restores on destruction")
        && expect(DuskCrashLog::detail::installCount() == 0,
                  "ScopedRegistration balances the refcount");
}

using TestFunction = bool (*)(bool);

struct TestCase
{
    const char* name;
    TestFunction function;
};

constexpr TestCase tests[] = {
    { "host-handler-still-runs", &testHostHandlerStillRuns },
    { "no-sigkill", &testNoSigkill },
    { "uninstall-restores", &testUninstallRestores },
    { "chained", &testChained },
    { "refcount", &testRefcount },
    { "reentrant", &testReentrant },
    { "sig-ign-resumable", &testSigIgnResumable },
    { "sig-ign-bounded-no-spin", &testSigIgnBoundedNoSpin },
    { "concurrent-threads", &testConcurrentThreads },
    { "reinstall-after-partial-uninstall", &testReinstallAfterPartialUninstall },
    { "partial-unlink", &testPartialUnlink },
    { "siglongjmp-recovery", &testSiglongjmpRecovery },
    { "full-registry", &testFullRegistry },
    { "scoped", &testScoped },
};
}

int main(int argc, char** argv)
{
    std::string requested;
    std::string home;
    bool mutant = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--case" && i + 1 < argc)
            requested = argv[++i];
        else if (arg == "--home" && i + 1 < argc)
            home = argv[++i];
        else if (arg == "--mutant")
            mutant = true;
        else
        {
            std::cerr << "usage: CrashLogProbe --case NAME --home DIR [--mutant]\n";
            return 2;
        }
    }
    if (requested.empty() || home.empty())
    {
        std::cerr << "case and home are required\n";
        return 2;
    }
    std::error_code ec;
    std::filesystem::create_directories(home, ec);
    if (ec || ::setenv("HOME", home.c_str(), 1) != 0)
    {
        std::cerr << "could not prepare isolated HOME\n";
        return 2;
    }
    for (const TestCase& test : tests)
    {
        if (requested == test.name)
        {
            const bool passed = test.function(mutant);
            std::cout << (passed ? "PASS" : "FAIL") << '\t' << test.name
                      << '\t' << (mutant ? "mutant" : "correct") << '\n';
            return passed ? 0 : 1;
        }
    }
    std::cerr << "unknown case: " << requested << '\n';
    return 2;
}
