# How to Add Patreon Backers to 4K EQ

Your plugin now displays Patreon supporter credits! Here's how to add your backers' names:

## Quick Start

1. **Edit PatreonBackers.h** - Add names to the appropriate tier
2. **Update README.md** - Add the same names to the README
3. **Rebuild** - Run `./rebuild_all.sh` or build manually
4. **Ship it!** - The credits will appear in the plugin header

---

## Step 1: Add Names to PatreonBackers.h

Open [PatreonBackers.h](PatreonBackers.h) and add names to the appropriate arrays:

```cpp
// Platinum Tier Supporters ($50+/month)
static const std::vector<juce::String> platinumBackers = {
    "John Doe",          // Add comma after each name
    "Jane Smith",        // Except the last one
};

// Gold Tier Supporters ($25+/month)
static const std::vector<juce::String> goldBackers = {
    "Bob Johnson",
    "Alice Williams",
};

// Silver Tier Supporters ($10+/month)
static const std::vector<juce::String> silverBackers = {
    "Charlie Brown",
    "Diana Prince",
};

// Standard Supporters ($5+/month)
static const std::vector<juce::String> supporters = {
    "Eve Anderson",
    "Frank Miller",
    "Grace Lee",
};
```

**Tips:**
- Use quotes around each name: `"Name Here"`
- Add comma after each name (except the last one in each tier)
- You can use real names, usernames, or however your backers prefer to be credited

---

## Step 2: Update README.md

Open [README.md](README.md) and scroll to the "💖 Special Thanks to Our Patreon Backers" section.

Replace the placeholder text with your actual backers:

```markdown
### 🌟 Platinum Supporters
- John Doe
- Jane Smith

### ⭐ Gold Supporters
- Bob Johnson
- Alice Williams

### ✨ Silver Supporters
- Charlie Brown
- Diana Prince

### 💙 Supporters
- Eve Anderson
- Frank Miller
- Grace Lee
```

**Don't forget to update your Patreon link!**

Replace `[Become a Patreon backer](https://patreon.com/YourPatreonPage)` with your actual Patreon URL.

---

## Step 3: Rebuild the Plugin

From the `/home/marc/projects/Luna/plugins/` directory:

```bash
# Full rebuild (recommended)
./rebuild_all.sh

# Or build just 4K EQ
cd build
cmake --build . --target FourKEQ_All -j8
```

The plugin will automatically install to:
- VST3: `~/.vst3/4K EQ.vst3`
- LV2: `~/.lv2/4K EQ.lv2`

---

## Where Credits Appear

### In the Plugin GUI
A subtle message appears in the header: **"Made with support from Patreon backers 💖"**

This shows in every instance of the plugin, in all DAWs.

### In the README
Full credits with all tiers and names appear in the README for anyone browsing the source code or documentation.

---

## Automation Ideas

### Monthly Updates
Create a monthly routine:
1. Export Patreon backers list
2. Update PatreonBackers.h and README.md
3. Rebuild and release a new patch version
4. Announce the update to your community

### Tier Changes
If a backer upgrades/downgrades tiers, simply move their name between the arrays.

### Removing Backers
If someone cancels, remove their name from both files. Consider keeping long-term supporters even after they cancel as a thank you.

---

## Example: Complete PatreonBackers.h

Here's a filled-out example:

```cpp
namespace PatreonCredits
{
    static const std::vector<juce::String> platinumBackers = {
        "Studio One Producer",
        "Mix Master Mike",
    };

    static const std::vector<juce::String> goldBackers = {
        "Audio Enthusiast",
        "Sound Designer Pro",
        "Mixing Engineer Jane",
    };

    static const std::vector<juce::String> silverBackers = {
        "Producer Bob",
        "Beatmaker Sam",
        "Musician Alex",
        "Composer Chris",
    };

    static const std::vector<juce::String> supporters = {
        "Music Lover 1",
        "Music Lover 2",
        "Music Lover 3",
        "Music Lover 4",
        "Music Lover 5",
    };
}
```

---

## Version Tracking

Each time you update the backers list, consider updating the version number:
- Patch version for backer updates: `1.0.2` → `1.0.3`
- This helps backers see when their name was added

Update version in:
1. `CMakeLists.txt` - Line 2: `project(FourKEQ VERSION 1.0.3)`
2. `FourKEQ.h` - Line 31: `PLUGIN_VERSION = "1.0.3"`
3. `FourKEQ.cpp` - Line 1088: `xml->setAttribute("pluginVersion", "1.0.3")`
4. `README.md` - Add a new changelog entry

---

## Questions?

If you need help or have questions about managing the Patreon credits system, check:
- [CLAUDE.md](../../CLAUDE.md) - Project documentation
- [README.md](README.md) - Full plugin documentation

---

**Happy crediting! Your backers make this possible. 💖**
