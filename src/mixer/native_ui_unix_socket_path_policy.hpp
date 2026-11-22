#pragma once

#include <cstddef>
#include <string>

#include <sys/un.h>

namespace mpcmixer {

enum class NativeUiUnixSocketPathStatus {
    Valid,
    Empty,
    NotAbsolute,
    RootDirectory,
    TrailingSlash,
    EmbeddedNull,
    TooLong,
    DotComponent,
    ParentComponent
};

// Pure validation only: this does not inspect the filesystem, reserve a path,
// or make bind() race-free. Call only on a control/transport thread.
class NativeUiUnixSocketPathPolicy {
public:
    static constexpr std::size_t maximumPathBytes() noexcept {
        return sizeof(sockaddr_un::sun_path) - 1;
    }

    static NativeUiUnixSocketPathStatus validate(
        const std::string& path) noexcept {
        if (path.empty())
            return NativeUiUnixSocketPathStatus::Empty;
        if (path.find('\0') != std::string::npos)
            return NativeUiUnixSocketPathStatus::EmbeddedNull;
        if (path[0] != '/')
            return NativeUiUnixSocketPathStatus::NotAbsolute;
        if (path == "/")
            return NativeUiUnixSocketPathStatus::RootDirectory;
        if (path[path.size() - 1] == '/')
            return NativeUiUnixSocketPathStatus::TrailingSlash;
        if (path.size() > maximumPathBytes())
            return NativeUiUnixSocketPathStatus::TooLong;

        std::size_t componentStart = 1;
        while (componentStart < path.size()) {
            const std::size_t slash = path.find('/', componentStart);
            const std::size_t componentEnd =
                slash == std::string::npos ? path.size() : slash;
            const std::string component =
                path.substr(componentStart, componentEnd - componentStart);
            if (component == ".")
                return NativeUiUnixSocketPathStatus::DotComponent;
            if (component == "..")
                return NativeUiUnixSocketPathStatus::ParentComponent;
            componentStart = componentEnd + 1;
        }
        return NativeUiUnixSocketPathStatus::Valid;
    }
};

} // namespace mpcmixer
