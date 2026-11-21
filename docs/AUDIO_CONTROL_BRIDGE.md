# Audio control bridge

## Why it exists

The DSP engine's direct setters are control-thread APIs and must never run
concurrently with its audio processing methods. Calling a setter directly from
a UI thread while the audio callback is processing would create a C++ data
race. The browser prototype is not connected to the C++ engine, so it must not
call this class directly; a native host/controller will own the bridge.

## Contract

`src/audio/audio_control_bridge.hpp` provides a fixed-capacity,
single-producer/single-consumer (SPSC) command queue:

- One control/UI producer enqueues validated track state, precomputed six-band
  EQ configurations, Aux return state, Master gain, and Master mute values.
- The audio callback is the sole consumer. It drains pending commands at the
  beginning of each block, then processes that block using the updated state.
- Commands are applied in FIFO order. If the 64-command queue is full, enqueue
  returns `false`; callers must retain/retry the latest desired state rather
  than silently assuming it was applied.
- EQ coefficient calculation (trigonometry and powers) happens before enqueue
  on the producer/control thread. The audio callback only copies the prepared
  coefficients and resets the filter history at a block boundary.
- No mutex, dynamic allocation, logging, or device access occurs in enqueue or
  block processing. Queue publication uses acquire/release atomics.
- Only one producer and one consumer are supported. A future host with several
  UI/control sources must serialize them on a single control thread before
  calling the enqueue methods.
- EQ coefficient preparation and insert lifetime/configuration remain separate
  control-thread responsibilities. They are not currently represented as
  queue commands.

Use `process`, `processWithAux`, or
`processWithAuxAndReturns` from the audio thread. Do not call the underlying
engine setters from another thread while processing is active.

## Current boundary

This is a tested host-side synchronization primitive, not a completed audio
host. It does not capture sound, open ALSA, select MPC inputs, create an audio
device, or send output to the MPC. The browser prototype still has local demo
state and needs a native host/controller adapter before it can enqueue these
commands. A target audio backend and real MPC routing remain unimplemented and
require a separately authorized, reversible hardware validation phase.
