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

    assert(NativeUiFrameCodec::encodeLength(
        (std::numeric_limits<std::uint32_t>::max)(), header));
    assert(NativeUiFrameCodec::decodeLength(header) ==
           (std::numeric_limits<std::uint32_t>::max)());
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
        const auto first = reader.feed(frame, split, consumed);
        if (split < sizeof(frame)) {
            assert(first == NativeUiFrameCodec::Status::NeedMore);
            assert(consumed == split);
            assert(reader.feed(frame + split, sizeof(frame) - split, consumed) ==
                   NativeUiFrameCodec::Status::FrameReady);
            assert(consumed == sizeof(frame) - split);
        } else {
            assert(first == NativeUiFrameCodec::Status::FrameReady);
            assert(consumed == sizeof(frame));
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

void testMaximumLengthAndEmptyFeed() {
    std::array<std::uint8_t, 4> storage{};
    NativeUiFrameReader reader(storage.data(), storage.size(), storage.size());
    std::size_t consumed = 99;
    assert(reader.feed(nullptr, 0, consumed) ==
           NativeUiFrameCodec::Status::NeedMore);
    assert(consumed == 0);

    const std::uint8_t exactMaximum[] = {0, 0, 0, 4, 't', 'e', 's', 't'};
    assert(reader.feed(exactMaximum, sizeof(exactMaximum), consumed) ==
           NativeUiFrameCodec::Status::FrameReady);
    assert(consumed == sizeof(exactMaximum));
    assert(reader.payloadSize() == storage.size());
    assert(std::memcmp(reader.payload(), "test", storage.size()) == 0);
}

void testRejectZeroAndOversizedBeforePayload() {
    std::array<std::uint8_t, 8> storage{};
    NativeUiFrameReader reader(storage.data(), storage.size(), 4);
    const std::uint8_t zero[] = {0, 0, 0, 0};
    std::size_t consumed = 0;
    assert(reader.feed(zero, sizeof(zero), consumed) ==
           NativeUiFrameCodec::Status::InvalidLength);
    assert(consumed == sizeof(zero));
    reader.reset();
    assert(reader.feed(nullptr, 0, consumed) ==
           NativeUiFrameCodec::Status::InvalidLength);

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

void testInvalidConfigurationAndNullInput() {
    std::array<std::uint8_t, 4> storage{};
    std::size_t consumed = 0;
    NativeUiFrameReader noBuffer(nullptr, 8, 8);
    assert(noBuffer.feed(nullptr, 0, consumed) ==
           NativeUiFrameCodec::Status::InvalidConfiguration);

    NativeUiFrameReader tooLarge(storage.data(), storage.size(), 5);
    assert(tooLarge.feed(nullptr, 0, consumed) ==
           NativeUiFrameCodec::Status::InvalidConfiguration);

    NativeUiFrameReader reader(storage.data(), storage.size(), storage.size());
    assert(reader.feed(nullptr, 1, consumed) ==
           NativeUiFrameCodec::Status::InvalidConfiguration);
    assert(consumed == 0);
    const std::uint8_t frame[] = {0, 0, 0, 1, 'z'};
    assert(reader.feed(frame, sizeof(frame), consumed) ==
           NativeUiFrameCodec::Status::FrameReady);
}

} // namespace

int main() {
    testHeaderEncoding();
    testFragmentedHeaderAndPayload();
    testEverySplitPoint();
    testBackToBackFramesStopAtBoundary();
    testMaximumLengthAndEmptyFeed();
    testRejectZeroAndOversizedBeforePayload();
    testTruncatedEof();
    testInvalidConfigurationAndNullInput();
    return 0;
}
