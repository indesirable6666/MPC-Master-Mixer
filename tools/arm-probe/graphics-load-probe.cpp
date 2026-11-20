#include <dlfcn.h>
#include <cstdio>

struct LibraryCheck {
    const char* name;
    const char* symbols[5];
};

int main() {
    const LibraryCheck checks[] = {
        {"libEGL.so", {"eglGetDisplay", "eglInitialize", "eglChooseConfig", "eglCreateContext", "eglCreateWindowSurface"}},
        {"libGLESv2.so", {"glGetString", "glClear", "glViewport", "glDrawArrays", nullptr}}
    };

    std::puts("MPC Master Mixer: graphics library load probe");
    std::puts("This test loads libraries and resolves symbols only.");
    std::puts("It does not initialize EGL, open the display, draw, or access touch/MIDI/audio.");

    bool all_ok = true;
    for (const auto& check : checks) {
        void* handle = dlopen(check.name, RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            std::printf("%s: LOAD FAILED: %s\n", check.name, dlerror());
            all_ok = false;
            continue;
        }

        std::printf("%s: loaded\n", check.name);
        for (const char** symbol = check.symbols; *symbol; ++symbol) {
            dlerror();
            void* address = dlsym(handle, *symbol);
            const char* error = dlerror();
            std::printf("  %s: %s\n", *symbol, error ? "MISSING" : "available");
            if (error || !address) all_ok = false;
        }
        dlclose(handle);
    }

    std::printf("Result: %s\n", all_ok ? "libraries and symbols available" : "one or more checks failed");
    return all_ok ? 0 : 1;
}
