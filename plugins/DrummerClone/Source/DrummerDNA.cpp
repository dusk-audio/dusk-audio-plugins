#include "DrummerDNA.h"

DrummerDNA::DrummerDNA()
{
    createDefaultProfiles();
}

void DrummerDNA::createDefaultProfiles()
{
    profiles.clear();

    // ========== ROCK DRUMMERS ==========
    {
        DrummerProfile p;
        p.name = "Kyle";
        p.style = "Rock";
        p.bio = "Hard-hitting rock drummer with a solid backbeat. Great for classic rock and blues.";
        p.aggression = 0.7f;
        p.grooveBias = 0.2f;
        p.ghostNotes = 0.2f;
        p.fillHunger = 0.4f;
        p.tomLove = 0.6f;
        p.ridePreference = 0.3f;
        p.crashHappiness = 0.5f;
        p.simplicity = 0.6f;
        p.laidBack = 0.1f;
        p.preferredDivision = 8;
        p.swingDefault = 0.05f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Anders";
        p.style = "Rock";
        p.bio = "Heavy rock drummer inspired by 70s arena rock. Powerful fills and driving rhythms.";
        p.aggression = 0.85f;
        p.grooveBias = 0.1f;
        p.ghostNotes = 0.15f;
        p.fillHunger = 0.5f;
        p.tomLove = 0.8f;
        p.ridePreference = 0.2f;
        p.crashHappiness = 0.7f;
        p.simplicity = 0.4f;
        p.laidBack = -0.1f;
        p.preferredDivision = 8;
        p.swingDefault = 0.0f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Max";
        p.style = "Rock";
        p.bio = "Modern rock drummer with punk influences. Fast and energetic.";
        p.aggression = 0.8f;
        p.grooveBias = 0.0f;
        p.ghostNotes = 0.1f;
        p.fillHunger = 0.3f;
        p.tomLove = 0.4f;
        p.ridePreference = 0.1f;
        p.crashHappiness = 0.6f;
        p.simplicity = 0.7f;
        p.laidBack = -0.15f;
        p.preferredDivision = 8;
        p.swingDefault = 0.0f;
        profiles.push_back(p);
    }

    // ========== ALTERNATIVE DRUMMERS ==========
    {
        DrummerProfile p;
        p.name = "Logan";
        p.style = "Alternative";
        p.bio = "Indie rock drummer with creative fills. Perfect for alternative and indie tracks.";
        p.aggression = 0.5f;
        p.grooveBias = 0.3f;
        p.ghostNotes = 0.4f;
        p.fillHunger = 0.35f;
        p.tomLove = 0.5f;
        p.ridePreference = 0.5f;
        p.crashHappiness = 0.4f;
        p.simplicity = 0.4f;
        p.laidBack = 0.0f;
        p.preferredDivision = 16;
        p.swingDefault = 0.1f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Aidan";
        p.style = "Alternative";
        p.bio = "Post-punk inspired drummer. Atmospheric and textural approach.";
        p.aggression = 0.4f;
        p.grooveBias = 0.4f;
        p.ghostNotes = 0.3f;
        p.fillHunger = 0.2f;
        p.tomLove = 0.3f;
        p.ridePreference = 0.7f;
        p.crashHappiness = 0.3f;
        p.simplicity = 0.5f;
        p.laidBack = 0.15f;
        p.preferredDivision = 16;
        p.swingDefault = 0.05f;
        profiles.push_back(p);
    }

    // ========== HIP-HOP DRUMMERS ==========
    {
        DrummerProfile p;
        p.name = "Austin";
        p.style = "HipHop";
        p.bio = "Classic boom-bap hip-hop style. Tight kicks and snappy snares.";
        p.aggression = 0.6f;
        p.grooveBias = 0.6f;
        p.ghostNotes = 0.5f;
        p.fillHunger = 0.15f;
        p.tomLove = 0.2f;
        p.ridePreference = 0.1f;
        p.crashHappiness = 0.2f;
        p.simplicity = 0.6f;
        p.laidBack = 0.2f;
        p.preferredDivision = 16;
        p.swingDefault = 0.25f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Tyrell";
        p.style = "HipHop";
        p.bio = "Modern trap-influenced hip-hop. Complex hi-hat patterns and 808 style.";
        p.aggression = 0.7f;
        p.grooveBias = 0.3f;
        p.ghostNotes = 0.2f;
        p.fillHunger = 0.1f;
        p.tomLove = 0.1f;
        p.ridePreference = 0.0f;
        p.crashHappiness = 0.15f;
        p.simplicity = 0.3f;
        p.laidBack = 0.05f;
        p.preferredDivision = 16;
        p.swingDefault = 0.0f;
        profiles.push_back(p);
    }

    // ========== R&B DRUMMERS ==========
    {
        DrummerProfile p;
        p.name = "Brooklyn";
        p.style = "R&B";
        p.bio = "Smooth neo-soul drummer. Pocket grooves with tasteful ghost notes.";
        p.aggression = 0.4f;
        p.grooveBias = 0.7f;
        p.ghostNotes = 0.7f;
        p.fillHunger = 0.2f;
        p.tomLove = 0.3f;
        p.ridePreference = 0.4f;
        p.crashHappiness = 0.25f;
        p.simplicity = 0.5f;
        p.laidBack = 0.25f;
        p.preferredDivision = 16;
        p.swingDefault = 0.3f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Darnell";
        p.style = "R&B";
        p.bio = "Gospel-influenced R&B drummer. Dynamic and expressive with intricate patterns.";
        p.aggression = 0.5f;
        p.grooveBias = 0.6f;
        p.ghostNotes = 0.8f;
        p.fillHunger = 0.4f;
        p.tomLove = 0.5f;
        p.ridePreference = 0.3f;
        p.crashHappiness = 0.35f;
        p.simplicity = 0.2f;
        p.laidBack = 0.1f;
        p.preferredDivision = 16;
        p.swingDefault = 0.2f;
        profiles.push_back(p);
    }

    // ========== ELECTRONIC DRUMMERS ==========
    {
        DrummerProfile p;
        p.name = "Niklas";
        p.style = "Electronic";
        p.bio = "Four-on-the-floor electronic beats. Clean and precise.";
        p.aggression = 0.6f;
        p.grooveBias = 0.0f;
        p.ghostNotes = 0.0f;
        p.fillHunger = 0.1f;
        p.tomLove = 0.1f;
        p.ridePreference = 0.0f;
        p.crashHappiness = 0.3f;
        p.simplicity = 0.8f;
        p.laidBack = 0.0f;
        p.preferredDivision = 16;
        p.swingDefault = 0.0f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Lexi";
        p.style = "Electronic";
        p.bio = "Synth-pop and electro influenced. Punchy with creative variations.";
        p.aggression = 0.55f;
        p.grooveBias = 0.2f;
        p.ghostNotes = 0.1f;
        p.fillHunger = 0.2f;
        p.tomLove = 0.2f;
        p.ridePreference = 0.1f;
        p.crashHappiness = 0.4f;
        p.simplicity = 0.6f;
        p.laidBack = 0.0f;
        p.preferredDivision = 16;
        p.swingDefault = 0.0f;
        profiles.push_back(p);
    }

    // ========== SONGWRITER/ACOUSTIC DRUMMERS ==========
    {
        DrummerProfile p;
        p.name = "Jesse";
        p.style = "Songwriter";
        p.bio = "Sensitive singer-songwriter accompanist. Supports without overpowering.";
        p.aggression = 0.3f;
        p.grooveBias = 0.4f;
        p.ghostNotes = 0.4f;
        p.fillHunger = 0.15f;
        p.tomLove = 0.2f;
        p.ridePreference = 0.6f;
        p.crashHappiness = 0.2f;
        p.simplicity = 0.7f;
        p.laidBack = 0.1f;
        p.preferredDivision = 8;
        p.swingDefault = 0.15f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Maya";
        p.style = "Songwriter";
        p.bio = "Folk-influenced acoustic drummer. Brushes and mallets, warm and organic.";
        p.aggression = 0.25f;
        p.grooveBias = 0.5f;
        p.ghostNotes = 0.3f;
        p.fillHunger = 0.1f;
        p.tomLove = 0.15f;
        p.ridePreference = 0.7f;
        p.crashHappiness = 0.15f;
        p.simplicity = 0.8f;
        p.laidBack = 0.2f;
        p.preferredDivision = 8;
        p.swingDefault = 0.2f;
        p.velocityFloor = 30;
        p.velocityCeiling = 100;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Emily";
        p.style = "Songwriter";
        p.bio = "Country and Americana influenced. Steady grooves with tasteful fills.";
        p.aggression = 0.35f;
        p.grooveBias = 0.35f;
        p.ghostNotes = 0.25f;
        p.fillHunger = 0.2f;
        p.tomLove = 0.35f;
        p.ridePreference = 0.5f;
        p.crashHappiness = 0.3f;
        p.simplicity = 0.65f;
        p.laidBack = 0.05f;
        p.preferredDivision = 8;
        p.swingDefault = 0.1f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Sam";
        p.style = "Songwriter";
        p.bio = "Coffee shop acoustic vibe. Minimal and supportive.";
        p.aggression = 0.2f;
        p.grooveBias = 0.45f;
        p.ghostNotes = 0.35f;
        p.fillHunger = 0.05f;
        p.tomLove = 0.1f;
        p.ridePreference = 0.8f;
        p.crashHappiness = 0.1f;
        p.simplicity = 0.85f;
        p.laidBack = 0.15f;
        p.preferredDivision = 8;
        p.swingDefault = 0.18f;
        p.velocityFloor = 25;
        p.velocityCeiling = 90;
        profiles.push_back(p);
    }

    // ========== TRAP DRUMMERS ==========
    {
        DrummerProfile p;
        p.name = "Xavier";
        p.style = "Trap";
        p.bio = "Atlanta trap style. Rolling hi-hats and hard-hitting 808s.";
        p.aggression = 0.75f;
        p.grooveBias = 0.1f;
        p.ghostNotes = 0.05f;
        p.fillHunger = 0.05f;
        p.tomLove = 0.05f;
        p.ridePreference = 0.0f;
        p.crashHappiness = 0.2f;
        p.simplicity = 0.3f;
        p.laidBack = 0.0f;
        p.preferredDivision = 16;
        p.swingDefault = 0.0f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Jayden";
        p.style = "Trap";
        p.bio = "Melodic trap producer style. Bouncy patterns with space.";
        p.aggression = 0.6f;
        p.grooveBias = 0.15f;
        p.ghostNotes = 0.1f;
        p.fillHunger = 0.1f;
        p.tomLove = 0.1f;
        p.ridePreference = 0.0f;
        p.crashHappiness = 0.25f;
        p.simplicity = 0.4f;
        p.laidBack = 0.05f;
        p.preferredDivision = 16;
        p.swingDefault = 0.05f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Zion";
        p.style = "Trap";
        p.bio = "Dark trap aesthetics. Heavy 808s and aggressive patterns.";
        p.aggression = 0.9f;
        p.grooveBias = 0.05f;
        p.ghostNotes = 0.0f;
        p.fillHunger = 0.08f;
        p.tomLove = 0.0f;
        p.ridePreference = 0.0f;
        p.crashHappiness = 0.35f;
        p.simplicity = 0.5f;
        p.laidBack = -0.05f;
        p.preferredDivision = 16;
        p.swingDefault = 0.0f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Luna";
        p.style = "Trap";
        p.bio = "Lo-fi trap style. Chill but with attitude.";
        p.aggression = 0.5f;
        p.grooveBias = 0.25f;
        p.ghostNotes = 0.15f;
        p.fillHunger = 0.12f;
        p.tomLove = 0.15f;
        p.ridePreference = 0.1f;
        p.crashHappiness = 0.2f;
        p.simplicity = 0.55f;
        p.laidBack = 0.1f;
        p.preferredDivision = 16;
        p.swingDefault = 0.1f;
        profiles.push_back(p);
    }

    // ========== ADDITIONAL ROCK DRUMMERS ==========
    {
        DrummerProfile p;
        p.name = "Ricky";
        p.style = "Rock";
        p.bio = "Metal and hard rock specialist. Double bass and aggressive fills.";
        p.aggression = 0.95f;
        p.grooveBias = 0.0f;
        p.ghostNotes = 0.05f;
        p.fillHunger = 0.45f;
        p.tomLove = 0.85f;
        p.ridePreference = 0.15f;
        p.crashHappiness = 0.8f;
        p.simplicity = 0.2f;
        p.laidBack = -0.2f;
        p.preferredDivision = 16;
        p.swingDefault = 0.0f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Jake";
        p.style = "Rock";
        p.bio = "Classic rock legend vibes. Pocket player with tasteful fills.";
        p.aggression = 0.65f;
        p.grooveBias = 0.25f;
        p.ghostNotes = 0.3f;
        p.fillHunger = 0.3f;
        p.tomLove = 0.55f;
        p.ridePreference = 0.4f;
        p.crashHappiness = 0.45f;
        p.simplicity = 0.55f;
        p.laidBack = 0.05f;
        p.preferredDivision = 8;
        p.swingDefault = 0.08f;
        profiles.push_back(p);
    }

    // ========== ADDITIONAL ALTERNATIVE DRUMMERS ==========
    {
        DrummerProfile p;
        p.name = "River";
        p.style = "Alternative";
        p.bio = "Shoegaze and dreampop influenced. Washes of cymbals and dynamic builds.";
        p.aggression = 0.45f;
        p.grooveBias = 0.35f;
        p.ghostNotes = 0.25f;
        p.fillHunger = 0.25f;
        p.tomLove = 0.4f;
        p.ridePreference = 0.8f;
        p.crashHappiness = 0.55f;
        p.simplicity = 0.45f;
        p.laidBack = 0.2f;
        p.preferredDivision = 8;
        p.swingDefault = 0.0f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Quinn";
        p.style = "Alternative";
        p.bio = "Math rock precision. Complex time signatures and intricate patterns.";
        p.aggression = 0.6f;
        p.grooveBias = 0.1f;
        p.ghostNotes = 0.5f;
        p.fillHunger = 0.35f;
        p.tomLove = 0.6f;
        p.ridePreference = 0.45f;
        p.crashHappiness = 0.4f;
        p.simplicity = 0.1f;
        p.laidBack = 0.0f;
        p.preferredDivision = 16;
        p.swingDefault = 0.0f;
        profiles.push_back(p);
    }

    // ========== ADDITIONAL HIP-HOP DRUMMERS ==========
    {
        DrummerProfile p;
        p.name = "Marcus";
        p.style = "HipHop";
        p.bio = "J Dilla inspired. Off-grid swing and soulful grooves.";
        p.aggression = 0.5f;
        p.grooveBias = 0.8f;
        p.ghostNotes = 0.4f;
        p.fillHunger = 0.1f;
        p.tomLove = 0.15f;
        p.ridePreference = 0.05f;
        p.crashHappiness = 0.15f;
        p.simplicity = 0.5f;
        p.laidBack = 0.3f;
        p.preferredDivision = 16;
        p.swingDefault = 0.35f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Kira";
        p.style = "HipHop";
        p.bio = "West coast G-funk style. Laid back with funky bounce.";
        p.aggression = 0.55f;
        p.grooveBias = 0.7f;
        p.ghostNotes = 0.35f;
        p.fillHunger = 0.15f;
        p.tomLove = 0.2f;
        p.ridePreference = 0.1f;
        p.crashHappiness = 0.2f;
        p.simplicity = 0.55f;
        p.laidBack = 0.25f;
        p.preferredDivision = 16;
        p.swingDefault = 0.3f;
        profiles.push_back(p);
    }

    // ========== ADDITIONAL R&B DRUMMERS ==========
    {
        DrummerProfile p;
        p.name = "Aaliyah";
        p.style = "R&B";
        p.bio = "90s R&B throwback. Smooth pocket with swing.";
        p.aggression = 0.45f;
        p.grooveBias = 0.65f;
        p.ghostNotes = 0.6f;
        p.fillHunger = 0.18f;
        p.tomLove = 0.25f;
        p.ridePreference = 0.35f;
        p.crashHappiness = 0.2f;
        p.simplicity = 0.6f;
        p.laidBack = 0.2f;
        p.preferredDivision = 16;
        p.swingDefault = 0.25f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Andre";
        p.style = "R&B";
        p.bio = "Modern R&B and PBR&B fusion. Minimalist yet impactful.";
        p.aggression = 0.55f;
        p.grooveBias = 0.5f;
        p.ghostNotes = 0.45f;
        p.fillHunger = 0.15f;
        p.tomLove = 0.2f;
        p.ridePreference = 0.2f;
        p.crashHappiness = 0.25f;
        p.simplicity = 0.65f;
        p.laidBack = 0.15f;
        p.preferredDivision = 16;
        p.swingDefault = 0.15f;
        profiles.push_back(p);
    }

    // ========== ADDITIONAL ELECTRONIC DRUMMERS ==========
    {
        DrummerProfile p;
        p.name = "Sasha";
        p.style = "Electronic";
        p.bio = "Techno and house specialist. Hypnotic and driving.";
        p.aggression = 0.7f;
        p.grooveBias = 0.05f;
        p.ghostNotes = 0.05f;
        p.fillHunger = 0.05f;
        p.tomLove = 0.05f;
        p.ridePreference = 0.15f;
        p.crashHappiness = 0.25f;
        p.simplicity = 0.75f;
        p.laidBack = 0.0f;
        p.preferredDivision = 16;
        p.swingDefault = 0.0f;
        profiles.push_back(p);
    }
    {
        DrummerProfile p;
        p.name = "Felix";
        p.style = "Electronic";
        p.bio = "Breakbeat and jungle influenced. Complex rhythms with energy.";
        p.aggression = 0.75f;
        p.grooveBias = 0.2f;
        p.ghostNotes = 0.3f;
        p.fillHunger = 0.25f;
        p.tomLove = 0.3f;
        p.ridePreference = 0.2f;
        p.crashHappiness = 0.35f;
        p.simplicity = 0.2f;
        p.laidBack = -0.1f;
        p.preferredDivision = 16;
        p.swingDefault = 0.1f;
        profiles.push_back(p);
    }

    // Set default profile
    defaultProfile = profiles[0];
}

const DrummerProfile& DrummerDNA::getProfile(int index) const
{
    if (index >= 0 && index < static_cast<int>(profiles.size()))
        return profiles[index];
    return defaultProfile;
}

const DrummerProfile& DrummerDNA::getProfileByName(const juce::String& name) const
{
    for (const auto& profile : profiles)
    {
        if (profile.name == name)
            return profile;
    }
    return defaultProfile;
}

std::vector<int> DrummerDNA::getDrummersByStyle(const juce::String& style) const
{
    std::vector<int> indices;
    for (size_t i = 0; i < profiles.size(); ++i)
    {
        if (profiles[i].style == style)
            indices.push_back(static_cast<int>(i));
    }
    return indices;
}

juce::StringArray DrummerDNA::getDrummerNames() const
{
    juce::StringArray names;
    for (const auto& profile : profiles)
    {
        names.add(profile.name + " - " + profile.style);
    }
    return names;
}

juce::StringArray DrummerDNA::getStyleNames() const
{
    juce::StringArray styles;
    for (const auto& profile : profiles)
    {
        if (!styles.contains(profile.style))
            styles.add(profile.style);
    }
    return styles;
}

void DrummerDNA::loadFromDirectory(const juce::File& directory)
{
    if (!directory.exists() || !directory.isDirectory())
        return;

    auto jsonFiles = directory.findChildFiles(juce::File::findFiles, false, "*.json");

    for (const auto& file : jsonFiles)
    {
        auto profile = loadFromJSON(file);
        if (!profile.name.isEmpty())
            profiles.push_back(profile);
    }
}

void DrummerDNA::saveToJSON(const DrummerProfile& profile, const juce::File& file)
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();

    obj->setProperty("name", profile.name);
    obj->setProperty("style", profile.style);
    obj->setProperty("bio", profile.bio);
    obj->setProperty("aggression", profile.aggression);
    obj->setProperty("grooveBias", profile.grooveBias);
    obj->setProperty("ghostNotes", profile.ghostNotes);
    obj->setProperty("fillHunger", profile.fillHunger);
    obj->setProperty("tomLove", profile.tomLove);
    obj->setProperty("ridePreference", profile.ridePreference);
    obj->setProperty("crashHappiness", profile.crashHappiness);
    obj->setProperty("simplicity", profile.simplicity);
    obj->setProperty("laidBack", profile.laidBack);
    obj->setProperty("preferredDivision", profile.preferredDivision);
    obj->setProperty("swingDefault", profile.swingDefault);
    obj->setProperty("velocityFloor", profile.velocityFloor);
    obj->setProperty("velocityCeiling", profile.velocityCeiling);

    juce::var json(obj.get());
    file.replaceWithText(juce::JSON::toString(json, true));
}

DrummerProfile DrummerDNA::loadFromJSON(const juce::File& file)
{
    DrummerProfile profile;

    auto json = juce::JSON::parse(file);
    if (json.isObject())
    {
        auto* obj = json.getDynamicObject();

        profile.name = obj->getProperty("name").toString();
        profile.style = obj->getProperty("style").toString();
        profile.bio = obj->getProperty("bio").toString();
        profile.aggression = static_cast<float>(obj->getProperty("aggression"));
        profile.grooveBias = static_cast<float>(obj->getProperty("grooveBias"));
        profile.ghostNotes = static_cast<float>(obj->getProperty("ghostNotes"));
        profile.fillHunger = static_cast<float>(obj->getProperty("fillHunger"));
        profile.tomLove = static_cast<float>(obj->getProperty("tomLove"));
        profile.ridePreference = static_cast<float>(obj->getProperty("ridePreference"));
        profile.crashHappiness = static_cast<float>(obj->getProperty("crashHappiness"));
        profile.simplicity = static_cast<float>(obj->getProperty("simplicity"));
        profile.laidBack = static_cast<float>(obj->getProperty("laidBack"));
        profile.preferredDivision = static_cast<int>(obj->getProperty("preferredDivision"));
        profile.swingDefault = static_cast<float>(obj->getProperty("swingDefault"));
        profile.velocityFloor = static_cast<int>(obj->getProperty("velocityFloor"));
        profile.velocityCeiling = static_cast<int>(obj->getProperty("velocityCeiling"));
    }

    return profile;
}