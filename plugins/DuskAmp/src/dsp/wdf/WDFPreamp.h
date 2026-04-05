// WDFPreamp.h — Abstract interface for WDF-based preamp circuit models.
// Each amp model implements this interface with its specific circuit topology.

#pragma once

class WDFPreamp
{
public:
    virtual ~WDFPreamp() = default;

    virtual void prepare (double sampleRate) = 0;
    virtual void reset() = 0;
    virtual void setGain (float gain01) = 0;
    virtual void setBright (bool on) = 0;
    virtual void process (float* buffer, int numSamples) = 0;
};
