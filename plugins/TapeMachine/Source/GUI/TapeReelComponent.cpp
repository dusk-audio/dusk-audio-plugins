#include "TapeReelComponent.h"

//==============================================================================
// Construction
//==============================================================================
TapeReelComponent::TapeReelComponent()
{
    setOpaque(false);
}

TapeReelComponent::~TapeReelComponent()
{
    stopTimer();
}

//==============================================================================
// Playback Control
//==============================================================================
void TapeReelComponent::setPlaying(bool isPlaying)
{
    if (playing != isPlaying)
    {
        playing = isPlaying;

        if (playing)
            startTimerHz(static_cast<int>(kTargetFPS));
        else
            stopTimer();
    }
}

void TapeReelComponent::setSpeed(float speedMultiplier)
{
    // Speed is passed as a multiplier (0 = stopped, 1.0 = 7.5 IPS, 1.5 = 15 IPS, 2.0 = 30 IPS)
    tapeSpeedIPS = juce::jlimit(0.0f, 10.0f, speedMultiplier);

    // Auto-start/stop based on speed
    if (speedMultiplier > 0.01f && !playing)
        setPlaying(true);
    else if (speedMultiplier < 0.01f && playing)
        setPlaying(false);
}

void TapeReelComponent::setTransportMode(TransportMode mode)
{
    transportMode = mode;
    setPlaying(mode != TransportMode::Stopped);
}

//==============================================================================
// Visual Configuration
//==============================================================================
void TapeReelComponent::setTapeAmount(float amount)
{
    float newAmount = juce::jlimit(0.0f, 1.0f, amount);
    if (std::abs(tapeAmount - newAmount) > 0.001f)
    {
        tapeAmount = newAmount;
        repaint();
    }
}

void TapeReelComponent::setReelType(ReelType type)
{
    if (reelType != type)
    {
        reelType = type;
        invalidateCache();
    }
}

void TapeReelComponent::setIsSupplyReel(bool isSupply)
{
    supplyReel = isSupply;
    // During playback, both reels rotate clockwise (tape moves left to right)
    // The supply reel unwinds clockwise, takeup reel winds clockwise
    rotateClockwise = true;
}

//==============================================================================
// Component Overrides
//==============================================================================
void TapeReelComponent::resized()
{
    invalidateCache();
}

void TapeReelComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    auto centre = bounds.getCentre();
    auto radius = size * 0.48f;

    // Render static elements to cache if needed
    if (!cacheValid || cachedSize != static_cast<int>(size))
    {
        cachedSize = static_cast<int>(size);
        renderStaticElements();
    }

    // Draw drop shadow first (underneath everything)
    drawDropShadow(g, centre, radius);

    // Draw cached flange (static brushed metal)
    if (flangeCache.isValid())
    {
        auto cacheX = centre.x - flangeCache.getWidth() * 0.5f;
        auto cacheY = centre.y - flangeCache.getHeight() * 0.5f;
        g.drawImageAt(flangeCache, static_cast<int>(cacheX), static_cast<int>(cacheY));
    }

    // Calculate dynamic radii
    float hubRadius = radius * kHubOuterRatio;
    float tapeOuterRadius = radius * (kTapeMinRatio + tapeAmount * (kTapeMaxRatio - kTapeMinRatio));
    float flangeInner = radius * kFlangeInnerRatio;

    // Draw ventilation holes (rotate with reel)
    drawVentilationHoles(g, centre, tapeOuterRadius * 1.02f, flangeInner, rotation);

    // Draw tape pack (if there's tape)
    if (tapeAmount > 0.02f)
    {
        drawTapePack(g, centre, hubRadius * 1.05f, tapeOuterRadius);
        drawTapeEdge(g, centre, tapeOuterRadius);
    }

    // Draw dynamic light reflections on flange
    drawLightReflections(g, centre, radius * kFlangeOuterRatio, rotation);

    // Draw hub (rotates with reel)
    drawHub(g, centre, hubRadius, rotation);

    // Draw center spindle
    drawSpindle(g, centre, radius * kSpindleRatio);

    // Draw center label (static, doesn't rotate)
    drawCenterLabel(g, centre, radius * kLabelRatio);
}

//==============================================================================
// Timer Callback
//==============================================================================
void TapeReelComponent::timerCallback()
{
    // Speed is already a multiplier (1.0 = base speed, 1.5 = faster, 2.0 = fastest)
    float rpm = kBaseRPM * tapeSpeedIPS;

    if (transportMode == TransportMode::FastForward || transportMode == TransportMode::Rewind)
        rpm *= kFastMultiplier;

    // Convert RPM to radians per frame
    float radiansPerSecond = rpm * (2.0f * juce::MathConstants<float>::pi) / 60.0f;
    float radiansPerFrame = radiansPerSecond / kTargetFPS;

    // Apply direction
    if (!rotateClockwise)
        radiansPerFrame = -radiansPerFrame;

    // Update rotation
    rotation += radiansPerFrame;

    // Keep rotation in range [0, 2π)
    while (rotation >= juce::MathConstants<float>::twoPi)
        rotation -= juce::MathConstants<float>::twoPi;
    while (rotation < 0.0f)
        rotation += juce::MathConstants<float>::twoPi;

    repaint();
}

//==============================================================================
// Cache Management
//==============================================================================
void TapeReelComponent::invalidateCache()
{
    cacheValid = false;
    repaint();
}

void TapeReelComponent::renderStaticElements()
{
    if (cachedSize <= 0)
        return;

    float radius = cachedSize * 0.48f;
    juce::Point<float> centre(cachedSize * 0.5f, cachedSize * 0.5f);

    // Create flange cache with all static elements
    flangeCache = juce::Image(juce::Image::ARGB, cachedSize, cachedSize, true);
    juce::Graphics fg(flangeCache);

    // Draw static flange elements
    drawFlangeOuter(fg, centre, radius);
    drawFlangeFace(fg, centre, radius * kFlangeInnerRatio, radius * kFlangeFaceRatio);
    drawFlangeRings(fg, centre, radius * kFlangeInnerRatio, radius * kFlangeFaceRatio);
    drawBrushedMetalTexture(fg, centre, radius * kFlangeInnerRatio, radius * kFlangeOuterRatio);

    cacheValid = true;
}

//==============================================================================
// Drawing: Drop Shadow
//==============================================================================
void TapeReelComponent::drawDropShadow(juce::Graphics& g, juce::Point<float> centre, float radius)
{
    // Multi-layer soft shadow for depth
    float shadowOffset = radius * 0.04f;

    // Outer soft shadow
    juce::ColourGradient shadowGrad(
        juce::Colour(0x40000000), centre.x + shadowOffset, centre.y + shadowOffset,
        juce::Colour(0x00000000), centre.x + shadowOffset, centre.y + shadowOffset + radius * 0.15f,
        true);
    g.setGradientFill(shadowGrad);
    g.fillEllipse(centre.x - radius * 1.05f + shadowOffset,
                  centre.y - radius * 1.05f + shadowOffset,
                  radius * 2.1f, radius * 2.1f);

    // Inner darker shadow
    g.setColour(juce::Colour(0x30000000));
    g.fillEllipse(centre.x - radius + shadowOffset * 0.7f,
                  centre.y - radius + shadowOffset * 0.7f,
                  radius * 2.0f, radius * 2.0f);
}

//==============================================================================
// Drawing: Outer Flange (Rim)
//==============================================================================
void TapeReelComponent::drawFlangeOuter(juce::Graphics& g, juce::Point<float> centre, float radius)
{
    float outerRadius = radius * kFlangeOuterRatio;
    float faceRadius = radius * kFlangeFaceRatio;

    // Outer rim with beveled edge effect
    // Dark outer edge
    g.setColour(juce::Colour(Colors::alumEdge));
    g.fillEllipse(centre.x - outerRadius, centre.y - outerRadius,
                  outerRadius * 2.0f, outerRadius * 2.0f);

    // Main rim body - metallic gradient simulating light from top-left
    float lightX = centre.x + std::cos(kLightAngle) * outerRadius * 0.4f;
    float lightY = centre.y + std::sin(kLightAngle) * outerRadius * 0.4f;

    juce::ColourGradient rimGrad(
        juce::Colour(Colors::alumHighlight), lightX, lightY,
        juce::Colour(Colors::alumDark), centre.x - lightX + centre.x, centre.y - lightY + centre.y,
        true);
    rimGrad.addColour(0.5, juce::Colour(Colors::alumMid));
    g.setGradientFill(rimGrad);

    float rimInner = outerRadius - (outerRadius - faceRadius) * 0.3f;
    g.fillEllipse(centre.x - rimInner, centre.y - rimInner,
                  rimInner * 2.0f, rimInner * 2.0f);

    // Beveled highlight on outer edge (top-left arc)
    juce::Path highlightArc;
    highlightArc.addArc(centre.x - outerRadius + 1, centre.y - outerRadius + 1,
                        (outerRadius - 1) * 2.0f, (outerRadius - 1) * 2.0f,
                        -juce::MathConstants<float>::pi * 0.8f,
                        -juce::MathConstants<float>::pi * 0.3f, true);
    g.setColour(juce::Colour(Colors::highlightSoft));
    g.strokePath(highlightArc, juce::PathStrokeType(2.5f));

    // Shadow arc on bottom-right
    juce::Path shadowArc;
    shadowArc.addArc(centre.x - outerRadius + 1, centre.y - outerRadius + 1,
                     (outerRadius - 1) * 2.0f, (outerRadius - 1) * 2.0f,
                     juce::MathConstants<float>::pi * 0.2f,
                     juce::MathConstants<float>::pi * 0.7f, true);
    g.setColour(juce::Colour(Colors::shadowSoft));
    g.strokePath(shadowArc, juce::PathStrokeType(2.0f));
}

//==============================================================================
// Drawing: Flange Face
//==============================================================================
void TapeReelComponent::drawFlangeFace(juce::Graphics& g, juce::Point<float> centre,
                                        float innerRadius, float outerRadius)
{
    // Main flange face with subtle radial gradient
    float lightX = centre.x + std::cos(kLightAngle) * outerRadius * 0.3f;
    float lightY = centre.y + std::sin(kLightAngle) * outerRadius * 0.3f;

    juce::ColourGradient faceGrad(
        juce::Colour(Colors::alumLight), lightX, lightY,
        juce::Colour(Colors::alumShadow), centre.x - lightX + centre.x, centre.y - lightY + centre.y,
        true);
    faceGrad.addColour(0.4, juce::Colour(Colors::alumMid));
    g.setGradientFill(faceGrad);
    g.fillEllipse(centre.x - outerRadius, centre.y - outerRadius,
                  outerRadius * 2.0f, outerRadius * 2.0f);

    // Inner edge (where tape sits) - darker recessed area
    juce::ColourGradient innerGrad(
        juce::Colour(Colors::alumDark), centre.x, centre.y - innerRadius,
        juce::Colour(Colors::alumShadow), centre.x, centre.y + innerRadius,
        false);
    g.setGradientFill(innerGrad);

    // Draw as a ring
    juce::Path innerRing;
    innerRing.addEllipse(centre.x - innerRadius - 3, centre.y - innerRadius - 3,
                         (innerRadius + 3) * 2.0f, (innerRadius + 3) * 2.0f);
    innerRing.addEllipse(centre.x - innerRadius, centre.y - innerRadius,
                         innerRadius * 2.0f, innerRadius * 2.0f);
    innerRing.setUsingNonZeroWinding(false);
    g.fillPath(innerRing);

    // Inner edge highlight (top)
    juce::Path innerHighlight;
    innerHighlight.addArc(centre.x - innerRadius, centre.y - innerRadius,
                          innerRadius * 2.0f, innerRadius * 2.0f,
                          -juce::MathConstants<float>::pi * 0.9f,
                          -juce::MathConstants<float>::pi * 0.1f, true);
    g.setColour(juce::Colour(Colors::highlightSubtle));
    g.strokePath(innerHighlight, juce::PathStrokeType(1.5f));

    // Inner edge shadow (bottom)
    juce::Path innerShadow;
    innerShadow.addArc(centre.x - innerRadius, centre.y - innerRadius,
                       innerRadius * 2.0f, innerRadius * 2.0f,
                       juce::MathConstants<float>::pi * 0.1f,
                       juce::MathConstants<float>::pi * 0.9f, true);
    g.setColour(juce::Colour(Colors::shadowSubtle));
    g.strokePath(innerShadow, juce::PathStrokeType(1.5f));
}

//==============================================================================
// Drawing: Flange Rings (Decorative)
//==============================================================================
void TapeReelComponent::drawFlangeRings(juce::Graphics& g, juce::Point<float> centre,
                                         float innerRadius, float outerRadius)
{
    // Decorative concentric rings etched into the flange face
    const float ringPositions[] = { 0.2f, 0.45f, 0.7f, 0.9f };
    const int numRings = 4;

    for (int i = 0; i < numRings; ++i)
    {
        float t = ringPositions[i];
        float ringRadius = innerRadius + t * (outerRadius - innerRadius);

        // Etched groove - dark line
        g.setColour(juce::Colour(0x18000000));
        g.drawEllipse(centre.x - ringRadius, centre.y - ringRadius,
                      ringRadius * 2.0f, ringRadius * 2.0f, 0.8f);

        // Light edge below (3D etched effect)
        float hlRadius = ringRadius + 0.8f;
        g.setColour(juce::Colour(0x0cffffff));
        g.drawEllipse(centre.x - hlRadius, centre.y - hlRadius,
                      hlRadius * 2.0f, hlRadius * 2.0f, 0.5f);
    }
}

//==============================================================================
// Drawing: Brushed Metal Texture
//==============================================================================
void TapeReelComponent::drawBrushedMetalTexture(juce::Graphics& g, juce::Point<float> centre,
                                                  float innerRadius, float outerRadius)
{
    // Radial brushed metal texture - fine lines radiating from center
    juce::Random rng(42);  // Fixed seed for consistent texture

    int numStrokes = static_cast<int>((outerRadius - innerRadius) * 1.5f);
    numStrokes = juce::jmin(numStrokes, 120);

    for (int i = 0; i < numStrokes; ++i)
    {
        float angle = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        float startDist = innerRadius + rng.nextFloat() * (outerRadius - innerRadius) * 0.3f;
        float endDist = startDist + 2.0f + rng.nextFloat() * 8.0f;
        endDist = juce::jmin(endDist, outerRadius);

        float x1 = centre.x + std::cos(angle) * startDist;
        float y1 = centre.y + std::sin(angle) * startDist;
        float x2 = centre.x + std::cos(angle) * endDist;
        float y2 = centre.y + std::sin(angle) * endDist;

        // Alternate between dark and light strokes
        if (rng.nextBool())
            g.setColour(juce::Colour(0x08000000));
        else
            g.setColour(juce::Colour(0x06ffffff));

        g.drawLine(x1, y1, x2, y2, 0.6f);
    }
}

//==============================================================================
// Drawing: Ventilation Holes
//==============================================================================
void TapeReelComponent::drawVentilationHoles(juce::Graphics& g, juce::Point<float> centre,
                                              float innerRadius, float outerRadius, float rot)
{
    float holeDistance = (innerRadius + outerRadius) * 0.5f;
    float holeRadius = (outerRadius - innerRadius) * 0.22f;

    for (int i = 0; i < kNumVentHoles; ++i)
    {
        float angle = rot + (i * juce::MathConstants<float>::twoPi / kNumVentHoles);

        float holeX = centre.x + std::cos(angle) * holeDistance;
        float holeY = centre.y + std::sin(angle) * holeDistance;

        // Elliptical holes (wider than tall for perspective)
        float holeW = holeRadius * 2.4f;
        float holeH = holeRadius * 2.0f;

        // Deep dark interior
        juce::ColourGradient holeGrad(
            juce::Colour(Colors::spindleDeep), holeX, holeY - holeH * 0.2f,
            juce::Colour(Colors::spindleInner), holeX, holeY + holeH * 0.3f,
            false);
        g.setGradientFill(holeGrad);
        g.fillEllipse(holeX - holeW * 0.5f, holeY - holeH * 0.5f, holeW, holeH);

        // Beveled edge - shadow on top-left of hole
        juce::Path holeShadow;
        holeShadow.addArc(holeX - holeW * 0.5f, holeY - holeH * 0.5f, holeW, holeH,
                          -juce::MathConstants<float>::pi,
                          -juce::MathConstants<float>::pi * 0.2f, true);
        g.setColour(juce::Colour(Colors::shadowMedium));
        g.strokePath(holeShadow, juce::PathStrokeType(1.5f));

        // Beveled edge - highlight on bottom-right of hole
        juce::Path holeHighlight;
        holeHighlight.addArc(holeX - holeW * 0.5f, holeY - holeH * 0.5f, holeW, holeH,
                             0.0f,
                             juce::MathConstants<float>::pi * 0.8f, true);
        g.setColour(juce::Colour(Colors::highlightSubtle));
        g.strokePath(holeHighlight, juce::PathStrokeType(1.0f));

        // Thin dark rim
        g.setColour(juce::Colour(Colors::alumEdge));
        g.drawEllipse(holeX - holeW * 0.5f, holeY - holeH * 0.5f, holeW, holeH, 0.8f);
    }
}

//==============================================================================
// Drawing: Tape Pack
//==============================================================================
void TapeReelComponent::drawTapePack(juce::Graphics& g, juce::Point<float> centre,
                                      float innerRadius, float outerRadius)
{
    // Main tape body with oxide brown gradient
    float lightX = centre.x + std::cos(kLightAngle) * outerRadius * 0.3f;
    float lightY = centre.y + std::sin(kLightAngle) * outerRadius * 0.3f;

    juce::ColourGradient tapeGrad(
        juce::Colour(Colors::tapeLight), lightX, lightY,
        juce::Colour(Colors::tapeDark), centre.x - lightX + centre.x, centre.y - lightY + centre.y,
        true);
    tapeGrad.addColour(0.5, juce::Colour(Colors::tapeOxide));
    g.setGradientFill(tapeGrad);
    g.fillEllipse(centre.x - outerRadius, centre.y - outerRadius,
                  outerRadius * 2.0f, outerRadius * 2.0f);

    // Cut out center hole (hub area shows through)
    // We'll draw the hub on top, but add a shadow ring here
    g.setColour(juce::Colour(Colors::tapeDark));
    g.drawEllipse(centre.x - innerRadius, centre.y - innerRadius,
                  innerRadius * 2.0f, innerRadius * 2.0f, 2.5f);

    // Tape layer lines - very fine concentric circles suggesting wound layers
    int numLayers = static_cast<int>((outerRadius - innerRadius) / 1.2f);
    numLayers = juce::jmin(numLayers, 40);

    juce::Random rng(789);
    for (int i = 0; i < numLayers; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(numLayers);
        float layerRadius = innerRadius + t * (outerRadius - innerRadius);

        // Slight random color variation for organic look
        float variation = rng.nextFloat() * 0.15f - 0.075f;
        auto layerColor = juce::Colour(Colors::tapeOxide);
        if (variation > 0)
            layerColor = layerColor.brighter(variation);
        else
            layerColor = layerColor.darker(-variation);

        g.setColour(layerColor.withAlpha(0.25f));
        g.drawEllipse(centre.x - layerRadius, centre.y - layerRadius,
                      layerRadius * 2.0f, layerRadius * 2.0f, 0.4f);
    }

    // Subtle surface sheen highlight (top-left)
    juce::Path sheenArc;
    sheenArc.addArc(centre.x - outerRadius * 0.9f, centre.y - outerRadius * 0.9f,
                    outerRadius * 1.8f, outerRadius * 1.8f,
                    -juce::MathConstants<float>::pi * 0.85f,
                    -juce::MathConstants<float>::pi * 0.4f, true);
    g.setColour(juce::Colour(0x12ffffff));
    g.strokePath(sheenArc, juce::PathStrokeType(3.0f));
}

//==============================================================================
// Drawing: Tape Edge
//==============================================================================
void TapeReelComponent::drawTapeEdge(juce::Graphics& g, juce::Point<float> centre, float radius)
{
    // Outer tape edge - slight highlight for the shiny oxide surface
    g.setColour(juce::Colour(Colors::tapeSheen).withAlpha(0.5f));
    g.drawEllipse(centre.x - radius, centre.y - radius,
                  radius * 2.0f, radius * 2.0f, 1.2f);

    // Shadow on bottom edge
    juce::Path edgeShadow;
    edgeShadow.addArc(centre.x - radius, centre.y - radius,
                      radius * 2.0f, radius * 2.0f,
                      juce::MathConstants<float>::pi * 0.15f,
                      juce::MathConstants<float>::pi * 0.85f, true);
    g.setColour(juce::Colour(0x25000000));
    g.strokePath(edgeShadow, juce::PathStrokeType(2.0f));
}

//==============================================================================
// Drawing: Hub
//==============================================================================
void TapeReelComponent::drawHub(juce::Graphics& g, juce::Point<float> centre, float radius, float rot)
{
    if (reelType == ReelType::NAB)
        drawNABHub(g, centre, radius, rot);
    else
        drawCineHub(g, centre, radius, rot);
}

void TapeReelComponent::drawNABHub(juce::Graphics& g, juce::Point<float> centre, float radius, float rot)
{
    // Drop shadow for hub
    g.setColour(juce::Colour(0x40000000));
    g.fillEllipse(centre.x - radius + 2, centre.y - radius + 2,
                  radius * 2.0f, radius * 2.0f);

    // Main hub body - chrome gradient
    float lightX = centre.x + std::cos(kLightAngle) * radius * 0.4f;
    float lightY = centre.y + std::sin(kLightAngle) * radius * 0.4f;

    juce::ColourGradient hubGrad(
        juce::Colour(Colors::chromeHighlight), lightX, lightY,
        juce::Colour(Colors::chromeShadow), centre.x - lightX + centre.x, centre.y - lightY + centre.y,
        true);
    hubGrad.addColour(0.4, juce::Colour(Colors::chromeLight));
    hubGrad.addColour(0.7, juce::Colour(Colors::chromeMid));
    g.setGradientFill(hubGrad);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    // Outer hub ring (beveled edge)
    drawBevelRing(g, centre, radius, 2.0f, true);

    // Draw 3 spokes
    float spokeWidth = radius * 0.32f;
    float spokeInner = radius * 0.35f;
    float spokeOuter = radius * 0.92f;

    for (int i = 0; i < kNumSpokes; ++i)
    {
        float spokeAngle = rot + (i * juce::MathConstants<float>::twoPi / kNumSpokes);

        // Create spoke shape
        juce::Path spoke;
        spoke.addRoundedRectangle(-spokeWidth * 0.5f, -spokeOuter,
                                  spokeWidth, spokeOuter - spokeInner,
                                  spokeWidth * 0.25f);
        spoke.applyTransform(juce::AffineTransform::rotation(spokeAngle)
                                 .translated(centre.x, centre.y));

        // Spoke gradient
        float spokeLightX = centre.x + std::cos(kLightAngle + spokeAngle) * spokeOuter * 0.3f;
        float spokeLightY = centre.y + std::sin(kLightAngle + spokeAngle) * spokeOuter * 0.3f;

        juce::ColourGradient spokeGrad(
            juce::Colour(Colors::chromeHighlight), spokeLightX, spokeLightY,
            juce::Colour(Colors::chromeDark),
            centre.x + std::cos(spokeAngle) * spokeOuter,
            centre.y + std::sin(spokeAngle) * spokeOuter,
            false);
        g.setGradientFill(spokeGrad);
        g.fillPath(spoke);

        // Spoke edge
        g.setColour(juce::Colour(Colors::chromeShadow));
        g.strokePath(spoke, juce::PathStrokeType(0.8f));

        // Spoke highlight edge
        g.setColour(juce::Colour(0x20ffffff));
        g.strokePath(spoke, juce::PathStrokeType(0.5f));
    }

    // Inner hub ring
    float innerRingRadius = radius * 0.4f;
    g.setColour(juce::Colour(Colors::chromeDark));
    g.drawEllipse(centre.x - innerRingRadius, centre.y - innerRingRadius,
                  innerRingRadius * 2.0f, innerRingRadius * 2.0f, 1.5f);
}

void TapeReelComponent::drawCineHub(juce::Graphics& g, juce::Point<float> centre, float radius, float rot)
{
    // Drop shadow
    g.setColour(juce::Colour(0x40000000));
    g.fillEllipse(centre.x - radius + 2, centre.y - radius + 2,
                  radius * 2.0f, radius * 2.0f);

    // Main hub body - chrome gradient
    float lightX = centre.x + std::cos(kLightAngle) * radius * 0.4f;
    float lightY = centre.y + std::sin(kLightAngle) * radius * 0.4f;

    juce::ColourGradient hubGrad(
        juce::Colour(Colors::chromeHighlight), lightX, lightY,
        juce::Colour(Colors::chromeShadow), centre.x - lightX + centre.x, centre.y - lightY + centre.y,
        true);
    hubGrad.addColour(0.4, juce::Colour(Colors::chromeLight));
    hubGrad.addColour(0.7, juce::Colour(Colors::chromeMid));
    g.setGradientFill(hubGrad);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    // Outer ring bevel
    drawBevelRing(g, centre, radius, 2.0f, true);

    // Cutout holes (6 around the perimeter)
    float cutoutRadius = radius * 0.13f;
    float cutoutDistance = radius * 0.62f;

    for (int i = 0; i < 6; ++i)
    {
        float angle = rot + (i * juce::MathConstants<float>::twoPi / 6.0f);
        float cx = centre.x + std::cos(angle) * cutoutDistance;
        float cy = centre.y + std::sin(angle) * cutoutDistance;

        // Dark cutout interior
        juce::ColourGradient cutoutGrad(
            juce::Colour(Colors::spindleDeep), cx, cy - cutoutRadius * 0.3f,
            juce::Colour(Colors::spindleInner), cx, cy + cutoutRadius * 0.3f,
            false);
        g.setGradientFill(cutoutGrad);
        g.fillEllipse(cx - cutoutRadius, cy - cutoutRadius,
                      cutoutRadius * 2.0f, cutoutRadius * 2.0f);

        // Cutout rim
        g.setColour(juce::Colour(Colors::chromeDark));
        g.drawEllipse(cx - cutoutRadius, cy - cutoutRadius,
                      cutoutRadius * 2.0f, cutoutRadius * 2.0f, 1.0f);

        // Inner highlight
        g.setColour(juce::Colour(0x15ffffff));
        juce::Path cutoutHighlight;
        cutoutHighlight.addArc(cx - cutoutRadius, cy - cutoutRadius,
                               cutoutRadius * 2.0f, cutoutRadius * 2.0f,
                               juce::MathConstants<float>::pi * 0.7f,
                               juce::MathConstants<float>::pi * 1.3f, true);
        g.strokePath(cutoutHighlight, juce::PathStrokeType(0.8f));
    }

    // Inner ring
    float innerRingRadius = radius * 0.38f;
    g.setColour(juce::Colour(Colors::chromeDark));
    g.drawEllipse(centre.x - innerRingRadius, centre.y - innerRingRadius,
                  innerRingRadius * 2.0f, innerRingRadius * 2.0f, 1.5f);
}

//==============================================================================
// Drawing: Center Spindle
//==============================================================================
void TapeReelComponent::drawSpindle(juce::Graphics& g, juce::Point<float> centre, float radius)
{
    // Outer spindle rim
    juce::ColourGradient rimGrad(
        juce::Colour(Colors::spindleOuter), centre.x, centre.y - radius,
        juce::Colour(Colors::spindleInner), centre.x, centre.y + radius,
        false);
    g.setGradientFill(rimGrad);
    g.fillEllipse(centre.x - radius, centre.y - radius,
                  radius * 2.0f, radius * 2.0f);

    // Deep interior
    float innerRadius = radius * 0.7f;
    juce::ColourGradient innerGrad(
        juce::Colour(Colors::spindleInner), centre.x, centre.y - innerRadius * 0.5f,
        juce::Colour(Colors::spindleDeep), centre.x, centre.y + innerRadius * 0.5f,
        false);
    g.setGradientFill(innerGrad);
    g.fillEllipse(centre.x - innerRadius, centre.y - innerRadius,
                  innerRadius * 2.0f, innerRadius * 2.0f);

    // Rim highlight (top)
    juce::Path rimHighlight;
    rimHighlight.addArc(centre.x - radius, centre.y - radius,
                        radius * 2.0f, radius * 2.0f,
                        -juce::MathConstants<float>::pi * 0.8f,
                        -juce::MathConstants<float>::pi * 0.2f, true);
    g.setColour(juce::Colour(0x25ffffff));
    g.strokePath(rimHighlight, juce::PathStrokeType(1.2f));

    // Small dome reflection highlight in center
    float highlightRadius = radius * 0.25f;
    float highlightX = centre.x - radius * 0.2f;
    float highlightY = centre.y - radius * 0.25f;
    g.setColour(juce::Colour(0x18ffffff));
    g.fillEllipse(highlightX - highlightRadius, highlightY - highlightRadius * 0.6f,
                  highlightRadius * 2.0f, highlightRadius * 1.2f);
}

//==============================================================================
// Drawing: Center Label
//==============================================================================
void TapeReelComponent::drawCenterLabel(juce::Graphics& g, juce::Point<float> centre, float radius)
{
    if (labelText.isEmpty())
        return;

    // Label background (cream colored paper look)
    juce::ColourGradient labelGrad(
        juce::Colour(Colors::labelBg), centre.x, centre.y - radius,
        juce::Colour(Colors::labelBgDark), centre.x, centre.y + radius,
        false);
    g.setGradientFill(labelGrad);
    g.fillEllipse(centre.x - radius, centre.y - radius,
                  radius * 2.0f, radius * 2.0f);

    // Label border (subtle gold ring)
    g.setColour(juce::Colour(Colors::labelBorder));
    g.drawEllipse(centre.x - radius, centre.y - radius,
                  radius * 2.0f, radius * 2.0f, 1.0f);

    // Inner decorative ring
    float innerRing = radius * 0.75f;
    g.setColour(juce::Colour(Colors::labelBorder).withAlpha(0.5f));
    g.drawEllipse(centre.x - innerRing, centre.y - innerRing,
                  innerRing * 2.0f, innerRing * 2.0f, 0.5f);

    // Label text
    float fontSize = radius * 1.1f;
    g.setFont(juce::Font(fontSize, juce::Font::bold));
    g.setColour(juce::Colour(Colors::labelText));

    juce::Rectangle<float> textArea(centre.x - radius, centre.y - radius * 0.5f,
                                     radius * 2.0f, radius);
    g.drawText(labelText, textArea, juce::Justification::centred);
}

//==============================================================================
// Drawing: Light Reflections
//==============================================================================
void TapeReelComponent::drawLightReflections(juce::Graphics& g, juce::Point<float> centre,
                                              float radius, float rot)
{
    // Primary specular highlight (stationary, simulating overhead light)
    juce::Path primaryHighlight;
    float hlAngle = kLightAngle;
    primaryHighlight.addArc(centre.x - radius * 0.94f, centre.y - radius * 0.94f,
                            radius * 1.88f, radius * 1.88f,
                            hlAngle - 0.25f, hlAngle + 0.25f, true);
    g.setColour(juce::Colour(Colors::highlightSoft));
    g.strokePath(primaryHighlight, juce::PathStrokeType(4.0f));

    // Secondary highlight (slightly offset)
    juce::Path secondaryHighlight;
    secondaryHighlight.addArc(centre.x - radius * 0.88f, centre.y - radius * 0.88f,
                              radius * 1.76f, radius * 1.76f,
                              hlAngle - 0.15f, hlAngle + 0.15f, true);
    g.setColour(juce::Colour(Colors::highlightBright));
    g.strokePath(secondaryHighlight, juce::PathStrokeType(2.0f));

    // Rotating subtle highlight (follows spoke positions for subtle movement)
    if (reelType == ReelType::NAB)
    {
        for (int i = 0; i < kNumSpokes; ++i)
        {
            float spokeAngle = rot + (i * juce::MathConstants<float>::twoPi / kNumSpokes);

            // Only draw highlight when spoke is near the light source angle
            float angleDiff = std::abs(spokeAngle - (kLightAngle + juce::MathConstants<float>::twoPi));
            while (angleDiff > juce::MathConstants<float>::pi)
                angleDiff = std::abs(angleDiff - juce::MathConstants<float>::twoPi);

            if (angleDiff < 0.5f)
            {
                float intensity = 1.0f - (angleDiff / 0.5f);
                juce::Path spokeHighlight;
                spokeHighlight.addArc(centre.x - radius * 0.5f, centre.y - radius * 0.5f,
                                      radius, radius,
                                      spokeAngle - 0.1f, spokeAngle + 0.1f, true);
                g.setColour(juce::Colour(0xffffffff).withAlpha(intensity * 0.15f));
                g.strokePath(spokeHighlight, juce::PathStrokeType(2.0f));
            }
        }
    }
}

//==============================================================================
// Helper: Create Metallic Gradient
//==============================================================================
juce::ColourGradient TapeReelComponent::createMetallicGradient(juce::Point<float> centre, float radius,
                                                                 juce::Colour baseColor, float highlightIntensity)
{
    float lightX = centre.x + std::cos(kLightAngle) * radius * 0.4f;
    float lightY = centre.y + std::sin(kLightAngle) * radius * 0.4f;

    juce::Colour highlight = baseColor.brighter(highlightIntensity);
    juce::Colour shadow = baseColor.darker(highlightIntensity * 0.7f);

    juce::ColourGradient grad(
        highlight, lightX, lightY,
        shadow, centre.x - lightX + centre.x, centre.y - lightY + centre.y,
        true);
    grad.addColour(0.5, baseColor);

    return grad;
}

//==============================================================================
// Helper: Draw Bevel Ring
//==============================================================================
void TapeReelComponent::drawBevelRing(juce::Graphics& g, juce::Point<float> centre, float radius,
                                       float thickness, bool raised)
{
    // Top-left highlight arc
    juce::Path highlightArc;
    highlightArc.addArc(centre.x - radius, centre.y - radius,
                        radius * 2.0f, radius * 2.0f,
                        -juce::MathConstants<float>::pi * 0.85f,
                        -juce::MathConstants<float>::pi * 0.15f, true);
    g.setColour(raised ? juce::Colour(Colors::highlightSoft) : juce::Colour(Colors::shadowSoft));
    g.strokePath(highlightArc, juce::PathStrokeType(thickness));

    // Bottom-right shadow arc
    juce::Path shadowArc;
    shadowArc.addArc(centre.x - radius, centre.y - radius,
                     radius * 2.0f, radius * 2.0f,
                     juce::MathConstants<float>::pi * 0.15f,
                     juce::MathConstants<float>::pi * 0.85f, true);
    g.setColour(raised ? juce::Colour(Colors::shadowSoft) : juce::Colour(Colors::highlightSoft));
    g.strokePath(shadowArc, juce::PathStrokeType(thickness));
}
