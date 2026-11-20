#include <dlfcn.h>
#include <cstdio>

using GetProcAddress = void* (*)(const char*);

struct Symbol {
    const char* name;
};

int main() {
    std::puts("MPC Master Mixer: GLES2 symbol capability probe");
    std::puts("Loads libraries and resolves function addresses only.");
    std::puts("Does NOT initialize EGL, request a display, create a context,");
    std::puts("open DRM, draw, or access touch/MIDI/audio.");

    void* egl = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
    if (!egl) {
        std::printf("libEGL.so: LOAD FAILED: %s\n", dlerror());
        return 1;
    }
    std::puts("libEGL.so: loaded");

    auto getProcAddress = reinterpret_cast<GetProcAddress>(
        dlsym(egl, "eglGetProcAddress"));
    std::printf("  eglGetProcAddress: %s\n",
                getProcAddress ? "available" : "MISSING");

    void* gles = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
    if (!gles) {
        std::printf("libGLESv2.so: LOAD FAILED: %s\n", dlerror());
        dlclose(egl);
        return 1;
    }
    std::puts("libGLESv2.so: loaded");

    const Symbol symbols[] = {
        {"glCreateShader"},
        {"glShaderSource"},
        {"glCompileShader"},
        {"glCreateProgram"},
        {"glAttachShader"},
        {"glLinkProgram"},
        {"glGenBuffers"},
        {"glBindBuffer"},
        {"glBufferData"},
        {"glDrawArrays"},
        {"glDrawElements"},
        {"glViewport"},
        {"glGetString"}
    };

    int resolved = 0;
    const int total = static_cast<int>(sizeof(symbols) / sizeof(symbols[0]));
    std::puts("GLES2 entry points (address lookup only):");
    for (const auto& symbol : symbols) {
        dlerror();
        void* address = dlsym(gles, symbol.name);
        const char* method = "dlsym";
        if (!address && getProcAddress) {
            address = getProcAddress(symbol.name);
            method = "eglGetProcAddress";
        }
        if (address) {
            ++resolved;
            std::printf("  %-22s available (%s)\n", symbol.name, method);
        } else {
            std::printf("  %-22s MISSING\n", symbol.name);
        }
    }

    std::printf("Result: %d/%d GLES2 symbols resolved.\n", resolved, total);
    std::puts("Symbol availability does not prove that a context or display works.");

    dlclose(gles);
    dlclose(egl);
    return resolved == total ? 0 : 2;
}
