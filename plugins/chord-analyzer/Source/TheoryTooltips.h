#pragma once

#include "ChordAnalyzer.h"
#include <juce_core/juce_core.h>

//==============================================================================
// Header-only class for music theory educational tooltips
class TheoryTooltips
{
public:
    //==========================================================================
    // Chord quality explanations
    static juce::String getChordExplanation(ChordQuality quality)
    {
        switch (quality)
        {
            case ChordQuality::Major:
                return "Major chords have a bright, happy sound. Built from root, major 3rd (4 semitones), and perfect 5th (7 semitones).";

            case ChordQuality::Minor:
                return "Minor chords have a darker, more melancholic sound. Built from root, minor 3rd (3 semitones), and perfect 5th (7 semitones).";

            case ChordQuality::Diminished:
                return "Diminished chords sound tense and unstable. Built from root, minor 3rd, and diminished 5th (6 semitones). Often used as passing chords.";

            case ChordQuality::Augmented:
                return "Augmented chords have a mysterious, unresolved quality. Built from root, major 3rd, and augmented 5th (8 semitones).";

            case ChordQuality::Dominant7:
                return "Dominant 7th chords create tension that wants to resolve. Built by adding a minor 7th to a major triad. The most common chord to precede the tonic.";

            case ChordQuality::Major7:
                return "Major 7th chords have a jazzy, sophisticated sound. Built by adding a major 7th to a major triad. Common in jazz and R&B.";

            case ChordQuality::Minor7:
                return "Minor 7th chords are warm and versatile. Built by adding a minor 7th to a minor triad. Essential in jazz and pop music.";

            case ChordQuality::MinorMajor7:
                return "Minor-major 7th chords have a dramatic, film-noir quality. Combines a minor triad with a major 7th - an unusual but expressive sound.";

            case ChordQuality::Diminished7:
                return "Fully diminished 7th chords are highly unstable and symmetrical (all minor 3rds). Each note can function as the root, enabling smooth chromatic movement.";

            case ChordQuality::HalfDiminished7:
                return "Half-diminished 7th chords (m7b5) are used as the ii chord in minor keys. Less dissonant than fully diminished - a staple of jazz harmony.";

            case ChordQuality::Augmented7:
                return "Augmented 7th chords combine augmented triad tension with dominant 7th tension. Often used as an altered dominant approaching resolution.";

            case ChordQuality::Sus2:
                return "Suspended 2nd chords replace the 3rd with a 2nd, creating an open, ambiguous sound. Neither major nor minor - great for modern pop.";

            case ChordQuality::Sus4:
                return "Suspended 4th chords replace the 3rd with a 4th. Creates tension that typically resolves to a major chord. Common in rock and pop.";

            case ChordQuality::Dominant7Sus4:
                return "Combines the suspension of sus4 with dominant 7th tension. Often resolves to a standard dominant 7th or directly to tonic.";

            case ChordQuality::Add9:
                return "Add9 chords add color by including the 9th without the 7th. Brighter than maj9 - popular in pop and acoustic music.";

            case ChordQuality::Major6:
                return "Major 6th chords add the 6th to a major triad. A classic jazz sound, often used in place of major 7th for a warmer tone.";

            case ChordQuality::Minor6:
                return "Minor 6th chords add the 6th to a minor triad. A sophisticated jazz chord with a slightly bittersweet quality.";

            case ChordQuality::Major9:
                return "Major 9th chords extend the major 7th with a 9th. Rich and lush - a cornerstone of jazz ballads and neo-soul.";

            case ChordQuality::Minor9:
                return "Minor 9th chords are smooth and sophisticated. The 9th adds warmth to the minor 7th chord - essential in R&B and jazz.";

            case ChordQuality::Dominant9:
                return "Dominant 9th chords add the 9th to a dominant 7th. More colorful than plain V7 - common in funk, jazz, and blues.";

            case ChordQuality::Major11:
            case ChordQuality::Minor11:
            case ChordQuality::Dominant11:
                return "11th chords include the root, 3rd, 5th, 7th, 9th, and 11th. Very rich and complex - often the 3rd is omitted to avoid dissonance with the 11th.";

            case ChordQuality::Major13:
            case ChordQuality::Minor13:
            case ChordQuality::Dominant13:
                return "13th chords are the most extended tertian harmonies. Includes all scale tones - incredibly rich and used in jazz and sophisticated pop.";

            case ChordQuality::Power5:
                return "Power chords contain only root and 5th (no 3rd). Ambiguous quality makes them versatile - the backbone of rock and metal guitar.";

            case ChordQuality::Dominant7Flat5:
                return "The b5 alteration creates a tritone between root and 5th, adding more tension to the dominant chord.";

            case ChordQuality::Dominant7Sharp5:
                return "The #5 (same as augmented 5th) increases tension in the dominant chord. Common in jazz as an altered dominant.";

            case ChordQuality::Dominant7Flat9:
                return "The b9 adds a dark, dissonant color to the dominant chord. Classic in jazz and often resolves to minor.";

            case ChordQuality::Dominant7Sharp9:
                return "The #9 (the 'Hendrix chord') combines major 3rd and minor 3rd sounds. Bluesy, gritty tension that defined rock.";

            default:
                return "A chord built from stacked intervals. The quality determines its emotional character.";
        }
    }

    //==========================================================================
    // Harmonic function explanations
    static juce::String getFunctionExplanation(HarmonicFunction func)
    {
        switch (func)
        {
            case HarmonicFunction::Tonic:
                return "TONIC function provides stability and rest. The I chord is 'home' - other tonic-function chords (vi, iii) offer softer arrival points.";

            case HarmonicFunction::Subdominant:
                return "SUBDOMINANT function creates gentle movement away from tonic. IV and ii chords prepare the ear for dominant or can return directly to tonic (plagal cadence).";

            case HarmonicFunction::Dominant:
                return "DOMINANT function creates maximum tension seeking resolution to tonic. V and vii contain the leading tone, creating strong pull toward the tonic.";

            case HarmonicFunction::SecondaryDom:
                return "SECONDARY DOMINANT: A chord that acts as V of a chord other than tonic. Adds chromatic color and can tonicize other scale degrees temporarily.";

            case HarmonicFunction::Borrowed:
                return "BORROWED CHORD: Taken from the parallel major or minor key. Modal interchange adds color without fully modulating to another key.";

            case HarmonicFunction::Chromatic:
                return "CHROMATIC chord: Outside the diatonic scale. Could be a passing chord, chromatic mediant, or preparation for modulation.";

            default:
                return "This chord's harmonic function depends on context.";
        }
    }

    //==========================================================================
    // Roman numeral explanations
    static juce::String getRomanNumeralExplanation(const juce::String& numeral)
    {
        // Strip any accidentals and extensions for matching
        juce::String base = numeral.toUpperCase()
                                   .replace("B", "")
                                   .replace("#", "")
                                   .replace("7", "")
                                   .replace("9", "")
                                   .replace("11", "")
                                   .replace("13", "")
                                   .replace("M", "")
                                   .replace("SUS", "")
                                   .replace(juce::String::fromUTF8("\xC2\xB0"), "")
                                   .replace(juce::String::fromUTF8("\xC3\xB8"), "")
                                   .replace("+", "");

        if (base == "I")
            return "I (one) - The TONIC chord. Home base of the key. Most stable, most final. Progressions typically start and end here.";

        if (base == "II")
            return "ii (two) - The SUPERTONIC chord. Minor in major keys. Classic pre-dominant - sets up the V chord beautifully. The ii-V-I is the most common jazz progression.";

        if (base == "III")
            return "iii (three) - The MEDIANT chord. Minor in major keys. Connects I and IV/V smoothly. Less common but adds interest.";

        if (base == "IV")
            return "IV (four) - The SUBDOMINANT chord. Major in major keys. Creates gentle motion - think \"Amen\" (plagal cadence). One of the most used chords in pop.";

        if (base == "V")
            return "V (five) - The DOMINANT chord. Creates maximum tension wanting to resolve to I. Contains the leading tone. The V-I is the strongest resolution in tonal music.";

        if (base == "VI")
            return "vi (six) - The SUBMEDIANT chord. Minor in major keys. Relative minor of the tonic - used for deceptive cadences and the famous I-V-vi-IV pop progression.";

        if (base == "VII")
            return "vii (seven) - The LEADING TONE chord. Diminished in major keys. Shares notes with V7 and has similar dominant function. Often moves to I or iii.";

        return "Roman numerals show chord function within a key. Uppercase = major quality, lowercase = minor quality.";
    }

    //==========================================================================
    // Suggestion category explanations
    static juce::String getCategoryExplanation(SuggestionCategory category)
    {
        switch (category)
        {
            case SuggestionCategory::Basic:
                return "BASIC: Common, expected progressions that work in most styles. Safe choices that follow traditional voice leading.";

            case SuggestionCategory::Intermediate:
                return "INTERMEDIATE: Secondary dominants, borrowed chords, and modal interchange. Adds color while staying accessible.";

            case SuggestionCategory::Advanced:
                return "ADVANCED: Chromatic mediants, tritone substitutions, and extended harmonies. For jazz, film scores, and adventurous writing.";

            default:
                return "";
        }
    }

    //==========================================================================
    // General progression tips
    static juce::String getProgressionTip(int fromDegree, int toDegree)
    {
        if (fromDegree == 5 && toDegree == 1)
            return "V to I: The PERFECT CADENCE - strongest resolution in tonal music. Use at phrase endings for definitive closure.";

        if (fromDegree == 4 && toDegree == 1)
            return "IV to I: The PLAGAL CADENCE (\"Amen\" cadence). Softer resolution than V-I. Common in gospel, hymns, and as a final tag.";

        if (fromDegree == 2 && toDegree == 5)
            return "ii to V: Classic PRE-DOMINANT motion. The ii chord smoothly leads to V, creating the foundation of the ii-V-I progression.";

        if (fromDegree == 5 && toDegree == 6)
            return "V to vi: The DECEPTIVE CADENCE. Surprises the ear by avoiding the expected tonic. Great for extending phrases.";

        if (fromDegree == 1 && toDegree == 4)
            return "I to IV: Classic opening motion. Moving away from home creates forward momentum. Foundation of countless songs.";

        if (fromDegree == 6 && toDegree == 4)
            return "vi to IV: Part of the famous four-chord progression (I-V-vi-IV or I-IV-vi-IV). Timeless pop formula.";

        if (fromDegree == 1 && toDegree == 5)
            return "I to V: Direct move to dominant creates immediate tension and expectation. Sets up the return home.";

        if (fromDegree == 4 && toDegree == 5)
            return "IV to V: Strong pre-dominant to dominant motion. Building toward resolution.";

        return "Consider voice leading - which notes connect smoothly between these chords?";
    }

    //==========================================================================
    // Key context tips
    static juce::String getKeyTip(bool isMinor)
    {
        if (isMinor)
        {
            return "MINOR KEY: Natural minor (Aeolian) is the default. The V chord is often major (harmonic minor) for stronger resolution. The bVII and bIII are common borrowed chords.";
        }
        else
        {
            return "MAJOR KEY: The most common key in Western music. The ii-V-I progression is foundational. Borrowing from parallel minor (iv, bVII, bVI) adds color.";
        }
    }

    //==========================================================================
    // Inversion tips
    static juce::String getInversionTip(int inversion)
    {
        switch (inversion)
        {
            case 0:
                return "ROOT POSITION: The root is in the bass. Most stable voicing with the strongest fundamental.";
            case 1:
                return "FIRST INVERSION: The 3rd is in the bass. Lighter sound, good for smooth bass lines and passing motion.";
            case 2:
                return "SECOND INVERSION: The 5th is in the bass. Less stable - classically used for cadential 6/4, neighbor, or passing motion.";
            case 3:
                return "THIRD INVERSION: The 7th is in the bass (for 7th chords). Creates strong voice leading tendency down by step.";
            default:
                return "";
        }
    }

private:
    TheoryTooltips() = delete;  // Static class - no instantiation
};
