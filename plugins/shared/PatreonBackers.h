/*
  ==============================================================================

    PatreonBackers.h

    Special thanks to our Patreon supporters who make these plugins possible!

    This file is shared across all Luna Co. Audio plugins.
    Edit this single file to update credits in all plugins.

    To add new backers:
    1. Add names to the appropriate tier array below
    2. Rebuild all plugins
    3. Also update the project README.md file with the same names

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>

namespace PatreonCredits
{
    // Platinum Tier Supporters ($50+/month)
    static const std::vector<juce::String> platinumBackers = {
        // Add platinum backer names here, one per line:
        // "John Doe",
        // "Jane Smith",
    };

    // Gold Tier Supporters ($25+/month)
    static const std::vector<juce::String> goldBackers = {
        // Add gold backer names here, one per line:
        // "John Doe",
        // "Jane Smith",
    };

    // Silver Tier Supporters ($10+/month)
    static const std::vector<juce::String> silverBackers = {
        // Add silver backer names here, one per line:
        // "John Doe",
        // "Jane Smith",
    };

    // Standard Supporters ($5+/month)
    static const std::vector<juce::String> supporters = {
        // Add supporter names here, one per line:
        // "John Doe",
        // "Jane Smith",
    };

    // Helper function to get all backers formatted for display
    static juce::String getAllBackersFormatted()
    {
        juce::String result;

        if (!platinumBackers.empty())
        {
            result += "PLATINUM SUPPORTERS\n";
            for (const auto& name : platinumBackers)
                result += "  " + name + "\n";
            result += "\n";
        }

        if (!goldBackers.empty())
        {
            result += "GOLD SUPPORTERS\n";
            for (const auto& name : goldBackers)
                result += "  " + name + "\n";
            result += "\n";
        }

        if (!silverBackers.empty())
        {
            result += "SILVER SUPPORTERS\n";
            for (const auto& name : silverBackers)
                result += "  " + name + "\n";
            result += "\n";
        }

        if (!supporters.empty())
        {
            result += "SUPPORTERS\n";
            for (const auto& name : supporters)
                result += "  " + name + "\n";
        }

        if (result.isEmpty())
            result = "Be the first to support development on Patreon!\n\nYour name could be here!";

        return result;
    }

    // Get total number of backers
    static int getTotalBackerCount()
    {
        return static_cast<int>(platinumBackers.size() + goldBackers.size() +
               silverBackers.size() + supporters.size());
    }
}
