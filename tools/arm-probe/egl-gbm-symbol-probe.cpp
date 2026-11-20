#include <dlfcn.h>
#include <cstdio>
#include <unistd.h>

typedef void* (*EGLGetProcAddressFn)(const char*);
typedef void* (*EGLGetPlatformDisplayEXTFn)(unsigned int, void*, const int*);
typedef void* (*EGLCreatePlatformWindowSurfaceEXTFn)(void*, void*, void*, const int*);

static bool has_symbol(void* handle, const char* name) {
    dlerror();
    void* address = dlsym(handle, name);
    const char* error = dlerror();
    const bool found = !error && address;
    std::printf("  dlsym %-38s %s\n", name, found ? "available" : "MISSING");
    return found;
}

static bool has_proc(EGLGetProcAddressFn get_proc, const char* name) {
    void* address = get_proc ? get_proc(name) : nullptr;
    const bool found = address != nullptr;
    std::printf("  eglGetProcAddress %-28s %s\n", name, found ? "available" : "MISSING");
    return found;
}

static void inspect_gbm() {
    std::puts("libgbm.so.1:");
    void* handle = dlopen("libgbm.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        std::printf("  LOAD FAILED: %s\n", dlerror());
        return;
    }
    std::puts("  loaded");
    const char* symbols[] = {
        "gbm_create_device",
        "gbm_device_get_fd",
        "gbm_surface_create",
        "gbm_surface_lock_front_buffer",
        "gbm_bo_get_fd"
    };
    for (unsigned i = 0; i < sizeof(symbols) / sizeof(symbols[0]); ++i)
        has_symbol(handle, symbols[i]);
    dlclose(handle);
}

int main() {
    FILE* report = std::fopen("mpc-graphics-report.txt", "wx");
    if (!report) {
        std::perror("Cannot create mpc-graphics-report.txt (it may already exist)");
        return 2;
    }
    if (dup2(fileno(report), STDOUT_FILENO) == -1) {
        std::perror("Cannot redirect report output");
        std::fclose(report);
        return 2;
    }
    std::fclose(report);

    std::puts("MPC Master Mixer: EGL/GBM symbol capability probe");
    std::puts("Loads libraries and resolves symbols only.");
    std::puts("Does NOT initialize EGL, request a display, open DRM devices,");
    std::puts("create surfaces, draw, or access touch/MIDI/audio.");

    void* egl = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
    if (!egl) {
        std::printf("libEGL.so LOAD FAILED: %s\n", dlerror());
        inspect_gbm();
        return 0;
    }

    std::puts("libEGL.so: loaded");
    const bool get_proc_present = has_symbol(egl, "eglGetProcAddress");
    has_symbol(egl, "eglGetPlatformDisplay");
    has_symbol(egl, "eglGetPlatformDisplayEXT");
    has_symbol(egl, "eglCreatePlatformWindowSurface");
    has_symbol(egl, "eglCreatePlatformWindowSurfaceEXT");

    EGLGetProcAddressFn get_proc = nullptr;
    if (get_proc_present) {
        dlerror();
        void* address = dlsym(egl, "eglGetProcAddress");
        if (!dlerror() && address)
            get_proc = reinterpret_cast<EGLGetProcAddressFn>(address);
    }

    std::puts("Extension entry points via eglGetProcAddress (no display calls):");
    has_proc(get_proc, "eglGetPlatformDisplayEXT");
    has_proc(get_proc, "eglCreatePlatformWindowSurfaceEXT");

    dlclose(egl);
    inspect_gbm();
    std::puts("Probe complete. Availability does not prove that a GBM display can be initialized.");
    return 0;
}
