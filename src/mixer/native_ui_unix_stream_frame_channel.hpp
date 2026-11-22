#pragma once

#include "native_ui_frame_codec.hpp"
#include "native_ui_unix_stream_io.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace mpcmixer {

// Bounded single-frame transmit queue. The caller owns the storage and
// descriptor. All methods are control/transport-thread only.
class NativeUiUnixStreamFrameWriter {
public:
    enum class Status { Idle, Queued, Busy, InvalidFrame, Progress, WouldBlock, Sent, PeerClosed, Error };

    struct Result {
        Status status;
        std::size_t bytes;
        int errorNumber;
    };

    NativeUiUnixStreamFrameWriter(
        std::uint8_t* storage, std::size_t capacity,
        std::size_t maximumFrameBytes) noexcept
        : storage_(storage), capacity_(capacity), maximumFrameBytes_(maximumFrameBytes) {}

    Result queue(const void* payload, std::size_t size) noexcept {
        if (pending_)
            return {Status::Busy, 0, 0};
        if (storage_ == nullptr || capacity_ < NativeUiFrameCodec::kHeaderBytes ||
            maximumFrameBytes_ == 0 ||
            maximumFrameBytes_ > (std::numeric_limits<std::uint32_t>::max)() ||
            size == 0 || size > maximumFrameBytes_ ||
            size > capacity_ - NativeUiFrameCodec::kHeaderBytes || payload == nullptr)
            return {Status::InvalidFrame, 0, 0};

        std::uint8_t header[NativeUiFrameCodec::kHeaderBytes]{};
        if (!NativeUiFrameCodec::encodeLength(static_cast<std::uint32_t>(size), header))
            return {Status::InvalidFrame, 0, 0};
        std::memcpy(storage_, header, sizeof(header));
        std::memcpy(storage_ + sizeof(header), payload, size);
        totalBytes_ = sizeof(header) + size;
        offset_ = 0;
        pending_ = true;
        return {Status::Queued, size, 0};
    }

    Result flush(int descriptor) noexcept {
        if (!pending_)
            return {Status::Idle, 0, 0};
        std::size_t transferred = 0;
        while (offset_ < totalBytes_) {
            const NativeUiUnixStreamIo::Result io =
                NativeUiUnixStreamIo::transmit(
                    descriptor, storage_ + offset_, totalBytes_ - offset_);
            if (io.status == NativeUiUnixStreamIo::Status::Progress) {
                offset_ += io.bytes;
                transferred += io.bytes;
                continue;
            }
            if (io.status == NativeUiUnixStreamIo::Status::WouldBlock)
                return {Status::WouldBlock, transferred, io.errorNumber};
            if (io.status == NativeUiUnixStreamIo::Status::PeerClosed)
                return {Status::PeerClosed, transferred, io.errorNumber};
            return {Status::Error, transferred, io.errorNumber};
        }
        pending_ = false;
        totalBytes_ = 0;
        offset_ = 0;
        return {Status::Sent, transferred, 0};
    }

    bool pending() const noexcept { return pending_; }
    std::size_t remainingBytes() const noexcept {
        return pending_ ? totalBytes_ - offset_ : 0;
    }

private:
    std::uint8_t* storage_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t maximumFrameBytes_ = 0;
    std::size_t totalBytes_ = 0;
    std::size_t offset_ = 0;
    bool pending_ = false;
};

// Incremental receive pump. The scratch buffer retains bytes beyond a completed
// frame so concatenated frames are never discarded. The caller must consume
// payload() before calling acknowledgeFrame().
class NativeUiUnixStreamFrameReaderPump {
public:
    using FrameStatus = NativeUiFrameCodec::Status;
    using IoStatus = NativeUiUnixStreamIo::Status;

    NativeUiUnixStreamFrameReaderPump(
        std::uint8_t* payload, std::size_t payloadCapacity,
        std::size_t maximumFrameBytes,
        std::uint8_t* scratch, std::size_t scratchCapacity) noexcept
        : reader_(payload, payloadCapacity, maximumFrameBytes),
          scratch_(scratch), scratchCapacity_(scratchCapacity) {
        if (scratch_ == nullptr || scratchCapacity_ == 0)
            configurationValid_ = false;
    }

    FrameStatus receiveNext(int descriptor) noexcept {
        if (!configurationValid_)
            return FrameStatus::InvalidConfiguration;
        if (frameReady_)
            return FrameStatus::FrameReady;

        for (;;) {
            if (pendingOffset_ < pendingBytes_) {
                std::size_t consumed = 0;
                const FrameStatus status = reader_.feed(
                    scratch_ + pendingOffset_, pendingBytes_ - pendingOffset_, consumed);
                pendingOffset_ += consumed;
                if (status == FrameStatus::FrameReady) {
                    frameReady_ = true;
                    return status;
                }
                if (status != FrameStatus::NeedMore)
                    return status;
                if (pendingOffset_ < pendingBytes_)
                    return FrameStatus::InvalidConfiguration;
                pendingOffset_ = pendingBytes_ = 0;
                continue;
            }

            const NativeUiUnixStreamIo::Result io =
                NativeUiUnixStreamIo::receive(descriptor, scratch_, scratchCapacity_);
            if (io.status == IoStatus::Progress) {
                pendingOffset_ = 0;
                pendingBytes_ = io.bytes;
                continue;
            }
            if (io.status == IoStatus::WouldBlock)
                return FrameStatus::NeedMore;
            if (io.status == IoStatus::PeerClosed)
                return reader_.endOfStream();
            return FrameStatus::InvalidConfiguration;
        }
    }

    void acknowledgeFrame() noexcept {
        if (!frameReady_)
            return;
        reader_.reset();
        frameReady_ = false;
    }

    const std::uint8_t* payload() const noexcept { return reader_.payload(); }
    std::size_t payloadSize() const noexcept { return reader_.payloadSize(); }
    bool hasFrame() const noexcept { return frameReady_; }

private:
    NativeUiFrameReader reader_;
    std::uint8_t* scratch_ = nullptr;
    std::size_t scratchCapacity_ = 0;
    std::size_t pendingBytes_ = 0;
    std::size_t pendingOffset_ = 0;
    bool configurationValid_ = true;
    bool frameReady_ = false;
};

} // namespace mpcmixer
