#include "native_ui_frame_codec.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

using mpcmixer::NativeUiFrameCodec;
using mpcmixer::NativeUiFrameReader;

namespace {

void testHeaderEncoding() {
    std::uint8_t header[4]{};
    assert(NativeUiFrameCodec::encodeLength(0x01020304u, header));
    assert(header[0] == 0x01 && header[1] == 0x02 &&
           header[2] == 0x03 && header[3] == 0x04);
    assert(NativeUiFrameCodec::decodeLength(header) == 0x01020304u);
    assert(!NativeUiFrameCodec::encodeLength(0, header));
}

void testFragmentedHeaderAndPayload() {
    std::array<std::uint8_t, 16> storage{};
    NativeUiFrameReader reader(storage.data(), storage.size(), 8);
    const std::uint8_t part1[] = {0, 0};
    std::size_t consumed = 0;
    assert(reader.feed(part1, sizeof(part1), consumed) ==
           NativeUiFrameCodec::Status::NeedMore);
    assert(consumed == sizeof(part1));

    const std::uint8_t part2[] = {0, 3, 'a'};
    assert(reader.feed(part2, sizeof(part2), consumed) ==
           NativeUiFrameCodec::Status::NeedMore);
    assert(consumed == sizeof(part2));

    const std::uint8_t part3[] = {'b', 'c'};
    assert(reader.feed(part3, sizeof(part3), consumed) ==
           NativeUiFrameCodec::Status::FrameReady);
    assert(consumed == sizeof(part3));
    assert(reader.payloadSize() == 3);
    assert(std::memcmp(reader.payload(), "abc", 3) == 0);
    assert(reader.endOfStream() == NativeUiFrameCodec::Status::CleanEof);
}

void testEverySplitPoint() {
    const std::uint8_t frame[] = {0, 0, 0, 5, 'h', 'e', 'l', 'l', 'o'};
    for (std::size_t split = 0; split <= sizeof(frame); ++split) {
        std::array<std::uint8_t, 8> storage{};
        NativeUiFrameReader reader(storage.data(), storage.size(), storage.size());
        std::size_t consumed = 0;
        assert(reader.feed(frame, split, consumed) ==
               NativeUiFrameCodec::Status::NeedMore ||
               (split == sizeof(frame) &&
                reader.feed(nullptr, 0, consumed) ==
                    NativeUiFrameCodec::Status::FrameReady));
        if (split < sizeof(frame)) {
            assert(consumed == split);
            assert(reader.feed(frame + split, sizeof(frame) - split, consumed) ==
                   NativeUiFrameCodec::Status::FrameReady);
            assert(consumed == sizeof(frame) - split);
        }
        assert(reader.payloadSize() == 5);
        assert(std::memcmp(reader.payload(), "hello", 5) == 0);
    }
}

void testBackToBackFramesStopAtBoundary() {
    std::array<std::uint8_t, 8> storage{};
    NativeUiFrameReader reader(storage.data(), storage.size(), storage.size());
    const std::uint8_t bytes[] = {
        0, 0, 0, 1, 'x', 0, 0, 0, 1, 'y'
    };
    std::size_t consumed = 0;
    assert(reader.feed(bytes, sizeof(bytes), consumed) ==
           NativeUiFrameCodec::Status::FrameReady);
    assert(consumed == 5 && reader.payload()[0] == 'x');
    reader.reset();
    assert(reader.feed(bytes + consumed, sizeof(bytes) - consumed, consumed) ==
           NativeUiFrameCodec::Status::FrameReady);
    assert(consumed == 5 && reader.payload()[0] == 'y');
}

void testRejectZeroAndOversizedBeforePayload() {
    std::array<std::uint8_t, 8> storage{};
    NativeUiFrameReader reader(storage.data(), storage.size(), 4);
    const std::uint8_t zero[] = {0, 0, 0, 0};
    std::size_t consumed = 0;
    assert(reader.feed(zero, sizeof(zero), consumed) ==
           NativeUiFrameCodec::Status::InvalidLength);
    assert(consumed == sizeof(zero));

    NativeUiFrameReader oversized(storage.data(), storage.size(), 4);
    const std::uint8_t tooLarge[] = {0, 0, 0, 5};
    assert(oversized.feed(tooLarge, sizeof(tooLarge), consumed) ==
           NativeUiFrameCodec::Status::InvalidLength);
    assert(consumed == sizeof(tooLarge));
}

void testTruncatedEof() {
    std::array<std::uint8_t, 8> storage{};
    std::size_t consumed = 0;
    NativeUiFrameReader partialHeader(storage.data(), storage.size(), 8);
    const std::uint8_t header[] = {0, 0};
    assert(partialHeader.feed(header, sizeof(header), consumed) ==
           NativeUiFrameCodec::Status::NeedMore);
    assert(partialHeader.endOfStream() == NativeUiFrameCodec::Status::Truncated);

    NativeUiFrameReader partialPayload(storage.data(), storage.size(), 8);
    const std::uint8_t body[] = {0, 0, 0, 3, 'a'};
    assert(partialPayload.feed(body, sizeof(body), consumed) ==
           NativeUiFrameCodec::Status::NeedMore);
    assert(partialPayload.endOfStream() == NativeUiFrameCodec::Status::Truncated);
}

void testInvalidConfiguration() {
    NativeUiFrameReader noBuffer(nullptr, 8, 8);
    std::size_t consumed = 0;
    assert(noBuffer.feed(nullptr, 0, consumed) ==
           NativeUiFrameCodec::Status::InvalidConfiguration);
    std::array<std::uint8_t, 4> small{};
    NativeUiFrameReader tooLarge(small.data(), small.size(), 5);
    assert(tooLarge.feed(nullptr, 0, consumed) ==
           NativeUiFrameCodec::Status::InvalidConfiguration);
}

} // namespace

int main() {
    testHeaderEncoding();
    testFragmentedHeaderAndPayload();
    testEverySplitPoint();
    testBackToBackFramesStopAtBoundary();
    testRejectZeroAndOversizedBeforePayload();
    testTruncatedEof();
    testInvalidConfiguration();
    return 0;
}
