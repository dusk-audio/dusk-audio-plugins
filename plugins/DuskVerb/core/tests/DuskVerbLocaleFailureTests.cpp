// Force locale creation failure without depending on machine locale resources.
#include <locale.h>
#include <cstdio>
static int creations = 0;
#if defined(_WIN32)
static _locale_t failLocale(int, const char*) { ++creations; return nullptr; }
#define _create_locale failLocale
#else
static locale_t failLocale(int, const char*, locale_t) { ++creations; return nullptr; }
#define newlocale failLocale
#endif
#include "DuskVerbTextParse.hpp"
int main()
{
    duskverb::primeParseLocale();
    const bool primed = creations == 1;
    const bool parsed = duskverb::parseFloat("1.25") == 1.25f
        && duskverb::parseFloat("invalid") == 0.0f
        && duskverb::parseFloat(nullptr) == 0.0f;
    const bool once = creations == 1;
    std::printf("%s failed locale creation is primed once and parsing falls back safely\n",
                primed && parsed && once ? "PASS" : "FAIL");
    return primed && parsed && once ? 0 : 1;
}
