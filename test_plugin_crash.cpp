#include <dlfcn.h>
#include <iostream>

int main() {
    std::cout << "Testing Dragonfly Unified Reverb VST3 loading...\n";
    
    const char* path = "/home/marc/.vst3/Dragonfly Unified Reverb.vst3/Contents/x86_64-linux/Dragonfly Unified Reverb.so";
    
    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        std::cerr << "Failed to load: " << dlerror() << std::endl;
        return 1;
    }
    
    std::cout << "Successfully loaded library\n";
    
    // Try to find the factory function
    void* factory = dlsym(handle, "GetPluginFactory");
    if (!factory) {
        std::cerr << "Failed to find factory: " << dlerror() << std::endl;
        dlclose(handle);
        return 1;
    }
    
    std::cout << "Found plugin factory\n";
    
    dlclose(handle);
    std::cout << "Test passed!\n";
    return 0;
}
