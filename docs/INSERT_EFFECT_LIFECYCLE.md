# Insert effect replacement lifecycle

## Ownership and publication

`AudioInsert` instances are caller-owned. `MixerController::setTrackInsert()`
and `AudioEngineCommandBridge::enqueueTrackInsert()` enqueue a non-owning
pointer; neither takes ownership, copies the object, nor deletes it.

Treat a slot update as a two-phase handoff:

1. On the serialized control thread, construct and fully configure the new
   effect. Do not change its parameters after publication.
2. Enqueue the pointer and retain both the old and new objects.
3. Let the audio callback drain the command at a block boundary.
4. Poll `MixerController::isInsertUpdateApplied(ticket)` (or
   `AudioEngineCommandBridge::hasApplied(ticket)`) from the control thread.
5. Only after it returns true may the old object be destroyed or reused.
   The new object must remain alive until a later replacement is acknowledged
   and the callback has stopped referencing it.

Do not block or wait for the acknowledgement on the audio thread. Do not
allocate or destroy effects in the callback. Do not mutate a published effect:
to change gain, drive, or bypass, prepare a replacement object and repeat the
handoff.

## Serialize updates per slot

The native `MixerController` enforces at most one unacknowledged replacement
per insert slot: a second call for that slot returns `false` until the first
ticket is acknowledged. This keeps the ownership handoff unambiguous and avoids
accidentally retiring an intermediate effect while a later queued command still
refers to it. Direct users of `AudioEngineCommandBridge` must enforce the same
rule themselves. The command bridge is
SPSC: all enqueue and acknowledgement polling for a controller must be
serialized on its one producer/control thread.

`MixerController::desiredStateSnapshot()` is a producer-side desired-state
view. Its insert pointers reflect successfully enqueued requests, not
necessarily the effect currently active in the audio engine. The ticket
acknowledgement is the signal that the queued pointer swap has actually
happened.

## Clearing a slot

Pass `nullptr` to enqueue a clear. Retain the previous object until the clear
ticket is acknowledged, then destroy it off the audio thread.

## Example: prepare and publish a Gain insert

```cpp
// All operations below are on the serialized control thread.
// `current` owns the effect currently published in this slot.
// Keep `candidate` alive while its pointer is queued and until it becomes current.
auto candidate = std::unique_ptr<GainInsert>(new GainInsert());
candidate->setGain(1.5f);
candidate->setEnabled(true);

std::uint32_t ticket = 0;
if (controller.setTrackInsert(track, slot, candidate.get(), ticket)) {
    // Keep current and candidate alive. Poll on a later control/UI iteration.
    // Once controller.isInsertUpdateApplied(ticket) is true:
    //     current = std::move(candidate); // old current is retired here
}
```

The example intentionally leaves polling to the host's control loop. Do not
spin-wait on the UI thread, and do not infer application merely because enqueue
returned true. If enqueue returns false (for example, a full queue), the
candidate was not published and may be discarded on the control thread.

## Scope

This is a native C++ ownership/synchronization contract only. It does not
connect the browser prototype to the controller, instantiate effects in the
MPC application, or provide live MPC audio I/O.
