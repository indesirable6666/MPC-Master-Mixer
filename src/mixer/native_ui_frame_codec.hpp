#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace mpcmixer {

// Four-byte unsigned big-endian length prefix used by the proposed local IPC
// framing. This codec owns no memory and performs no I/O or allocation.
class NativeUiFrameCodec {
public:
    static constexpr std::size_t kHeaderBytes = 4;

    enum class Status : std::uint8_t {
        NeedMore,
        FrameReady,
        CleanEof,
        Truncated,
        InvalidLength,
        InvalidConfiguration
    };

    // Encodes a nonzero payload length. Zero is reserved as invalid.
    static bool encodeLength(
        std::uint32_t length,
        std::uint8_t (&header)[kHeaderBytes]) noexcept {
        if (length == 0)
            return false;
        header[0] = static_cast<std::uint8_t>((length >> 24) & 0xffu);
        header[1] = static_cast<std::uint8_t>((length >> 16) & 0xffu);
        header[2] = static_cast<std::uint8_t>((length >> 8) & 0xffu);
        header[3] = static_cast<std::uint8_t>(length & 0xffu);
        return true;
    }

    static std::uint32_t decodeLength(
        const std::uint8_t (&header)[kHeaderBytes]) noexcept {
        return (static_cast<std::uint32_t>(header[0]) << 24) |
               (static_cast<std::uint32_t>(header[1]) << 16) |
               (static_cast<std::uint32_t>(header[2]) << 8) |
               static_cast<std::uint32_t>(header[3]);
    }
};

// Incremental, caller-buffered frame reader. feed() consumes no more than one
// frame and stops exactly at its boundary; the caller processes the payload,
// calls reset(), then feeds any remaining bytes. The caller chooses both the
// payload buffer capacity and the policy maximum (e.g. 1024 for command JSON).
class NativeUiFrameReader {
public:
    NativeUiFrameReader(
        std::uint8_t* payloadBuffer,
        std::size_t payloadCapacity,
        std::size_t maximumFrameBytes) noexcept
        : payloadBuffer_(payloadBuffer),
          payloadCapacity_(payloadCapacity),
          maximumFrameBytes_(maximumFrameBytes) {
        if (payloadBuffer_ == nullptr || payloadCapacity_ == 0 ||
            maximumFrameBytes_ == 0 ||
            maximumFrameBytes_ > payloadCapacity_ ||
            maximumFrameBytes_ > (std::numeric_limits<std::uint32_t>::max)()) {
            status_ = NativeUiFrameCodec::Status::InvalidConfiguration;
        }
    }

    NativeUiFrameCodec::Status feed(
        const std::uint8_t* input,
        std::size_t inputBytes,
        std::size_t& consumed) noexcept {
        consumed = 0;
        if (status_ == NativeUiFrameCodec::Status::InvalidConfiguration ||
            status_ == NativeUiFrameCodec::Status::InvalidLength)
            return status_;
        if (status_ == NativeUiFrameCodec::Status::FrameReady)
            return status_;
        if (inputBytes != 0 && input == nullptr)
            return NativeUiFrameCodec::Status::InvalidConfiguration;

        while (consumed < inputBytes) {
            if (headerBytes_ < NativeUiFrameCodec::kHeaderBytes) {
                header_[headerBytes_++] = input[consumed++];
                if (headerBytes_ == NativeUiFrameCodec::kHeaderBytes) {
                    expectedBytes_ = NativeUiFrameCodec::decodeLength(header_);
                    if (expectedBytes_ == 0 ||
                        expectedBytes_ > maximumFrameBytes_ ||
                        expectedBytes_ > payloadCapacity_) {
                        status_ = NativeUiFrameCodec::Status::InvalidLength;
                        return status_;
                    }
                }
                continue;
            }

            const std::size_t remaining = expectedBytes_ - payloadBytes_;
            const std::size_t available = inputBytes - consumed;
            const std::size_t amount = remaining < available ? remaining : available;
            for (std::size_t i = 0; i < amount; ++i)
                payloadBuffer_[payloadBytes_ + i] = input[consumed + i];
            payloadBytes_ += amount;
            consumed += amount;

            if (payloadBytes_ == expectedBytes_) {
                status_ = NativeUiFrameCodec::Status::FrameReady;
                return status_;
            }
        }
        return NativeUiFrameCodec::Status::NeedMore;
    }

    // Call only after the current FrameReady payload has been consumed.
    void reset() noexcept {
        if (status_ == NativeUiFrameCodec::Status::InvalidConfiguration ||
            status_ == NativeUiFrameCodec::Status::InvalidLength)
            return;
        headerBytes_ = 0;
        expectedBytes_ = 0;
        payloadBytes_ = 0;
        status_ = NativeUiFrameCodec::Status::NeedMore;
    }

    NativeUiFrameCodec::Status endOfStream() const noexcept {
        if (status_ == NativeUiFrameCodec::Status::InvalidConfiguration ||
            status_ == NativeUiFrameCodec::Status::InvalidLength)
            return status_;
        if (status_ == NativeUiFrameCodec::Status::FrameReady ||
            (headerBytes_ == 0 && payloadBytes_ == 0))
            return NativeUiFrameCodec::Status::CleanEof;
        return NativeUiFrameCodec::Status::Truncated;
    }

    const std::uint8_t* payload() const noexcept { return payloadBuffer_; }
    std::size_t payloadSize() const noexcept { return expectedBytes_; }
    std::size_t maximumFrameBytes() const noexcept { return maximumFrameBytes_; }

private:
    std::uint8_t* payloadBuffer_ = nullptr;
    std::size_t payloadCapacity_ = 0;
    std::size_t maximumFrameBytes_ = 0;
    std::uint8_t header_[NativeUiFrameCodec::kHeaderBytes]{};
    std::size_t headerBytes_ = 0;
    std::size_t expectedBytes_ = 0;
    std::size_t payloadBytes_ = 0;
    NativeUiFrameCodec::Status status_ = NativeUiFrameCodec::Status::NeedMore;
};

} // namespace mpcmixer
