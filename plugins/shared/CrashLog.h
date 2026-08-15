// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cross-platform crash-log helper for Dusk Audio plugins.
//
// On install(), CHAINS a crash handler in front of whatever the host already had
// and writes a stack trace + plugin name + version + timestamp to a shared log
// file under the user's application-support directory. No network calls; users
// attach the file manually when filing a GitHub issue.
//
// Usage — pair these, install in the processor ctor, uninstall in its dtor:
//   DuskCrashLog::install   ("DuskAmp", JucePlugin_VersionString);
//   DuskCrashLog::uninstall ("DuskAmp", JucePlugin_VersionString);
//
// To open the folder from a UI button:
//   DuskCrashLog::openLogFolder();
//
// ============================================================================
// WHY THIS IS NOT juce::SystemStats::setApplicationCrashHandler
// ============================================================================
// It used to be, and that was measurably harmful. JUCE's helper
// (juce_SystemStats.cpp) does two things a plugin must never do to its host:
//
//   static void handleCrash (int signum)
//   {
//       globalCrashHandler (...);
//       ::kill (getpid(), SIGKILL);      // uncatchable, unconditional
//   }
//   ...
//   ::signal (signals[i], handleCrash);  // REPLACES the host's handler
//
// Measured with a probe that installed a host-style SIGSEGV handler, then
// dereferenced null: without install() the host handler ran and the process
// exited cleanly; with install() the host handler NEVER RAN and the process
// died by SIGKILL. So a plugin scan was enough to cost the user their DAW's
// autosave, crash reporter and recovery path, for a fault anywhere in the
// process, including in the host or another vendor's plugin.
//
// This implementation instead:
//   * uses sigaction, saves the previous disposition, and CHAINS to it, so the
//     host still gets its crash. Several Dusk plugins chain to each other and
//     every one of them logs.
//   * never calls kill(). When the chain ends it restores SIG_DFL and re-raises
//     so the OS produces its normal core dump / crash report.
//   * is async-signal-safe in everything WE do. The handler's whole external
//     call graph, verified by disassembling the built .so, is: open, write,
//     close, raise, sigaction, sigemptyset, pthread_self, time, strlen,
//     __errno_location, backtrace and backtrace_symbols_fd. It allocates
//     nothing, takes no lock of its own, restores errno, and never touches
//     juce::String or juce::File. The directory is created in install(), on the
//     message thread, so the handler only opens the file. The previous version
//     allocated, locked and built juce::Strings, so a heap-corruption crash
//     deadlocked in the handler and hung the DAW with no log.
//
//     The honest exception is glibc's backtrace()/backtrace_symbols_fd(): they
//     walk the loaded-object list and take the dynamic loader lock, so a fault
//     raised while another thread is inside dlopen() (a plugin scan, say) can
//     still deadlock there. That is why the record is written in the order it
//     is: signal, time and the plugin list all reach the file through unbuffered
//     write() calls BEFORE the backtrace is attempted, so a deadlock in the
//     unwinder costs the stack, not the whole record. Replacing it with a
//     self-contained unwinder is the remaining improvement.
//   * refcounts, and restores the previous disposition when the last instance
//     goes away, so the handler does not outlive the binary it lives in. The
//     previous version never uninstalled at all, leaving the process-wide
//     disposition pointing into unmapped memory after the host dlclose()d us.
//
// KNOWN LIMITS, stated rather than papered over:
//   * The registry is per-BINARY, not per-process. Each plugin has its own copy
//     of these statics, so each logs only itself. Chaining is what makes the
//     multi-plugin case work: every chained handler appends its own entry.
//   * If another handler chained on top of ours after we installed, we cannot
//     safely unlink from the middle of the chain, so uninstall leaves it alone.
//     See uninstall() for why that beats forcing SIG_DFL.
//   * The timestamp is a raw unix epoch, because localtime_r and strftime are
//     not async-signal-safe. Convert when reading: `date -d @<value>`.
//   * At most kMaxRecordsPerProcess records are written per process, so a host
//     that recovers from faults in a loop cannot fill the disk.
//   * Windows frames are bare addresses; symbolising needs dbghelp, which
//     allocates and locks. Resolve them offline against the shipped PDB.
//   * The backtrace step can deadlock against the dynamic loader lock (see
//     above). Everything except the stack is already on disk when it runs.
//   * A handler we chain into that recovers by siglongjmp rather than returning
//     will make us log that thread's next fault twice. Bounded by the record
//     cap, and preferred over the alternative; see crashHandler().

#pragma once

#if __has_include(<JuceHeader.h>)
    #include <JuceHeader.h>
#else
    #include <juce_core/juce_core.h>
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if JUCE_WINDOWS
    // This header is included from plugin PluginProcessor.h files, i.e. BEFORE
    // the JUCE headers. Unguarded, windows.h defines min/max as macros and they
    // then break std::min/std::max inside juce_audio_processors and
    // juce_gui_basics on MSVC. Nothing in this repo or in JUCE's CMake defines
    // NOMINMAX for us. WIN32_LEAN_AND_MEAN keeps the rest of the surface down;
    // everything used here (CreateFileW, WriteFile, CaptureStackBackTrace,
    // GetSystemTimeAsFileTime, SetUnhandledExceptionFilter) survives it.
    #ifndef NOMINMAX
        #define NOMINMAX 1
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN 1
    #endif
    #include <windows.h>
#else
    #include <cerrno>
    #include <csignal>
    #include <ctime>
    #include <fcntl.h>
    #include <pthread.h>
    #include <unistd.h>
    #if __has_include(<execinfo.h>)
        #define DUSK_CRASHLOG_HAS_EXECINFO 1
        #include <execinfo.h>
    #else
        #define DUSK_CRASHLOG_HAS_EXECINFO 0
    #endif
#endif

namespace DuskCrashLog
{
    inline juce::File getLogFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Dusk Audio");
    }

    inline juce::File getLogFile()
    {
        return getLogFolder().getChildFile ("crash.log");
    }

    namespace detail
    {
        constexpr int kMaxPlugins   = 16;
        constexpr int kMaxEntryLen  = 96;    // "Name vVersion"
        constexpr int kMaxPathLen   = 1024;

        // Native destination for the report writer. This used to be a bare int
        // on both platforms, with the Windows HANDLE squeezed through
        // (int)(intptr_t)h and cast back. A 64-bit HANDLE with bit 31 set
        // truncates and then sign-extends to a different value, so every
        // WriteFile would fail and the record would be silently empty.
    #if JUCE_WINDOWS
        using LogHandle = HANDLE;
    #else
        using LogHandle = int;
    #endif

        // Plugin list, readable from the signal handler. Fixed storage and an
        // atomic count instead of std::vector + CriticalSection: the handler
        // must not allocate or lock. Entries are fully written before the count
        // is released, so a handler that runs mid-install sees a consistent
        // prefix rather than a half-built string.
        inline char (&entries())[kMaxPlugins][kMaxEntryLen]
        {
            static char storage[kMaxPlugins][kMaxEntryLen] {};
            return storage;
        }

        inline std::atomic<int>& entryCount()
        {
            static std::atomic<int> instance { 0 };
            return instance;
        }

        // Log path, resolved once on the message thread. The handler cannot
        // build it: juce::File allocates.
        inline char* logPath()
        {
            static char storage[kMaxPathLen] {};
            return storage;
        }

    #if JUCE_WINDOWS
        // Wide form, because the crash path must use CreateFileW. See the
        // filter for why the ANSI form is not good enough.
        inline wchar_t* logPathW()
        {
            static wchar_t storage[kMaxPathLen] {};
            return storage;
        }
    #endif

        // Guards install/uninstall against each other. NEVER taken in the
        // handler.
        inline juce::CriticalSection& installLock()
        {
            static juce::CriticalSection instance;
            return instance;
        }

        inline int& installCount()      { static int instance = 0;   return instance; }
        inline bool& handlerInstalled() { static bool instance = false; return instance; }

        // PER-SIGNAL, not one flag for the set. uninstall() can restore some
        // signals and not others (we are only allowed to unlink the ones we are
        // still on top of), and a single flag then made a later install() an
        // early-return, permanently losing crash logging for exactly the signals
        // that HAD been restored. That is the realistic case, not a corner one:
        // Breakpad and JIT runtimes chain over some of these signals and not
        // others.
    #if ! JUCE_WINDOWS
        inline bool* installedForSignal() noexcept
        {
            static bool storage[8] {};
            return storage;
        }
    #endif

        // Reentrancy guard, tracked per thread by id in a fixed lock-free table.
        //
        // It must be per-thread: a single global flag made two threads faulting
        // at once pathological, because the second thread would see the first
        // thread's flag, skip to the default disposition and kill the process,
        // bypassing the host's handler. That is the regression this file exists
        // to remove.
        //
        // It must NOT be thread_local. In a shared library, which is what every
        // plugin is, a thread_local resolves through __tls_get_addr, and that
        // takes the dynamic loader lock and can allocate on first touch in a
        // dlopen'd module. Verified by disassembling the .so: the thread_local
        // version put __tls_get_addr in the handler's call graph. The
        // initial-exec TLS model would avoid the call but risks "cannot
        // allocate memory in static TLS block" when a host dlopen()s us.
        //
        // Atomic loads/CAS on a pointer-sized integer are lock-free and
        // async-signal-safe, and pthread_self / GetCurrentThreadId are too.
        constexpr int kMaxConcurrentHandlers = 8;

        inline std::atomic<std::uintptr_t>* handlerThreads() noexcept
        {
            static std::atomic<std::uintptr_t> slots[kMaxConcurrentHandlers] {};
            return slots;
        }

        inline std::uintptr_t currentThreadId() noexcept
        {
        #if JUCE_WINDOWS
            const std::uintptr_t id = (std::uintptr_t) ::GetCurrentThreadId();
        #else
            const std::uintptr_t id = (std::uintptr_t) ::pthread_self();
        #endif
            return id != 0 ? id : 1;   // 0 is the "slot empty" sentinel
        }

        // Recursion and "no free slot" are NOT the same thing and must not be
        // treated the same. Recursion means our own handler faulted, so the only
        // safe move is the default disposition. A full table just means more
        // threads are crashing at once than we budgeted for: that thread should
        // skip writing but MUST still chain, or it silently steals the host's
        // crash handling.
        enum class HandlerEntry { Acquired, Recursion, TableFull };

        inline HandlerEntry enterHandler (std::uintptr_t me) noexcept
        {
            auto* slots = handlerThreads();

            for (int i = 0; i < kMaxConcurrentHandlers; ++i)
                if (slots[i].load (std::memory_order_relaxed) == me)
                    return HandlerEntry::Recursion;

            for (int i = 0; i < kMaxConcurrentHandlers; ++i)
            {
                std::uintptr_t expected = 0;
                if (slots[i].compare_exchange_strong (expected, me,
                                                      std::memory_order_acq_rel,
                                                      std::memory_order_relaxed))
                    return HandlerEntry::Acquired;
            }

            return HandlerEntry::TableFull;
        }

        inline void leaveHandler (std::uintptr_t me) noexcept
        {
            auto* slots = handlerThreads();
            for (int i = 0; i < kMaxConcurrentHandlers; ++i)
            {
                std::uintptr_t expected = me;
                if (slots[i].compare_exchange_strong (expected, (std::uintptr_t) 0,
                                                      std::memory_order_acq_rel,
                                                      std::memory_order_relaxed))
                    return;
            }
        }

        // Hard cap on records per process. Recoverable SIGSEGV/SIGFPE are
        // routine in some hosts (Wine/yabridge, anything embedding a JIT or a
        // GC), and each record costs an open + backtrace + close on the
        // faulting thread. Without a cap a host that recovers from faults in a
        // loop would grow crash.log without bound; the SIG_IGN bug below
        // produced 35k records and 34 MB in five seconds before it was fixed.
        constexpr int kMaxRecordsPerProcess = 8;

        inline std::atomic<int>& recordsWritten()
        {
            static std::atomic<int> instance { 0 };
            return instance;
        }

        // Claim a record slot, SATURATING at the cap. A bare fetch_add would
        // keep incrementing past it and eventually wrap negative at INT_MAX,
        // silently re-enabling logging: precisely in the recoverable-fault loop
        // the cap exists to survive.
        inline bool claimRecordSlot() noexcept
        {
            int seen = recordsWritten().load (std::memory_order_relaxed);
            while (seen < kMaxRecordsPerProcess)
            {
                if (recordsWritten().compare_exchange_weak (seen, seen + 1,
                                                            std::memory_order_relaxed,
                                                            std::memory_order_relaxed))
                    return true;
            }
            return false;
        }

        //--------------------------------------------------------------------
        // Async-signal-safe output helpers. No stdio, no allocation.
        //--------------------------------------------------------------------
        inline void writeAll (LogHandle fd, const char* data, size_t len) noexcept
        {
        #if ! JUCE_WINDOWS
            while (len > 0)
            {
                const ssize_t n = ::write (fd, data, len);
                if (n <= 0)
                {
                    if (n < 0 && errno == EINTR) continue;
                    return;
                }
                data += n;
                len -= (size_t) n;
            }
        #else
            DWORD written = 0;
            ::WriteFile (fd, data, (DWORD) len, &written, nullptr);
        #endif
        }

        inline void writeStr (LogHandle fd, const char* s) noexcept
        {
            if (s != nullptr)
                writeAll (fd, s, std::strlen (s));
        }

        // Unsigned decimal into a caller-owned buffer. snprintf is not on the
        // POSIX async-signal-safe list, so format by hand.
        inline void writeUnsigned (LogHandle fd, unsigned long long v) noexcept
        {
            char buf[24];
            int i = (int) sizeof (buf);
            buf[--i] = '\0';
            if (v == 0)
                buf[--i] = '0';
            while (v > 0 && i > 0)
            {
                buf[--i] = (char) ('0' + (int) (v % 10ULL));
                v /= 10ULL;
            }
            writeStr (fd, buf + i);
        }

        // Hex, for anything that will be matched against a map file or a PDB.
        // Addresses printed in decimal are useless for symbolisation.
        inline void writeHex (LogHandle fd, unsigned long long v) noexcept
        {
            static const char digits[] = "0123456789abcdef";
            char buf[20];
            int i = (int) sizeof (buf);
            buf[--i] = '\0';
            if (v == 0)
                buf[--i] = '0';
            while (v > 0 && i > 0)
            {
                buf[--i] = digits[(std::size_t) (v & 0xFULL)];
                v >>= 4;
            }
            writeStr (fd, buf + i);
        }

        inline void writeSigned (LogHandle fd, long long v) noexcept
        {
            if (v < 0)
            {
                writeStr (fd, "-");
                writeUnsigned (fd, (unsigned long long) (-v));
            }
            else
            {
                writeUnsigned (fd, (unsigned long long) v);
            }
        }

        // Copies at most destSize-1 bytes and always terminates. strncpy does
        // not guarantee the terminator.
        inline void copyBounded (char* dest, size_t destSize, const char* src) noexcept
        {
            if (destSize == 0) return;
            size_t i = 0;
            for (; src != nullptr && src[i] != '\0' && i + 1 < destSize; ++i)
                dest[i] = src[i];
            dest[i] = '\0';
        }

        // Body shared by the POSIX and Windows handlers. fd is a real fd on
        // POSIX and a HANDLE cast to int on Windows.
        // causeAsHex: Windows exception codes are only recognisable in hex
        // (0xC0000005, not 3221225477); POSIX signal numbers read as decimal.
        inline void writeReport (LogHandle fd, const char* causeLabel, long long causeValue,
                                 bool causeAsHex = false) noexcept
        {
            writeStr (fd, "============================================\n");
            writeStr (fd, "Dusk Audio crash log\n");
            writeStr (fd, causeLabel);
            if (causeAsHex)
            {
                writeStr (fd, "0x");
                writeHex (fd, (unsigned long long) causeValue);
            }
            else
            {
                writeSigned (fd, causeValue);
            }
            writeStr (fd, "\n");

            // Raw unix epoch on both platforms. localtime_r and strftime are not
            // async-signal-safe, so no formatting happens here; convert when
            // reading with `date -d @<value>` (or `Get-Date -UnixTimeSeconds`).
            // Windows needs its own clock call because time() is not available
            // in the SEH filter path on all toolchains.
            writeStr (fd, "unix time: ");
        #if ! JUCE_WINDOWS
            writeSigned (fd, (long long) ::time (nullptr));
        #else
            {
                FILETIME ft {};
                ::GetSystemTimeAsFileTime (&ft);
                // FILETIME counts 100 ns ticks from 1601-01-01; 11644473600 is
                // the offset to the unix epoch.
                const unsigned long long ticks =
                    ((unsigned long long) ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                writeSigned (fd, (long long) (ticks / 10000000ULL) - 11644473600LL);
            }
        #endif
            writeStr (fd, "\n");

            writeStr (fd, "plugins in this binary:\n");
            const int count = entryCount().load (std::memory_order_acquire);
            for (int i = 0; i < count && i < kMaxPlugins; ++i)
            {
                writeStr (fd, "  - ");
                writeStr (fd, entries()[i]);
                writeStr (fd, "\n");
            }

            writeStr (fd, "stack:\n");
        #if ! JUCE_WINDOWS && DUSK_CRASHLOG_HAS_EXECINFO
            {
                void* frames[64];
                const int n = ::backtrace (frames, 64);
                // backtrace_symbols_fd, unlike backtrace_symbols, writes without
                // calling malloc. That is the whole reason it is used here.
                ::backtrace_symbols_fd (frames, n, fd);
            }
        #elif JUCE_WINDOWS
            {
                void* frames[64];
                const USHORT n = ::CaptureStackBackTrace (0, 64, frames, nullptr);
                // Addresses only: symbolising needs dbghelp, which allocates and
                // takes a lock. Resolve offline against the shipped PDB.
                for (USHORT i = 0; i < n; ++i)
                {
                    writeStr (fd, "  0x");
                    writeHex (fd, (unsigned long long) (std::uintptr_t) frames[i]);
                    writeStr (fd, "\n");
                }
            }
        #else
            writeStr (fd, "  <no backtrace facility on this platform>\n");
        #endif
            writeStr (fd, "\n");
        }

    #if ! JUCE_WINDOWS
        inline const int* handledSignals (int& count) noexcept
        {
            static const int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGSYS };
            count = (int) (sizeof (signals) / sizeof (signals[0]));
            return signals;
        }

        inline struct sigaction* previousActions() noexcept
        {
            static struct sigaction storage[8] {};
            return storage;
        }

        inline int signalSlot (int sig) noexcept
        {
            int n = 0;
            const int* sigs = handledSignals (n);
            for (int i = 0; i < n; ++i)
                if (sigs[i] == sig)
                    return i;
            return -1;
        }

        inline void restoreDefaultAndReraise (int sig) noexcept
        {
            struct sigaction dfl {};
            dfl.sa_handler = SIG_DFL;
            ::sigemptyset (&dfl.sa_mask);
            dfl.sa_flags = 0;
            ::sigaction (sig, &dfl, nullptr);
            ::raise (sig);
        }

        // Hand the crash on to whoever held the signal before us: the host's
        // reporter, another Dusk plugin, or the default disposition. This is the
        // part JUCE's helper replaced with kill(getpid(), SIGKILL).
        inline void chainOrDefault (int sig, siginfo_t* info, void* context) noexcept
        {
            const int slot = signalSlot (sig);
            if (slot >= 0)
            {
                const struct sigaction& prev = previousActions()[slot];

                // sa_handler and sa_sigaction alias the same storage, and
                // SIG_DFL/SIG_IGN are 0/1, so testing sa_handler identifies the
                // two sentinels whichever member is the live one.
                const bool isDefault = prev.sa_handler == SIG_DFL;
                const bool isIgnored = prev.sa_handler == SIG_IGN;

                if (! isDefault && ! isIgnored)
                {
                    if ((prev.sa_flags & SA_SIGINFO) != 0)
                    {
                        if (prev.sa_sigaction != nullptr)
                        {
                            prev.sa_sigaction (sig, info, context);
                            return;
                        }
                    }
                    else if (prev.sa_handler != nullptr)
                    {
                        prev.sa_handler (sig);
                        return;
                    }
                }

                // SIG_IGN needs splitting by signal, because "ignore" means two
                // different things here.
                //
                // For the four NON-RESUMABLE faults, returning resumes the
                // faulting instruction, which faults again immediately: an
                // unkillable spin that re-enters this handler forever. Measured
                // before this was handled at 35398 records and 34 MB of
                // crash.log in five seconds. POSIX leaves SIG_IGN undefined for
                // those, so falling through to the default disposition is both
                // safe and honest.
                //
                // SIGABRT and SIGSYS are different: they resume normally on
                // return, so a host that sets SIG_IGN for them means it. Forcing
                // the default there would kill a process solely because a Dusk
                // plugin happened to be loaded, which is the class of harm this
                // whole file exists to undo.
                if (isIgnored && (sig == SIGABRT || sig == SIGSYS))
                    return;
            }

            restoreDefaultAndReraise (sig);
        }

        inline void crashHandler (int sig, siginfo_t* info, void* context) noexcept
        {
            // A signal handler must leave errno exactly as it found it. open(),
            // write() and close() below all set it, and on the resumable-fault
            // path this file supports, the interrupted code could otherwise read
            // OUR errno between its own failing syscall and its check of it.
            const int savedErrno = errno;
            struct ErrnoRestorer
            {
                int value;
                ~ErrnoRestorer() { errno = value; }
            } errnoRestorer { savedErrno };

            // A fault inside our own handler on THIS thread must not recurse.
            // Other threads are unaffected: the guard is keyed by thread id.
            const std::uintptr_t me = currentThreadId();
            const HandlerEntry entry = enterHandler (me);

            if (entry == HandlerEntry::Recursion)
            {
                restoreDefaultAndReraise (sig);
                return;
            }

            if (entry == HandlerEntry::Acquired)
            {
                // O_APPEND keeps concurrent writers appending rather than
                // overwriting each other. open/write/close are all on the
                // async-signal-safe list.
                if (claimRecordSlot())
                {
                    const int fd = ::open (logPath(), O_WRONLY | O_CREAT | O_APPEND, 0644);
                    if (fd >= 0)
                    {
                        writeReport (fd, "signal: ", (long long) sig);
                        ::close (fd);
                    }
                }

                // Released BEFORE chaining, deliberately, and this is a real
                // trade-off rather than an oversight. Both orderings have a
                // failure mode:
                //
                //   release before chaining - if the handler we chain into
                //     faults, that fault re-enters us as a fresh Acquired, so we
                //     log a second record and call the host's handler
                //     reentrantly.
                //   release after chaining  - if the handler we chain into never
                //     returns, which is what siglongjmp-based recovery does
                //     (Wine/yabridge, JIT and GC guard pages), the slot leaks.
                //     The thread's NEXT genuine fault then reads as Recursion
                //     and is forced to SIG_DFL, so the host handler is skipped.
                //
                // The first is what would happen anyway if we were not in the
                // chain at all: a host handler that faults re-enters itself with
                // or without us. The second is a failure that exists ONLY
                // because we are here, and it suppresses host crash handling,
                // which is the precise regression this file was written to
                // remove. So take the one that preserves existing behaviour.
                // Repeat logging is bounded by kMaxRecordsPerProcess.
                leaveHandler (me);
            }

            // Reached for Acquired AND TableFull. Chaining is never skipped just
            // because we could not claim a slot to log with.
            chainOrDefault (sig, info, context);
        }
    #else
        inline LPTOP_LEVEL_EXCEPTION_FILTER& previousFilter() noexcept
        {
            static LPTOP_LEVEL_EXCEPTION_FILTER instance = nullptr;
            return instance;
        }

        inline LONG WINAPI crashFilter (LPEXCEPTION_POINTERS ep) noexcept
        {
            // Same three-way decision as the POSIX handler. This branch used to
            // test `! enterHandler(me)`, which does not even compile against the
            // scoped enum, and conflated a full slot table with self-recursion.
            const std::uintptr_t me = currentThreadId();
            const HandlerEntry entry = enterHandler (me);

            if (entry == HandlerEntry::Recursion)
            {
                // Our own filter faulted. Do not try to log again; hand straight
                // on so the host or the OS terminates us properly.
                return EXCEPTION_CONTINUE_SEARCH;
            }

            if (entry == HandlerEntry::Acquired)
            {
                if (claimRecordSlot())
                {
                    // CreateFileW, not CreateFileA: the A form interprets the
                    // path in the system ANSI code page, so a UTF-8 path from a
                    // profile with any non-ASCII character in the user name
                    // fails to open and the crash goes unlogged.
                    const HANDLE h = ::CreateFileW (logPathW(), FILE_APPEND_DATA,
                                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (h != INVALID_HANDLE_VALUE)
                    {
                        const long long code = ep != nullptr && ep->ExceptionRecord != nullptr
                                                 ? (long long) ep->ExceptionRecord->ExceptionCode
                                                 : 0;
                        writeReport (h, "exception code: ", code, true);
                        ::CloseHandle (h);
                    }
                }

                leaveHandler (me);
            }
            // TableFull falls through to the chain below without logging.

            // Chain, then let the host / OS reporter run. Never
            // EXCEPTION_EXECUTE_HANDLER, which would swallow the crash.
            if (previousFilter() != nullptr)
                return previousFilter() (ep);

            return EXCEPTION_CONTINUE_SEARCH;
        }
    #endif
    }

    // Registers this plugin and, on the first call in this binary, chains our
    // handler in front of the host's. Message thread only.
    inline void install (const juce::String& pluginName, const juce::String& version)
    {
        const juce::ScopedLock sl (detail::installLock());

        ++detail::installCount();

        // Resolve the log path and create the folder HERE, where allocating and
        // hitting the filesystem is legal. The handler only open()s it.
        if (detail::logPath()[0] == '\0')
        {
            const auto folder = getLogFolder();
            folder.createDirectory();
            const juce::String path = getLogFile().getFullPathName();
            detail::copyBounded (detail::logPath(), detail::kMaxPathLen, path.toRawUTF8());
        #if JUCE_WINDOWS
            {
                const wchar_t* wide = path.toWideCharPointer();
                std::size_t i = 0;
                for (; wide[i] != L'\0' && i + 1 < (std::size_t) detail::kMaxPathLen; ++i)
                    detail::logPathW()[i] = wide[i];
                detail::logPathW()[i] = L'\0';
            }
        #endif
        }

        // Add to the plugin list unless an identical entry is already there
        // (hosts instantiate a plugin many times).
        {
            const juce::String entry = pluginName + " v" + version;
            const int count = detail::entryCount().load (std::memory_order_relaxed);
            bool alreadyListed = false;

            for (int i = 0; i < count && i < detail::kMaxPlugins; ++i)
                if (std::strcmp (detail::entries()[i], entry.toRawUTF8()) == 0)
                    alreadyListed = true;

            if (! alreadyListed && count < detail::kMaxPlugins)
            {
                detail::copyBounded (detail::entries()[count], detail::kMaxEntryLen,
                                     entry.toRawUTF8());
                // Release: the text above must be visible before the count that
                // exposes it to a handler running on another thread.
                detail::entryCount().store (count + 1, std::memory_order_release);
            }
        }

    #if ! JUCE_WINDOWS
        // backtrace() lazily dlopen()s its unwinder on first use, which
        // allocates. Force that to happen now so the handler never does it.
       #if DUSK_CRASHLOG_HAS_EXECINFO
        {
            void* warmup[4];
            (void) ::backtrace (warmup, 4);
        }
       #endif

        int numSignals = 0;
        const int* signals = detail::handledSignals (numSignals);

        struct sigaction action {};
        action.sa_sigaction = &detail::crashHandler;
        ::sigemptyset (&action.sa_mask);
        // SA_ONSTACK so a stack-overflow SIGSEGV still has somewhere to run if
        // the host installed an alternate stack. SA_NODEFER is deliberately NOT
        // set: the reentrancy guard handles recursion.
        action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;

        // Arm each signal we are not already armed for. Doing this per signal,
        // rather than gating the whole loop on one flag, is what lets a
        // reinstall recover the signals a partial uninstall gave back.
        for (int i = 0; i < numSignals; ++i)
        {
            if (detail::installedForSignal()[i])
                continue;

            if (::sigaction (signals[i], &action, &detail::previousActions()[i]) == 0)
                detail::installedForSignal()[i] = true;
        }
    #else
        if (! detail::handlerInstalled())
            detail::previousFilter() = ::SetUnhandledExceptionFilter (&detail::crashFilter);
    #endif

        detail::handlerInstalled() = true;
    }

    // Pair with install(). On the last instance the handler is removed, so it
    // cannot outlive the binary it lives in. Message thread only.
    inline void uninstall (const juce::String& /*pluginName*/, const juce::String& /*version*/)
    {
        const juce::ScopedLock sl (detail::installLock());

        if (detail::installCount() > 0)
            --detail::installCount();

        if (detail::installCount() > 0 || ! detail::handlerInstalled())
            return;

        bool fullyUnlinked = true;

    #if ! JUCE_WINDOWS
        int numSignals = 0;
        const int* signals = detail::handledSignals (numSignals);

        for (int i = 0; i < numSignals; ++i)
        {
            struct sigaction current {};
            if (::sigaction (signals[i], nullptr, &current) != 0)
                continue;

            const bool stillOurs = (current.sa_flags & SA_SIGINFO) != 0
                                && current.sa_sigaction == &detail::crashHandler;

            // Only unlink when we are still the TOP of the chain. If someone
            // chained on top of us they hold a pointer into this binary and we
            // cannot unlink from the middle, so the chain is left exactly as it
            // is.
            //
            // The obvious alternative, forcing SIG_DFL, is worse. Once several
            // Dusk plugins chain to each other, "another handler is on top of
            // us" is the NORMAL case, so unloading any one of them would strip
            // the host's crash reporter and autosave for the rest of the
            // process. Trading a guaranteed, common loss of host crash handling
            // against a rare conditional one is the wrong way round.
            //
            // Residual risk, stated plainly: if the host then unmaps this
            // binary, the handler above us points at freed code. Measured on
            // this machine, dlclose() did NOT unmap a JUCE-linked module at all
            // (static TLS and atexit registrations keep it resident), so in
            // practice the pointer stays valid. That is a mitigation, not a
            // guarantee.
            if (stillOurs)
            {
                ::sigaction (signals[i], &detail::previousActions()[i], nullptr);
                // Record it per signal, so a later install() re-arms exactly
                // this one rather than assuming the whole set is still live.
                detail::installedForSignal()[i] = false;
            }
            else
            {
                fullyUnlinked = false;
            }
        }
    #else
        // Same policy as the POSIX branch: unlink only if we are still the top
        // filter. SetUnhandledExceptionFilter returns the filter it replaced, so
        // if that is not ours then somebody chained above us and holds a pointer
        // into this binary; put their filter straight back and leave the chain
        // alone rather than stripping the host's reporter.
        LPTOP_LEVEL_EXCEPTION_FILTER current = ::SetUnhandledExceptionFilter (detail::previousFilter());
        if (current != &detail::crashFilter)
        {
            ::SetUnhandledExceptionFilter (current);
            fullyUnlinked = false;
        }
    #endif

        // Only forget that we are installed if we actually came out of the
        // chain. If we are still in it, clearing this would let a later
        // install() add us a SECOND time and save OUR OWN handler as the
        // "previous" one. The chain would then contain us twice, the second
        // traversal would hit the reentrancy guard, and the guard would go
        // straight to SIG_DFL, so the host's reporter would never run: exactly
        // the failure this file exists to remove.
        if (fullyUnlinked)
            detail::handlerInstalled() = false;
    }

    // Preferred over calling install()/uninstall() by hand: hold one as a member
    // of the processor and the pair cannot be broken by an early return or a
    // defaulted destructor.
    //
    //   class MyProcessor : public juce::AudioProcessor
    //   {
    //       DuskCrashLog::ScopedRegistration crashLog_ { "My Plugin",
    //                                                    JucePlugin_VersionString };
    //   };
    //
    // Declare it FIRST among the members so it outlives everything whose
    // construction could crash.
    class ScopedRegistration
    {
    public:
        ScopedRegistration (juce::String pluginName, juce::String version)
            : name (std::move (pluginName)), ver (std::move (version))
        {
            install (name, ver);
        }

        ~ScopedRegistration() { uninstall (name, ver); }

        ScopedRegistration (const ScopedRegistration&) = delete;
        ScopedRegistration& operator= (const ScopedRegistration&) = delete;

    private:
        juce::String name, ver;
    };

    // Opens the folder containing the crash log in the OS file manager.
    inline void openLogFolder()
    {
        const auto folder = getLogFolder();
        folder.createDirectory();
        folder.revealToUser();
    }
}
