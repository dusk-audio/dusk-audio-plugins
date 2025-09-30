#include <iostream>
#include <dlfcn.h>

typedef struct IPluginFactory IPluginFactory;

int main() {
    const char* path = "/home/marc/.vst3/Dragonfly Unified Reverb.vst3/Contents/x86_64-linux/Dragonfly Unified Reverb.so";
    
    std::cout << "Loading VST3: " << path << std::endl;
    
    // Load with RTLD_LAZY to avoid symbol resolution issues
    void* handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        std::cerr << "Failed to load: " << dlerror() << std::endl;
        return 1;
    }
    
    typedef IPluginFactory* (*GetFactoryProc)();
    GetFactoryProc getFactory = (GetFactoryProc)dlsym(handle, "GetPluginFactory");
    
    if (!getFactory) {
        std::cerr << "GetPluginFactory not found: " << dlerror() << std::endl;
        dlclose(handle);
        return 1;
    }
    
    std::cout << "Found GetPluginFactory at: " << (void*)getFactory << std::endl;
    
    // Try to call it
    IPluginFactory* factory = getFactory();
    std::cout << "Factory pointer: " << factory << std::endl;
    
    if (!factory) {
        std::cerr << "Factory is null!" << std::endl;
        dlclose(handle);
        return 1;
    }
    
    std::cout << "Plugin loaded successfully!" << std::endl;
    
    dlclose(handle);
    return 0;
}
