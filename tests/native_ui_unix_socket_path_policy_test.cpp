#include "native_ui_unix_socket_path_policy.hpp"

#include <cassert>
#include <string>

using mpcmixer::NativeUiUnixSocketPathPolicy;
using mpcmixer::NativeUiUnixSocketPathStatus;

int main() {
    const auto check = [](const std::string& path,
                          NativeUiUnixSocketPathStatus expected) {
        assert(NativeUiUnixSocketPathPolicy::validate(path) == expected);
    };

    check("", NativeUiUnixSocketPathStatus::Empty);
    check("tmp/mixer.sock", NativeUiUnixSocketPathStatus::NotAbsolute);
    check("/", NativeUiUnixSocketPathStatus::RootDirectory);
    check("/run/mixer/", NativeUiUnixSocketPathStatus::TrailingSlash);
    check(std::string("/run/mix\0er.sock", 16),
          NativeUiUnixSocketPathStatus::EmbeddedNull);
    check("/run/./mixer.sock", NativeUiUnixSocketPathStatus::DotComponent);
    check("/run/../mixer.sock", NativeUiUnixSocketPathStatus::ParentComponent);
    check("/run//mixer.sock", NativeUiUnixSocketPathStatus::Valid);
    check("/run/mixer.sock", NativeUiUnixSocketPathStatus::Valid);
    check(std::string(NativeUiUnixSocketPathPolicy::maximumPathBytes() + 1, 'x')
              .insert(0, "/"),
          NativeUiUnixSocketPathStatus::NotAbsolute);

    const std::string tooLong =
        "/" + std::string(NativeUiUnixSocketPathPolicy::maximumPathBytes(), 'x');
    check(tooLong, NativeUiUnixSocketPathStatus::TooLong);

    return 0;
}
