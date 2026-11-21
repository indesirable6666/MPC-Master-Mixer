# Audio control bridge

## Why it exists

The DSP engine's direct setters are control-thread APIs and must never run
concurrently with its audio processing methods. Calling a setter directly from
a UI thread while the audio callback is processing would create a C++ data
race. The browser prototype is not connected to the C++ engine, so it must not
call this class directly; a native host/controller will own the bridge.

The framework-neutral `src/mixer/mixer_controller.hpp` maps native UI
operations to the bridge: track faders, pan, Mute/Solo, four Aux sends, six-band
EQ (including single-band edits that preserve the other five bands), insert-slot replacement with acknowledgement tickets (also queryable through `MixerController::isInsertUpdateApplied()`), Aux returns, and Master gain/mute. Its integer controls are range-checked
before conversion. It keeps producer-side desired-state snapshots for
all 16 tracks, their six-band EQ settings and two non-owning insert-slot pointers, four Aux returns, and Master gain/mute. Changing one track
control preserves the other cached track controls; accepted EQ changes update
the corresponding six-band snapshot only after enqueue succeeds. Each snapshot advances
only after its command is successfully enqueued; a full queue therefore leaves
the previous desired state intact. The controller and bridge must be called
from the same serialized producer thread. This adapter is not yet connected
to a widget toolkit or to the browser prototype. `desiredStateSnapshot()`
provides a value-copy of the complete producer-side state for a native view to
render; call it on that same serialized control thread, not from the audio
callback or an unrelated UI thread.

## Contract

`src/audio/audio_control_bridge.hpp` provides a fixed-capacity,
single-producer/single-consumer (SPSC) command queue:

- One control/UI producer enqueues validated track state, precomputed six-band
  EQ configurations, insert-slot replacements, Aux return state, Master gain,
  and Master mute values.
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
- Insert replacements carry non-owning pointers. Passing `nullptr` is a
  supported way to clear/bypass a slot. The producer keeps the previous and
  replacement objects alive until the returned 32-bit ticket is acknowledged
  by `hasApplied()` (or the controller's
  `isInsertUpdateApplied(ticket)` helper). The acknowledgement is published
  after the pointer swap at the block boundary; only then may the producer
  destroy the previous object. Never allocate, delete, or wait for
  acknowledgement in the audio callback. Configure an effect before
  enqueueing it and do not mutate it while the audio thread may process it.
- Published insert instances are immutable while the audio engine may
  process them. To change an effect parameter (such as SoftClip drive or enable),
  configure a replacement instance on the control thread and enqueue it with
  `enqueueTrackInsert()`. Keep both instances alive until its ticket is
  acknowledged; only then retire the old instance off the audio thread. Never
  mutate fields on an active effect from the UI thread.

See `docs/INSERT_EFFECT_LIFECYCLE.md` for a focused ownership handoff guide
and a control-thread replacement example. In particular, serialize replacements
per slot: do not queue a second update for a slot while its previous replacement
is still awaiting acknowledgement.

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
