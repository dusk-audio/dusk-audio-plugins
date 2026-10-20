// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once

// Explicit output marker for our opt-in clap-juce adapter. JUCE's meter category
// also forces AU logarithmic gain units, which are wrong for categorical/text
// analysis outputs. Leave the category/range/text intact and mark only the
// CLAP metadata read-only. Engine setValueNotifyingHost remains available.
struct DuskClapOutputParameterTag
{
    virtual ~DuskClapOutputParameterTag() = default;
};

template<class Parameter>
class DuskClapOutputParameter final : public Parameter, public DuskClapOutputParameterTag
{
public:
    using Parameter::Parameter;
    bool isAutomatable() const override { return false; }
};
