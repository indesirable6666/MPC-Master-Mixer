# Audio metering pipeline

## Purpose and current status

This document defines the boundary between block-level DSP measurement and
future mixer UI meters. The repository currently contains two independent
pieces:

- `src/audio/audio_block_meter.hpp` measures peak and RMS for one contiguous
  mono float block. It is stateless, allocation-free, and has no device or
  thread-publication responsibilities.
- `src/mixer/meter_model.hpp` maintains UI-side peak, RMS, peak-hold/decay,
  and clip-latch state. It is not thread-safe and must be owned and updated by
  one UI/control thread.

The bounded transport now exists in `src/audio/audio_meter_snapshot_bridge.hpp`.
It carries complete 17-channel snapshots (16 tracks plus Master) from one audio
producer to one UI/control consumer. Its eight-slot SPSC queue rejects the
newest frame when full, never waits, and exposes an approximate pending count.
The prototype UI is still not connected, and no physical signal is currently
captured.

## Intended data flow

A future validated audio host should use this order:

1. The audio callback receives a valid input block and runs the DSP engine.
2. At the defined meter tap point, it measures the relevant signal block.
3. It publishes a small, fixed-size meter snapshot through a dedicated,
   bounded audio-to-UI handoff.
4. The UI/control thread consumes the newest available snapshot and calls
   `MeterModel::update(peak, rms)`.
5. The UI timer or render loop applies peak decay and draws the meter and clip
   indicator. Rendering must never run on the audio callback.

The tap point must be specified for each displayed meter. A track input meter
should be measured before EQ/inserts; a track output meter should be measured
after inserts, gain, pan, and mute/solo decisions. Aux return meters should be
measured after return gain/balance/mute. The master meter should be measured
at the master output tap, with a clear distinction between pre-limiter and
post-limiter levels if a limiter is later added.

## Threading and real-time requirements

The audio callback must not access UI widgets or call rendering code. The
meter transport must be separate from the existing control-to-audio command
bridge: the latter has a single control-thread producer and audio-thread
consumer, whereas meter data flows in the opposite direction.

The current transport and any future extensions must preserve these rules:

- Have one audio-thread producer and one UI/control-thread consumer, or
  explicitly serialize any additional producers/consumers.
- Use a fixed-capacity structure with bounded work and a documented overflow
  policy (prefer dropping stale meter frames over blocking the audio thread).
- Avoid allocation, mutexes, logging, waits, and device calls in the callback.
- Publish complete snapshots so the UI never observes partially updated
  multi-track data.
- Include a monotonically increasing block sequence or sample position so the
  UI can identify stale or skipped updates.
- Keep peak/RMS values normalized and finite; define whether over-range values
  are clamped for display and preserve a separate clip indication if needed.
- Keep all `MeterModel` mutation on the UI/control thread.

Do not use unsynchronized shared floats or ordinary non-atomic structs as a
cross-thread shortcut. A future implementation must document its memory-order
and ownership rules and include stress-oriented tests for wraparound,
full/empty behavior, and producer/consumer ordering.

## Measurement semantics

`AudioBlockMeter::measure` currently computes absolute peak and RMS over one
mono block. It rejects null pointers, empty blocks, and any block containing
NaN or infinity; on rejection, it leaves the caller's previous result intact.
Peak and RMS are clamped to [0, 1] for meter presentation. This clamp is not a
limiter and does not alter the audio signal.

For stereo buses, the host must define whether left and right are displayed as
separate meters or combined. If combined, use the maximum channel peak and a
documented RMS aggregation; do not silently average peaks. For multi-channel
inputs, channel mapping must be explicit and independently validated.

`MeterModel::update` accepts finite non-negative values, clamps them to
full scale, holds the maximum peak until decay is applied, and latches clip
when the held peak reaches its threshold. Its decay amount is currently a
caller-supplied linear amplitude decrement; a UI integration should derive
that decrement from elapsed time rather than assume a fixed frame rate.

## Verification gates before UI or hardware integration

1. **Completed:** add the dedicated audio-to-UI snapshot transport without
   changing the existing control-to-audio SPSC contract.
2. **Completed in host tests:** verify empty/full behavior, accepted-frame
   integrity, FIFO ordering, and repeated ring-index wraparound. The test is
   part of host CI and is cross-compiled for ARMv7.
3. Add explicit meter tap points to the offline duplex host and test expected
   values with synthetic signals.
4. Connect the native UI to the UI-thread consumer and verify refresh/decay
   behavior without blocking DSP.
5. Only after a separate, authorized hardware phase, validate actual capture,
   routing, latency, and meter calibration on the MPC.

Until those gates are completed, meters are software models and helpers only.
They must not be described as live MPC input/output meters.
