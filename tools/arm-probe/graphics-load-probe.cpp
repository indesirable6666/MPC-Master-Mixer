#include <dlfcn.h>
#include <cstdio>

static bool check_library(const char* name, const char* const* symbols, unsigned count) {
    void* handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        std::printf("%s: LOAD FAILED: %s\n", name, dlerror());
        return false;
    }

    std::printf("%s: loaded\n", name);
    bool ok = true;
    for (unsigned i = 0; i < count; ++i) {
        dlerror();
        void* address = dlsym(handle, symbols[i]);
        const char* error = dlerror();
        std::printf("  %s: %s\n", symbols[i], (!error && address) ? "available" : "MISSING");
        if (error || !address) ok = false;
    }
    dlclose(handle);
    return ok;
}

int main() {
    static const char* const egl_symbols[] = {
        "eglGetDisplay", "eglInitialize", "eglChooseConfig",
        "eglCreateContext", "eglCreateWindowSurface"
    };
    static const char* const gles_symbols[] = {
        "glGetString", "glClear", "glViewport", "glDrawArrays"
    };

    std::puts("MPC Master Mixer: graphics loader probe v2");
    std::puts("Loads libraries and resolves symbols only.");
    std::puts("Does not initialize EGL, open the display, draw, or access touch/MIDI/audio.");

    const bool egl_ok = check_library("libEGL.so", egl_symbols,
                                      sizeof(egl_symbols) / sizeof(egl_symbols[0]));
    const bool gles_ok = check_library("libGLESv2.so", gles_symbols,
                                       sizeof(gles_symbols) / sizeof(gles_symbols[0]));
    const bool all_ok = egl_ok && gles_ok;
    std::printf("Result: %s\n", all_ok ? "all libraries and symbols available" : "one or more checks failed");
    return all_ok ? 0 : 1;
}
