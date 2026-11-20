#include <dlfcn.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

using EGLDisplay = void*;
using EGLConfig = void*;
using EGLSurface = void*;
using EGLContext = void*;
using EGLint = int;
using EGLBoolean = unsigned int;
using EGLenum = unsigned int;

static constexpr EGLint EGL_NONE = 0x3038;
static constexpr EGLint EGL_RED_SIZE = 0x3024;
static constexpr EGLint EGL_GREEN_SIZE = 0x3023;
static constexpr EGLint EGL_BLUE_SIZE = 0x3022;
static constexpr EGLint EGL_ALPHA_SIZE = 0x3021;
static constexpr EGLint EGL_SURFACE_TYPE = 0x3033;
static constexpr EGLint EGL_PBUFFER_BIT = 0x0001;
static constexpr EGLint EGL_RENDERABLE_TYPE = 0x3040;
static constexpr EGLint EGL_OPENGL_ES2_BIT = 0x0004;
static constexpr EGLint EGL_WIDTH = 0x3057;
static constexpr EGLint EGL_HEIGHT = 0x3056;
static constexpr EGLint EGL_CONTEXT_CLIENT_VERSION = 0x3098;
static constexpr EGLenum EGL_OPENGL_ES_API = 0x30A0;

template <typename T>
static T symbol(void* lib, const char* name) {
    return reinterpret_cast<T>(dlsym(lib, name));
}

static bool mpcProcessPresent() {
    DIR* proc = opendir("/proc");
    if (!proc) return true; // fail closed
    bool found = false;
    dirent* entry = nullptr;
    while ((entry = readdir(proc)) != nullptr) {
        if (entry->d_type != DT_DIR) continue;
        char* end = nullptr;
        std::strtol(entry->d_name, &end, 10);
        if (!end || *end != '\0' || end == entry->d_name) continue;
        char path[256];
        std::snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);
        FILE* f = std::fopen(path, "r");
        if (!f) continue;
        char name[128] = {};
        if (std::fgets(name, sizeof(name), f)) {
            name[std::strcspn(name, "\r\n")] = '\0';
            if (std::strcmp(name, "MPC Main Thread") == 0 ||
                std::strcmp(name, "MPC") == 0) found = true;
        }
        std::fclose(f);
        if (found) break;
    }
    closedir(proc);
    return found;
}

static bool mpcDrmClientPresent() {
    FILE* f = std::fopen("/sys/kernel/debug/dri/0/clients", "r");
    if (!f) return true; // fail closed if we cannot verify
    char line[512];
    bool found = false;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strstr(line, "MPC Main Thread") ||
            std::strstr(line, "MPC")) {
            found = true;
            break;
        }
    }
    std::fclose(f);
    return found;
}

int main(int argc, char** argv) {
    FILE* report = std::fopen("mpc-egl-offscreen-report.txt", "w");
    if (!report) report = stdout;
    std::fprintf(report, "MPC Master Mixer: EGL pbuffer/offscreen test\n");
    std::fprintf(report, "This test initializes EGL and creates an offscreen ES2 context.\n");
    std::fprintf(report, "It does not create a window or intentionally access the display framebuffer.\n");
    std::fprintf(report, "Run ONLY after MPC has been fully exited.\n");

    if (argc != 2 || std::strcmp(argv[1], "--i-confirm-mpc-is-closed") != 0) {
        std::fprintf(report, "STOP: explicit confirmation argument is required. No EGL calls made.\n");
        if (report != stdout) std::fclose(report);
        return 10;
    }
    if (mpcProcessPresent()) {
        std::fprintf(report, "STOP: MPC process detected (or /proc could not be checked). No EGL calls made.\n");
        if (report != stdout) std::fclose(report);
        return 11;
    }
    if (mpcDrmClientPresent()) {
        std::fprintf(report, "STOP: MPC DRM client detected (or DRM client list unavailable). No EGL calls made.\n");
        if (report != stdout) std::fclose(report);
        return 12;
    }

    void* egl = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
    void* gles = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
    if (!egl || !gles) {
        std::fprintf(report, "FAIL: could not load EGL/GLES libraries: %s\n", dlerror());
        if (gles) dlclose(gles);
        if (egl) dlclose(egl);
        if (report != stdout) std::fclose(report);
        return 1;
    }

    using GetDisplay = EGLDisplay (*)(void*);
    using Initialize = EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*);
    using ChooseConfig = EGLBoolean (*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
    using CreatePbuffer = EGLSurface (*)(EGLDisplay, EGLConfig, const EGLint*);
    using BindAPI = EGLBoolean (*)(EGLenum);
    using CreateContext = EGLContext (*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
    using MakeCurrent = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    using DestroySurface = EGLBoolean (*)(EGLDisplay, EGLSurface);
    using DestroyContext = EGLBoolean (*)(EGLDisplay, EGLContext);
    using Terminate = EGLBoolean (*)(EGLDisplay);
    using GetError = EGLint (*)();
    using GetString = const unsigned char* (*)(unsigned int);

    auto getDisplay = symbol<GetDisplay>(egl, "eglGetDisplay");
    auto initialize = symbol<Initialize>(egl, "eglInitialize");
    auto chooseConfig = symbol<ChooseConfig>(egl, "eglChooseConfig");
    auto createPbuffer = symbol<CreatePbuffer>(egl, "eglCreatePbufferSurface");
    auto bindAPI = symbol<BindAPI>(egl, "eglBindAPI");
    auto createContext = symbol<CreateContext>(egl, "eglCreateContext");
    auto makeCurrent = symbol<MakeCurrent>(egl, "eglMakeCurrent");
    auto destroySurface = symbol<DestroySurface>(egl, "eglDestroySurface");
    auto destroyContext = symbol<DestroyContext>(egl, "eglDestroyContext");
    auto terminate = symbol<Terminate>(egl, "eglTerminate");
    auto getError = symbol<GetError>(egl, "eglGetError");
    auto glGetString = symbol<GetString>(gles, "glGetString");

    if (!getDisplay || !initialize || !chooseConfig || !createPbuffer ||
        !bindAPI || !createContext || !makeCurrent || !destroySurface ||
        !destroyContext || !terminate || !getError || !glGetString) {
        std::fprintf(report, "FAIL: one or more required EGL/GLES entry points are missing.\n");
        dlclose(gles); dlclose(egl);
        if (report != stdout) std::fclose(report);
        return 2;
    }

    EGLDisplay display = getDisplay(nullptr);
    if (!display) {
        std::fprintf(report, "FAIL: eglGetDisplay returned EGL_NO_DISPLAY; error=0x%04x\n", getError());
        dlclose(gles); dlclose(egl);
        if (report != stdout) std::fclose(report);
        return 3;
    }

    EGLint major = 0, minor = 0;
    if (!initialize(display, &major, &minor)) {
        std::fprintf(report, "FAIL: eglInitialize error=0x%04x\n", getError());
        dlclose(gles); dlclose(egl);
        if (report != stdout) std::fclose(report);
        return 4;
    }
    std::fprintf(report, "EGL initialized: %d.%d\n", major, minor);

    const EGLint configAttrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_NONE
    };
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!chooseConfig(display, configAttrs, &config, 1, &count) || count < 1) {
        std::fprintf(report, "FAIL: no ES2 pbuffer config; error=0x%04x\n", getError());
        terminate(display); dlclose(gles); dlclose(egl);
        if (report != stdout) std::fclose(report);
        return 5;
    }

    if (!bindAPI(EGL_OPENGL_ES_API)) {
        std::fprintf(report, "FAIL: eglBindAPI error=0x%04x\n", getError());
        terminate(display); dlclose(gles); dlclose(egl);
        if (report != stdout) std::fclose(report);
        return 6;
    }
    const EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext context = createContext(display, config, nullptr, contextAttrs);
    if (!context) {
        std::fprintf(report, "FAIL: eglCreateContext error=0x%04x\n", getError());
        terminate(display); dlclose(gles); dlclose(egl);
        if (report != stdout) std::fclose(report);
        return 7;
    }
    const EGLint surfaceAttrs[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
    EGLSurface surface = createPbuffer(display, config, surfaceAttrs);
    if (!surface) {
        std::fprintf(report, "FAIL: eglCreatePbufferSurface error=0x%04x\n", getError());
        destroyContext(display, context); terminate(display);
        dlclose(gles); dlclose(egl);
        if (report != stdout) std::fclose(report);
        return 8;
    }
    if (!makeCurrent(display, surface, surface, context)) {
        std::fprintf(report, "FAIL: eglMakeCurrent error=0x%04x\n", getError());
        destroySurface(display, surface); destroyContext(display, context);
        terminate(display); dlclose(gles); dlclose(egl);
        if (report != stdout) std::fclose(report);
        return 9;
    }

    const auto* version = glGetString(0x1F02); // GL_VERSION
    const auto* renderer = glGetString(0x1F01); // GL_RENDERER
    const auto* vendor = glGetString(0x1F00); // GL_VENDOR
    std::fprintf(report, "GLES version: %s\n", version ? reinterpret_cast<const char*>(version) : "(null)");
    std::fprintf(report, "GLES renderer: %s\n", renderer ? reinterpret_cast<const char*>(renderer) : "(null)");
    std::fprintf(report, "GLES vendor: %s\n", vendor ? reinterpret_cast<const char*>(vendor) : "(null)");
    std::fprintf(report, "SUCCESS: EGL ES2 pbuffer context created and made current.\n");

    makeCurrent(display, nullptr, nullptr, nullptr);
    destroySurface(display, surface);
    destroyContext(display, context);
    terminate(display);
    dlclose(gles);
    dlclose(egl);
    if (report != stdout) std::fclose(report);
    return 0;
}
