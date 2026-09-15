// Focused, framework-free regressions for DuskVerb's visible-unit value text.
#include "DuskVerbFormat.hpp"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}

bool near(float a, float b, float tolerance = 0.002f)
{
    return std::fabs(a - b) <= tolerance;
}

void parseIs(EngineType e, int p, const char* text, float current, float expected)
{
    float actual = -999.0f;
    check(duskverb::ui::parsePlainValue(e, p, text, current, actual), text);
    if (!near(actual, expected))
        std::printf("typed detail: '%s' -> %.9g expected %.9g\n", text, actual, expected);
    check(near(actual, expected), "typed value maps to the expected plain parameter");
}

void testTypedExamples()
{
    using namespace duskverb;
    using namespace duskverb::ui;
    parseIs(EngineType::FDN, Predelay, "0.05 s", 0.0f, 50.0f);
    parseIs(EngineType::NonLinear, ModDepth, "0.025 s", 0.5f, 24.0f / 49.0f);
    parseIs(EngineType::FDN, Mix, "50%", 0.5f, 0.5f);
    parseIs(EngineType::FDN, Decay, "500", 0.5f, 0.5f);       // current visible ms
    parseIs(EngineType::FDN, Decay, "2", 2.0f, 2.0f);         // current visible s
    parseIs(EngineType::FDN, HiCut, "2", 2000.0f, 2000.0f); // current visible kHz
    parseIs(EngineType::FDN, HiCut, "2 kHz", 2000.0f, 2000.0f);
    parseIs(EngineType::Shimmer, ModDepth, "12st", 0.5f, 0.5f);
    parseIs(EngineType::Shimmer, ModDepth, "12 semitones", 0.5f, 0.5f);
    parseIs(EngineType::NonLinear, ModDepth, "25ms", 0.5f, 24.0f / 49.0f);
    parseIs(EngineType::NonLinear, MidMult, "-30 dB", 1.0f, 0.8f);

    float ignored = 0.0f;
    check(!parsePlainValue(EngineType::FDN, Mix, "50 Hz", 0.5f, ignored), "incompatible suffix is rejected");
    check(!parsePlainValue(EngineType::FDN, Mix, "", 0.5f, ignored), "empty input is rejected");
    check(!parsePlainValue(EngineType::FDN, Mix, "1e999", 0.5f, ignored), "overflow is rejected");
    check(!parsePlainValue(EngineType::FDN, Mix, "nan", 0.5f, ignored), "NaN text is rejected");
    check(!parsePlainValue(EngineType::FDN, Mix, "12junk", 0.5f, ignored), "partial junk is rejected");
    check(parsePlainValue(EngineType::FDN, Mix, "999%", 0.5f, ignored) && ignored == 1.0f,
          "valid out-of-range physical input is clamped");
}

void testRoundTrips()
{
    using namespace duskverb;
    using namespace duskverb::ui;
    const EngineType engines[] = { EngineType::FDN, EngineType::Spring,
                                   EngineType::Shimmer, EngineType::NonLinear };
    for (EngineType e : engines)
        for (int p = 0; p < kNumParams; ++p)
        {
            const auto& d = paramDesc(p);
            const float plain = d.min + (d.max - d.min) * 0.37f;
            char formatted[96];
            formatPlainValue(e, p, plain, formatted, sizeof(formatted));
            float parsed = -999.0f;
            if (!parsePlainValue(e, p, formatted, plain, parsed))
            {
                std::printf("rejected formatted text: engine %d param %d: %s\n", static_cast<int>(e), p, formatted);
                check(false, "every parameter accepts its own formatted text");
            }
            const float shown = displayNumber(e, p, plain);
            const float restored = plainFromDisplayNumber(e, p, shown, plain);
            if (!(duskdaf::value_text::finiteFloat(restored) && near(restored, plain, 0.001f)))
            {
                std::printf("round-trip detail: engine %d param %d plain %.9g shown %.9g restored %.9g\n",
                            static_cast<int>(e), p, plain, shown, restored);
                check(false, "display/inverse round trip");
            }
        }

    char text[48];
    for (int parameter : {MonoBelow, HiCut, Decay})
        for (float host : {0.5348837209302325f, 0.0f, 0.5f, 1.0f})
        {
            const auto& desc = paramDesc(parameter);
            const float plain = dspFromHost(desc, host);
            char first[48], second[48];
            formatPlainValue(EngineType::FDN, parameter, plain, first, sizeof(first));
            float parsed = 0.0f;
            check(parsePlainValue(EngineType::FDN, parameter, first, plain, parsed), "boundary text parses");
            formatPlainValue(EngineType::FDN, parameter,
                dspFromHost(desc, plainToHost(desc, parsed)), second, sizeof(second));
            check(std::strcmp(first, second) == 0, "host text roundtrip preserves formatting at unit boundaries");
        }
    formatPlainValue(EngineType::FDN, Decay, 0.689f, text, sizeof(text));
    check(std::strcmp(text, "689 ms") == 0, "existing decay readout remains exact");
    formatPlainValue(EngineType::FDN, HiCut, 17597.19f, text, sizeof(text));
    check(std::strcmp(text, "17.60 kHz") == 0, "existing frequency readout remains exact");
    formatPlainValue(EngineType::Shimmer, ModDepth, 0.5f, text, sizeof(text));
    check(std::strcmp(text, "+12 st") == 0, "existing shimmer readout remains exact");
}

void testLocaleMatrix()
{
    const char* old = std::setlocale(LC_NUMERIC, nullptr);
    char saved[64] = {};
    if (old != nullptr) std::snprintf(saved, sizeof(saved), "%s", old);
    const char* locales[] = { "C", "en_US.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8" };
    for (const char* locale : locales)
    {
        if (std::setlocale(LC_NUMERIC, locale) == nullptr) continue;
        char text[48];
        duskverb::ui::formatPlainValue(EngineType::FDN, duskverb::GainTrim,
                                       1.9f, text, sizeof(text));
        check(std::strcmp(text, "1.9 dB") == 0, "formatting uses dot independent of locale");
        float value = 0.0f;
        check(duskverb::ui::parsePlainValue(EngineType::FDN, duskverb::GainTrim,
                                            "1.9 dB", 1.9f, value) && near(value, 1.9f, 0.0001f),
              "parsing uses dot independent of locale");
    }
    if (saved[0] != '\0') std::setlocale(LC_NUMERIC, saved);
}
} // namespace

int main()
{
    testTypedExamples();
    testRoundTrips();
    testLocaleMatrix();
    std::printf("DuskVerbValueTextTests: %s (%d failures)\n",
                failures == 0 ? "all checks passed" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
