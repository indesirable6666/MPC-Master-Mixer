# Duplex audio engine host

`AudioDuplexEngineHost` connects the device-neutral duplex callback contract to
`AudioEngineCommandBridge`. It is the first complete software path from planar
input buffers, through the mixer, to stereo output buffers.

## Explicit input routing

The host supports up to two input channels and stereo output. Each input channel
is unmapped by default. The native application must explicitly map each input
channel to one of the mixer's 16 mono tracks before starting the backend. This
deliberately avoids assuming that any physical MPC, USB, or ALSA channel number
corresponds to a particular mixer strip.

A duplicated route is rejected for that block and both output channels are
silenced. Missing or invalid input pointers are handled the same way. If the
output-channel count is not stereo, any available supported output planes are
silenced without indexing beyond the supplied pointer array. If one output
plane is missing, any available output plane is silenced. A callback
whose input-channel count differs from the prepared format is rejected and
available outputs are silenced. A block larger than the prepared maximum is
ignored because the host cannot know whether the supplied buffers are large
enough to clear safely. Unmapped inputs are ignored; the mixer treats their
tracks as silence.

## Optional stereo Audio In pair

`AudioDuplexEngineHost::setStereoInputPairEnabled(true)` enables a
separate, opt-in stereo pair path. Select it before `prepare()`; the
prepared format must contain exactly two input channels. The pair reserves
physical input channels 0 and 1, so they cannot also be assigned to ordinary
mono tracks. Disabling pair mode requires the host to be shut down first.

`AudioEngineCommandBridge` accepts value-only commands for linked mode,
stereo gain/balance/mute, and independent left/right gain/pan/mute/solo. The
audio callback applies those commands at the next block boundary. Linked mode
keeps left and right discrete and balance only attenuates the opposite side;
unlinked mode uses the two independent mono pan laws. The pair is summed into
the main stereo output, and Master gain/mute is applied to it as well.

For meter presentation, the pair's pre-control input readings occupy the
prototype Audio In 1/2 meter slots (track indices 13 and 14); Master remains
post-mix. This dedicated pair path currently provides gain, balance/pan, mute
and solo only. It does not yet run the per-track six-band EQ, insert chain or
Aux sends for those two strips; those controls are not claimed as part of this
native pair path.

## Thread and lifecycle contract

- Call `prepare`, `start`, `stop`, and `shutdown` on the control
  thread. Input routes are atomic and may be updated while running; each
  channel's route is sampled once per callback block.
- The backend invokes the static callback with its own planar float buffers.
- The callback builds a fixed-size stack array of track pointers and drains the
  SPSC command bridge at the start of the audio block.
- No allocation, mutex, logging, device call, or wait is performed by the host
  callback.
- UI/control changes must be submitted through the command bridge. Do not call
  engine setters concurrently with processing.
- Keep the host and command bridge alive until the backend has stopped and
  shutdown has completed.

## Current implementation boundary

The offline backend drives both explicit mono routing and the optional stereo
pair path in automated tests using synthetic buffers.
There is still no ALSA, JACK, or MPC-specific backend, no physical input/output
device selection, and no live hardware validation. This host is a reusable
integration point, not proof that audio is already connected on the MPC.


The same pair controls are represented in the framework-neutral
`MixerController`, sequenced `MixerUiCommandDispatcher`, and value-only
`MixerPresentationSnapshot`. This lets a future native UI submit and render
pair state without calling DSP setters directly. It remains a host-side
contract only: there is no browser binding, IPC transport, or live device
feedback, and the snapshot reports desired rather than confirmed applied
state.
