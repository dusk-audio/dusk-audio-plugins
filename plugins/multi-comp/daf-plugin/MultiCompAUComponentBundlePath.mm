#include <src/DafDefines.h>

#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>

#include <cstdio>
#include <cstring>

START_NAMESPACE_DAF
extern const char* d_nextBundlePath;
END_NAMESPACE_DAF

namespace
{
char componentBundlePath[PATH_MAX]{};

__attribute__((constructor))
void setMultiCompAUComponentBundlePath()
{
    Dl_info imageInfo{};
    if (dladdr(reinterpret_cast<const void*>(&setMultiCompAUComponentBundlePath),
               &imageInfo) == 0
        || imageInfo.dli_fname == nullptr)
        return;

    if (realpath(imageInfo.dli_fname, componentBundlePath) == nullptr)
        std::snprintf(componentBundlePath, sizeof(componentBundlePath), "%s",
                      imageInfo.dli_fname);

    constexpr const char* contentsMarker = "/Contents/MacOS/";
    char* const contents = std::strstr(componentBundlePath, contentsMarker);
    if (contents == nullptr)
        return;
    *contents = '\0';

    constexpr const char* componentSuffix = ".component";
    const size_t pathLength = std::strlen(componentBundlePath);
    const size_t suffixLength = std::strlen(componentSuffix);
    if (pathLength < suffixLength
        || std::strcmp(componentBundlePath + pathLength - suffixLength,
                       componentSuffix) != 0)
        return;

    DAF_NAMESPACE::d_nextBundlePath = componentBundlePath;
}
} // namespace
