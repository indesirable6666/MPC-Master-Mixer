#pragma once

#include "native_ui_protocol_v1_audio_ack_publisher.hpp"
#include "native_ui_protocol_v1_client_message_handler.hpp"
#include "native_ui_protocol_v1_snapshot_delivery.hpp"
#include "native_ui_unix_stream_connection.hpp"

#include <cstdint>

namespace mpcmixer {

// Control-thread-only teardown coordinator for one protocol connection.
// It deliberately does not create sockets or run a polling loop. The caller
// owns connection establishment and invokes disconnect on the transport thread.
class NativeUiUnixStreamProtocolOwner {
public:
    NativeUiUnixStreamProtocolOwner(
        NativeUiUnixStreamConnection& connection,
        NativeUiProtocolV1ClientMessageHandler& ingress,
        NativeUiProtocolV1SnapshotDelivery& snapshots,
        NativeUiProtocolV1AudioAckPublisher& acknowledgements) noexcept
        : connection_(connection), ingress_(ingress), snapshots_(snapshots),
          acknowledgements_(acknowledgements) {}

    bool disconnect(std::uint64_t epoch) noexcept {
        if (!connection_.connected() || epoch == 0 ||
            connection_.epoch() != epoch)
            return false;

        // Discard transport bytes first, then protocol staged responses, then
        // invalidate the session and release its descriptor.
        connection_.cancelPendingOutput();
        ingress_.resetConnectionState(epoch, snapshots_);
        acknowledgements_.cancelPending();
        return connection_.invalidate(epoch);
    }

private:
    NativeUiUnixStreamConnection& connection_;
    NativeUiProtocolV1ClientMessageHandler& ingress_;
    NativeUiProtocolV1SnapshotDelivery& snapshots_;
    NativeUiProtocolV1AudioAckPublisher& acknowledgements_;
};

} // namespace mpcmixer
