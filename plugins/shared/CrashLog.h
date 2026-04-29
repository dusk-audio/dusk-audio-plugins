// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cross-platform crash-log helper for Dusk Audio plugins.
//
// On install(), registers a process-wide crash handler that writes a stack
// trace + plugin name + version + timestamp to a shared log file under the
// user's application-support directory. Multiple plugins from the same
// process append to the same file. No network calls; users attach the file
// manually when filing a GitHub issue.
//
// Usage (one call per plugin, in the processor constructor):
//   DuskCrashLog::install ("DuskAmp", JucePlugin_VersionString);
//
// To open the folder from a UI button:
//   DuskCrashLog::openLogFolder();

#pragma once

#if __has_include(<JuceHeader.h>)
    #include <JuceHeader.h>
#else
    #include <juce_core/juce_core.h>
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
        // The handler is process-global, so registered plugins are tracked in a
        // static list and all of them are dumped on a crash.
        struct RegisteredPlugin
        {
            juce::String name;
            juce::String version;
        };

        inline std::vector<RegisteredPlugin>& registry()
        {
            static std::vector<RegisteredPlugin> instance;
            return instance;
        }

        inline juce::CriticalSection& registryLock()
        {
            static juce::CriticalSection instance;
            return instance;
        }

        inline std::atomic<bool>& handlerInstalled()
        {
            static std::atomic<bool> installed { false };
            return installed;
        }

        inline void crashHandler (void*)
        {
            const auto folder = getLogFolder();
            folder.createDirectory();

            const auto file = getLogFile();
            const auto timestamp = juce::Time::getCurrentTime().toISO8601 (true);
            const auto trace = juce::SystemStats::getStackBacktrace();

            juce::String entry;
            entry << "============================================\n";
            entry << "Crash @ " << timestamp << "\n";
            entry << "Host:   " << juce::SystemStats::getOperatingSystemName() << "\n";
            entry << "CPU:    " << juce::SystemStats::getCpuModel() << "\n";

            {
                const juce::ScopedLock sl (registryLock());
                entry << "Plugins loaded:\n";
                for (const auto& p : registry())
                    entry << "  - " << p.name << " v" << p.version << "\n";
            }

            entry << "Stack:\n" << trace << "\n";

            file.appendText (entry);
        }
    }

    inline void install (const juce::String& pluginName, const juce::String& version)
    {
        {
            const juce::ScopedLock sl (detail::registryLock());
            // Avoid duplicate registry entries when the host instantiates the
            // plugin multiple times.
            for (const auto& p : detail::registry())
                if (p.name == pluginName && p.version == version)
                    return;
            detail::registry().push_back ({ pluginName, version });
        }

        // Idempotent — first plugin to call install wins; subsequent calls just
        // add their name to the registry.
        bool expected = false;
        if (detail::handlerInstalled().compare_exchange_strong (expected, true))
            juce::SystemStats::setApplicationCrashHandler (&detail::crashHandler);
    }

    // Opens the folder containing the crash log in the OS file manager.
    inline void openLogFolder()
    {
        const auto folder = getLogFolder();
        folder.createDirectory();
        folder.revealToUser();
    }
}
