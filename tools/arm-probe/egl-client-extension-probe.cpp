#include <dlfcn.h>
#include <cstdio>

typedef const char* (*EGLQueryStringFn)(void*, int);

int main() {
    std::puts("MPC Master Mixer: EGL client-extension probe");
    std::puts("Queries EGL_NO_DISPLAY only; does not initialize EGL or open the screen.");

    void* handle = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        std::printf("libEGL.so: LOAD FAILED: %s\n", dlerror());
        return 1;
    }

    dlerror();
    void* address = dlsym(handle, "eglQueryString");
    const char* error = dlerror();
    if (error || !address) {
        std::printf("eglQueryString: MISSING%s%s\n", error ? ": " : "", error ? error : "");
        dlclose(handle);
        return 1;
    }

    EGLQueryStringFn query_string = reinterpret_cast<EGLQueryStringFn>(address);
    const int EGL_EXTENSIONS_VALUE = 0x3055;
    const char* extensions = query_string(nullptr, EGL_EXTENSIONS_VALUE);

    if (!extensions) {
        std::puts("EGL client extensions: unavailable (driver returned null)");
        dlclose(handle);
        return 2;
    }

    std::puts("EGL client extensions: available");
    std::puts(extensions);
    dlclose(handle);
    return 0;
}
