#pragma once

#include <JuceHeader.h>

//==============================================================================
// Tape Reel Component
// Procedural graphics with cached rendering for performance
//==============================================================================
class TapeReelComponent : public juce::Component, private juce::Timer
{
public:
    //==========================================================================
    // Reel Type - Affects hub design and visual style
    //==========================================================================
    enum class ReelType
    {
        NAB,        // North American Broadcast - 3-spoke hub (Type A style)
        Cine        // Cinema style - solid hub with cutouts (Type B style)
    };

    //==========================================================================
    // Construction
    //==========================================================================
    TapeReelComponent();
    ~TapeReelComponent() override;

    //==========================================================================
    // Playback Control
    //==========================================================================

    // Set whether the reel should be rotating
    void setPlaying(bool isPlaying);
    bool isPlaying() const { return playing; }

    // Set tape speed in IPS (7.5, 15, 30) - affects visual rotation rate
    void setSpeed(float ipsSpeed);
    float getSpeed() const { return tapeSpeedIPS; }

    // Set rotation direction (true = clockwise for takeup, false = counter-clockwise for supply)
    void setClockwise(bool clockwise) { rotateClockwise = clockwise; }
    bool isClockwise() const { return rotateClockwise; }

    // Set transport mode for visual feedback
    enum class TransportMode { Stopped, Playing, FastForward, Rewind };
    void setTransportMode(TransportMode mode);
    TransportMode getTransportMode() const { return transportMode; }

    //==========================================================================
    // Visual Configuration
    //==========================================================================

    // Tape amount (0.0 = empty, 1.0 = full)
    void setTapeAmount(float amount);
    float getTapeAmount() const { return tapeAmount; }

    // Reel type (NAB or Cine style)
    void setReelType(ReelType type);
    ReelType getReelType() const { return reelType; }

    // Supply vs Takeup reel (affects default rotation direction)
    void setIsSupplyReel(bool isSupply);
    bool isSupplyReel() const { return supplyReel; }

    // Brand label text (optional)
    void setLabelText(const juce::String& text) { labelText = text; invalidateCache(); }
    const juce::String& getLabelText() const { return labelText; }

    //==========================================================================
    // Component Overrides
    //==========================================================================
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    //==========================================================================
    // Timer Callback for Animation
    //==========================================================================
    void timerCallback() override;

    //==========================================================================
    // Cached Rendering
    //==========================================================================
    void invalidateCache();
    void renderStaticElements();

    // Cached images for performance
    juce::Image flangeCache;        // Static flange with brushed metal
    juce::Image hubCache;           // Static hub structure
    juce::Image tapePackCache;      // Tape pack (changes with amount)
    int cachedSize = 0;
    float cachedTapeAmount = -1.0f;
    bool cacheValid = false;

    //==========================================================================
    // Drawing Methods
    //==========================================================================

    // Outer flange - photorealistic brushed aluminum
    void drawFlangeOuter(juce::Graphics& g, juce::Point<float> centre, float radius);

    // Flange face with depth and realistic metal finish
    void drawFlangeFace(juce::Graphics& g, juce::Point<float> centre,
                        float innerRadius, float outerRadius);

    // Decorative concentric rings on flange face
    void drawFlangeRings(juce::Graphics& g, juce::Point<float> centre,
                         float innerRadius, float outerRadius);

    // Realistic brushed metal texture
    void drawBrushedMetalTexture(juce::Graphics& g, juce::Point<float> centre,
                                  float innerRadius, float outerRadius);

    // Ventilation/lightening holes with depth effect
    void drawVentilationHoles(juce::Graphics& g, juce::Point<float> centre,
                               float innerRadius, float outerRadius, float rotation);

    // Wound tape pack with photorealistic oxide layers
    void drawTapePack(juce::Graphics& g, juce::Point<float> centre,
                      float innerRadius, float outerRadius);

    // Realistic tape edge (shiny oxide surface)
    void drawTapeEdge(juce::Graphics& g, juce::Point<float> centre, float radius);

    // Hub (NAB 3-spoke or Cine solid style) - metallic gradient finish
    void drawHub(juce::Graphics& g, juce::Point<float> centre, float radius, float rotation);
    void drawNABHub(juce::Graphics& g, juce::Point<float> centre, float radius, float rotation);
    void drawCineHub(juce::Graphics& g, juce::Point<float> centre, float radius, float rotation);

    // Center spindle with realistic depth
    void drawSpindle(juce::Graphics& g, juce::Point<float> centre, float radius);

    // Optional center label/brand area
    void drawCenterLabel(juce::Graphics& g, juce::Point<float> centre, float radius);

    // Dynamic highlights that simulate light source
    void drawLightReflections(juce::Graphics& g, juce::Point<float> centre,
                               float radius, float rotation);

    // Drop shadow for depth
    void drawDropShadow(juce::Graphics& g, juce::Point<float> centre, float radius);

    //==========================================================================
    // Helper Methods
    //==========================================================================

    // Create metallic gradient with light source simulation
    juce::ColourGradient createMetallicGradient(juce::Point<float> centre, float radius,
                                                  juce::Colour baseColor, float highlightIntensity);

    // Create depth bevel effect
    void drawBevelRing(juce::Graphics& g, juce::Point<float> centre, float radius,
                       float thickness, bool raised);

    //==========================================================================
    // State
    //==========================================================================
    float rotation = 0.0f;              // Current rotation angle (radians)
    float tapeAmount = 0.5f;            // 0.0 = empty, 1.0 = full
    float tapeSpeedIPS = 15.0f;         // Tape speed in IPS
    bool playing = false;               // Is transport running
    bool rotateClockwise = true;        // Rotation direction
    bool supplyReel = true;             // Supply or takeup reel
    ReelType reelType = ReelType::NAB;  // Hub style
    TransportMode transportMode = TransportMode::Stopped;
    juce::String labelText = "LUNA";    // Center label

    //==========================================================================
    // Animation Constants
    //==========================================================================
    static constexpr float kTargetFPS = 30.0f;
    static constexpr float kBaseRPM = 22.0f;          // Visual RPM at 15 IPS
    static constexpr float kFastMultiplier = 4.0f;    // Speed multiplier for FF/RW

    //==========================================================================
    // Visual Constants (relative to radius)
    //==========================================================================
    static constexpr float kFlangeOuterRatio = 0.96f;    // Outer flange edge
    static constexpr float kFlangeFaceRatio = 0.92f;     // Flange face surface
    static constexpr float kFlangeInnerRatio = 0.86f;    // Inner edge (tape area)
    static constexpr float kTapeMinRatio = 0.32f;        // Minimum tape pack radius
    static constexpr float kTapeMaxRatio = 0.82f;        // Maximum tape pack radius
    static constexpr float kHubOuterRatio = 0.30f;       // Hub outer radius
    static constexpr float kHubInnerRatio = 0.18f;       // Hub inner ring
    static constexpr float kSpindleRatio = 0.10f;        // Center spindle hole
    static constexpr float kLabelRatio = 0.14f;          // Label area radius
    static constexpr int kNumVentHoles = 6;              // Ventilation holes
    static constexpr int kNumSpokes = 3;                 // NAB hub spokes

    //==========================================================================
    // Light Source Position (normalized, top-left)
    //==========================================================================
    static constexpr float kLightAngle = -2.4f;          // ~135 degrees (top-left)
    static constexpr float kLightIntensity = 0.7f;       // Highlight strength

    //==========================================================================
    // Color Palette - Premium Metallic Finishes
    //==========================================================================
    struct Colors
    {
        // Aluminum flange (brushed metal)
        static constexpr uint32_t alumHighlight  = 0xffc8c8c8;   // Bright aluminum
        static constexpr uint32_t alumLight      = 0xffb0b0b0;   // Light aluminum
        static constexpr uint32_t alumMid        = 0xff8a8a8a;   // Mid aluminum
        static constexpr uint32_t alumDark       = 0xff686868;   // Dark aluminum
        static constexpr uint32_t alumShadow     = 0xff505050;   // Shadow aluminum
        static constexpr uint32_t alumEdge       = 0xff3a3a3a;   // Edge/rim

        // Tape pack (magnetic oxide)
        static constexpr uint32_t tapeOxide      = 0xff2a1a10;   // Base oxide brown
        static constexpr uint32_t tapeDark       = 0xff180c06;   // Dark oxide
        static constexpr uint32_t tapeLight      = 0xff3c2a1c;   // Light oxide
        static constexpr uint32_t tapeSheen      = 0xff4a3828;   // Surface sheen
        static constexpr uint32_t tapeEdge       = 0xff221408;   // Tape edge

        // Chrome hub
        static constexpr uint32_t chromeHighlight = 0xffd0d0d0;  // Chrome highlight
        static constexpr uint32_t chromeLight    = 0xffb8b8b8;   // Light chrome
        static constexpr uint32_t chromeMid      = 0xff989898;   // Mid chrome
        static constexpr uint32_t chromeDark     = 0xff686868;   // Dark chrome
        static constexpr uint32_t chromeShadow   = 0xff484848;   // Chrome shadow

        // Spindle and interior
        static constexpr uint32_t spindleOuter   = 0xff404040;   // Spindle rim
        static constexpr uint32_t spindleInner   = 0xff1a1a1a;   // Spindle hole
        static constexpr uint32_t spindleDeep    = 0xff080808;   // Deep interior

        // Label area
        static constexpr uint32_t labelBg        = 0xfff8f0e0;   // Cream paper
        static constexpr uint32_t labelBgDark    = 0xffe8dcc8;   // Aged paper
        static constexpr uint32_t labelText      = 0xff2a1a10;   // Dark brown text
        static constexpr uint32_t labelBorder    = 0xffc0a080;   // Gold border

        // Highlights and shadows
        static constexpr uint32_t highlightBright = 0x60ffffff;  // Bright specular
        static constexpr uint32_t highlightSoft  = 0x30ffffff;   // Soft highlight
        static constexpr uint32_t highlightSubtle = 0x18ffffff;  // Subtle highlight
        static constexpr uint32_t shadowMedium   = 0x50000000;   // Medium shadow
        static constexpr uint32_t shadowSoft     = 0x30000000;   // Soft shadow
        static constexpr uint32_t shadowSubtle   = 0x18000000;   // Subtle shadow
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeReelComponent)
};
