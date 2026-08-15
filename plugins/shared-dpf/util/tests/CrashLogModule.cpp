// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only DSO wrapper used to prove that separately loaded plugin binaries
// chain correctly and that a partial unlink pins the binary that owns a saved
// handler address.

#include "CrashLog.hpp"

#ifndef CRASHLOG_MODULE_NAME
 #define CRASHLOG_MODULE_NAME "CrashLog probe module"
#endif

#ifndef CRASHLOG_MODULE_VERSION
 #define CRASHLOG_MODULE_VERSION "1.0.0"
#endif

#if defined(_WIN32)
 #define CRASHLOG_TEST_EXPORT __declspec(dllexport)
#else
 #define CRASHLOG_TEST_EXPORT __attribute__((visibility("default")))
#endif

#if defined(CRASHLOG_TEST_WRAP_DLADDR)
extern "C" int __wrap_dladdr(const void*, Dl_info*)
{
    return 0;
}
#endif

extern "C" CRASHLOG_TEST_EXPORT void crashlog_module_install()
{
    DuskCrashLog::install(CRASHLOG_MODULE_NAME, CRASHLOG_MODULE_VERSION);
}

extern "C" CRASHLOG_TEST_EXPORT void crashlog_module_uninstall()
{
    DuskCrashLog::uninstall(CRASHLOG_MODULE_NAME, CRASHLOG_MODULE_VERSION);
}
