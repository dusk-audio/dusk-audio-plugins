// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later.
#include "MultiCompDSP.hpp"
#include "MultiCompBusMeterFixtures.hpp"
#include <cstdio>
#include <vector>
#include <cstring>
using DSP = duskaudio::MultiCompDSP;
using P = DSP::Parameter;

static void setup(DSP& d, bool holdout = false, double rate = 48000, int block = 240)
{
    d.setMode(3); d.setOversampling(1);
    d.setParameter(P::BusThreshold, holdout ? -6 : 0);
    d.setParameter(P::BusRatio, holdout ? 2 : 1);
    d.setParameter(P::BusAttack, holdout ? 0 : 2);
    d.setParameter(P::BusRelease, holdout ? 1 : 0);
    d.setParameter(P::BusHeadroom, 3);
    d.setParameter(P::NoiseEnable, 0); d.setParameter(P::TruePeakEnable, 0);
    d.setParameter(P::AutoMakeup, 0); d.setParameter(P::SidechainHP, 0);
    d.prepare(rate, block);
}
static void block(DSP& d, int start, int n, bool holdout, double rate = 48000,
                  int channels = 2, bool external = false)
{
    std::vector<float> l(n), r(n), ol(n), other(n);
    constexpr float levels[] = {-36,-24,-18,-12,-6};
    for (int i = 0; i < n; ++i)
    {
        const double t = (start + i) / rate;
        const float db = start < 0 ? -120.0f : holdout
            ? levels[std::clamp(static_cast<int>(t / 2), 0, 4)]
            : t >= 1 && t < 5 ? -12 : -54;
        l[i] = r[i] = std::pow(10.0f, db / 20) * std::sin(2 * 3.141592653589793 * 1000 * t + .37);
    }
    const float* in[] = {l.data(), r.data()}; float* out[] = {ol.data(), other.data()};
    if (external) d.processBlockExternal(in, in, out, channels, n);
    else d.processBlock(in, out, channels, n);
}
template<size_t N>
static bool reference(const bus_meter_reference::Point (&points)[N], bool holdout)
{
    DSP d; setup(d, holdout);
    for (int i = 0; i < 1000; ++i) block(d, -240, 240, holdout);
    const int count = holdout ? 2000 : 1600;
    std::vector<float> readings(count + 1); double squares = 0, worst = 0, windowWorst = 0;
    for (int i = 0; i < count; ++i)
    { block(d, i * 240, 240, holdout); readings[i + 1] = d.getBusMeterReading(); }
    for (const auto& p : points)
    {
        const double index = p.seconds * 200;
        const int i = std::clamp(static_cast<int>(index), 0, count - 1);
        const double value = readings[i] + (readings[i + 1] - readings[i]) * (index - i);
        const double error = value - p.db; squares += error * error; worst = std::max(worst, std::abs(error));
        float lo = 100, hi = -100;
        for (int j = std::max(0, static_cast<int>(std::ceil(index - 5)));
             j <= std::min(count, static_cast<int>(std::floor(index + 5))); ++j)
        { lo = std::min(lo, readings[j]); hi = std::max(hi, readings[j]); }
        windowWorst = std::max(windowWorst, double(std::max({0.0f, lo - p.db, p.db - hi})));
    }
    const double rms = std::sqrt(squares / N);
    const bool ok = rms < .3 && windowWorst < .6;
    std::printf("%s native meter %s: %zu points RMS %.6f dB, raw max %.6f, 25ms-window max %.6f\n",
                ok ? "PASS" : "FAIL", holdout ? "independent 10:1" : "4:1 step", N, rms, worst, windowWorst);
    return ok;
}
static bool lifecycle(const char* action)
{
    DSP d; setup(d);
    for (int i = 0; i < 400; ++i) block(d, 48000 + i * 240, 240, false);
    const float active = d.getBusMeterReading();
    if (!std::strcmp(action, "reset")) d.reset();
    else if (!std::strcmp(action, "prepare")) d.prepare(48000, 240);
    else if (!std::strcmp(action, "mode")) { d.setMode(2); block(d, 48000, 240, false); }
    else if (!std::strcmp(action, "listen")) { d.setParameter(P::GlobalSidechainListen, 1); block(d, 48000, 240, false); }
    else { d.setBypass(true); for (int i = 0; i < 400; ++i) block(d, 48000, 240, false); }
    if (!std::strcmp(action, "reset") || !std::strcmp(action, "prepare"))
        block(d, -240, 240, false); // Expose retained velocity/position after publication was cleared.
    const float cleared = d.getBusMeterReading();
    const bool ok = active > 9 && cleared < .001f;
    std::printf("%s meter %s active %.6f -> %.9f\n", ok ? "PASS" : "FAIL", action, active, cleared);
    return ok;
}
static bool rates()
{
    float values[3]{}; int k = 0;
    for (double rate : {44100.,48000.,96000.})
    {
        duskaudio::busLaw::CompressionMeter m; m.prepare(rate);
        for (int i = 0; i < static_cast<int>(rate * .1); ++i) m.process(10);
        values[k++] = m.reading();
    }
    const float difference = *std::max_element(values, values + 3) - *std::min_element(values, values + 3);
    const bool ok = values[0] > 7 && difference < .00001f;
    std::printf("%s meter rates %.9f %.9f %.9f, delta %.9f\n", ok ? "PASS" : "FAIL",values[0],values[1],values[2],difference);
    return ok;
}
static bool paths()
{
    bool ok = true; float worst = 0;
    for (bool external : {false, true}) for (int channels : {1,2}) for (int link : {0,100})
    {
        DSP a,b; setup(a,false,48000,240); setup(b,false,48000,60);
        for (DSP* d : {&a,&b}) { d->setStereoLink(link); d->setExternalSidechain(external); }
        for (int i = 0; i < 300; ++i)
        {
            block(a,48000+i*240,240,false,48000,channels,external);
            for(int j=0;j<4;++j) block(b,48000+i*240+j*60,60,false,48000,channels,external);
        }
        const float delta = std::abs(a.getBusMeterReading()-b.getBusMeterReading());
        worst = std::max(worst,delta); ok = ok && a.getBusMeterReading()>9 && delta<.00001f;
    }
    std::printf("%s meter mono/stereo, internal/external, linked/unlinked, block-size delta %.9f\n",ok?"PASS":"FAIL",worst);
    return ok;
}
int main(int argc,char**argv)
{
    bool ok=true; const char* which=argc>1?argv[1]:"all";
    auto run=[&](const char* name){return !std::strcmp(which,"all")||!std::strcmp(which,name);};
    if(run("reference")) { ok=reference(bus_meter_reference::step,false)&&ok; ok=reference(bus_meter_reference::holdout,true)&&ok; }
    for(const char* action:{"reset","prepare","mode","listen","bypass"}) if(run(action))ok=lifecycle(action)&&ok;
    if(run("rates"))ok=rates()&&ok;
    if(run("paths"))ok=paths()&&ok;
    return ok?0:1;
}
