# Native meter presentation adapter — integration boundary

## Decision

Keep the audio meter path independent from the presentation runtime. The
repository does not yet identify a buildable target UI framework, embedded
browser, native window host, or supported local IPC mechanism. Therefore this
milestone defines the host-side contract only; it does not select a transport
or add sockets, shared memory, a WebSocket server, or a WebView dependency.

The current browser page is a static offline prototype. Its
`mpc-meter-snapshot` window event is a presentation seam for a future
embedding wrapper, not an IPC endpoint and not a native binding.

## Existing producer-to-consumer path

```text
Audio callback
  -> AudioMeterSnapshotBridge::publish()
     bounded SPSC queue, 8 complete frames, drop newest if full
  -> UI/control thread
     AudioMeterUiConsumer::tick(elapsedSeconds)
     drains queued frames, applies peak hold/decay, retains newest RMS
  -> MeterPresentationAdapter::tick() on the UI/control thread
  -> MeterPresentationSink::present() with an owned-value frame
  -> concrete UI framework / embedded browser (not selected)
```

The callback must only publish the fixed-size snapshot. It must not call UI
code, invoke JavaScript, allocate, lock, wait, log, or perform IPC. Only the
single UI/control consumer may drain the SPSC queue.

## Presentation frame contract

`MeterPresentationAdapter` now publishes one complete value frame containing:

- `sequence`: a monotonically increasing presentation sequence (also advances
  on decay-only UI ticks, so strict UI consumers accept every visual update);
- `sourceBlockSequence`: the latest audio block sequence incorporated;
- `channels`: exactly 17 entries, tracks 1–16 followed by Master;
- each entry: finite normalized `peak` and `rms` values in [0, 1].

The UI consumer is the source of presentation state: peak hold and decay come
from `AudioMeterUiConsumer` / `MeterModel`, not from a second independently
implemented timer in the host. `MeterPresentationAdapter` copies the 17-channel
state into a `MeterPresentationFrame` before calling the sink; it never exposes
references to queue storage. A sink that enqueues asynchronous UI work must
copy that frame before returning.

The browser prototype currently validates the sequence, channel count and
numeric ranges before updating meters. A native wrapper that dispatches
`mpc-meter-snapshot` can implement `MeterPresentationSink`; it must do so on
the browser's owning/UI thread and pass a single complete frame. The adapter's
presentation `sequence` is the field to forward as the browser event sequence;
`sourceBlockSequence` is diagnostic metadata. It should coalesce pending presentation work rather
than queue an unbounded number of JavaScript tasks.

## Refresh and overload policy

- Keep the audio snapshot rate tied to audio blocks; do not add UI work to the
  audio callback.
- Poll/drain on the UI/control thread and present at a bounded visual cadence
  (initial target: 30–60 Hz, to be measured on the actual target).
- Drain all available SPSC snapshots per tick so peak hold observes transient
  peaks; render only the newest resulting state.
- If the UI is late, skip intermediate paints rather than accumulating stale
  frames. The SPSC producer already drops the newest frame when its queue is
  full; it never blocks the audio thread.
- Reject non-finite/out-of-range channel values and non-increasing sequence
  numbers at the presentation boundary as defensive validation.

## Lifecycle and threading requirements

1. Construct the bridge, UI consumer and host adapter before starting the
   audio callback; destroy them only after the callback and UI timer have
   stopped.
2. Keep exactly one producer and one consumer for the SPSC bridge.
3. Run `AudioMeterUiConsumer::tick` and all presentation callbacks on the
   selected UI/control thread, never on the audio callback.
4. Stop the UI timer before tearing down the consumer or its bridge.
5. Do not let JavaScript or framework callbacks retain references to C++
   snapshot objects. Transfer values by copy.
6. Treat a failed/closed presentation target as a UI-side condition; it must
   never stall or reconfigure the audio engine.

## Transport selection gates

Do not implement a concrete transport until repository evidence identifies:

- the actual host process and UI runtime that will own the mixer;
- whether the browser prototype will remain static, be embedded, or be
  replaced by a native UI;
- the supported local communication API and its availability in the exact
  target SDK/sysroot;
- a reversible launch/lifecycle model that does not replace or interfere with
  the stock MPC application;
- a host-side test strategy for startup, shutdown, dropped frames, stale
  sequence numbers and UI-thread affinity.

The target UI framework and launch path remain unverified. The abstract
sink and fake-sink test are host-side only; no concrete UI or IPC transport has
been selected. The ARMv7 CI profile
is a cross-compilation check, not proof that a binary can run on the MPC Live II.

## Validation scope

Host-side tests can verify snapshot contents, queue behavior, consumer
threading contracts, sequence validation and a fake presentation sink. They
cannot prove the target's graphics, touch, audio-device access, real-time
behavior, or coexistence with MPC. No hardware test is implied by this design.
