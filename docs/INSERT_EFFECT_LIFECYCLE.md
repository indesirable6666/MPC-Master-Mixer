# Native insert-effect ownership

## Owner component

`MixerInsertEffectOwner` is the control-thread ownership layer for the
non-owning pointers published by `MixerController`. It owns one current and
at most one pending `std::unique_ptr<AudioInsert>` for every track insert
slot and each of the two Audio Input pair sides.

- `publish(track, slot, candidate, ticket)` transfers ownership only after
  the controller successfully queues the pointer. On failure, the caller
  retains the candidate.
- `clear(track, slot, ticket)` queues a null pointer but retains the current
  effect until acknowledgement.
- `collectApplied()` polls tickets and promotes pending to current only after
  acknowledgement. The replaced effect is destroyed on the control thread.
- `requestClearAll()` incrementally queues null pointers for every populated
  track and Audio Input pair slot. Call it repeatedly with `collectApplied()`
  while the callback is running; it returns false while any slot has a handoff
  pending, a populated slot needs a clear queued, or a clear could not be queued
  (for example, the bounded command queue is full). It returns true only when
  no current/pending effect remains and no handoff is outstanding. Check
  `allCleared()` as the explicit teardown precondition.
- `allCleared()` becomes true only when all current/pending owners are empty
  and all pointer handoffs have been acknowledged. It is the owner's
  pre-shutdown check, not a substitute for stopping the backend.
- `publishAudioInputPair` and `clearAudioInputPair` provide the same contract
  for the two Audio Input pair sides.
- Track and Audio Input pair slots are independent; calls must still be
  serialized on the one SPSC producer/control thread. All pointer updates for
  slots managed by this owner must go through it; bypassing the owner with
  `MixerController`'s raw-pointer setters invalidates its lifetime guarantees.

Do not mutate a published effect. Prepare a replacement and publish it.
Queue-full rejection leaves the candidate and current slot unchanged.

## Shutdown ordering

The owner cannot stop an arbitrary audio backend. Before teardown, keep the
callback running and repeat `requestClearAll()` followed by `collectApplied()`
until `allCleared()` returns true. This drains existing pending publications,
queues clears for populated track and Audio Input pair slots, and retries
safely under queue backpressure. Then stop and shut down the audio host before
destroying the owner. This ordering ensures the engine no longer holds insert
pointers and no callback can race destruction. A queued clear alone is not a
shutdown barrier; stopping the callback before the clears are applied leaves
the engine's last insert pointers live.

## Validation

`mixer_insert_effect_owner_test` uses the offline command bridge to verify
replacement and clear acknowledgements, delayed destruction, independent
slots, queue-full rollback, and invalid-coordinate rejection. It does not
open an audio device or exercise MPC hardware.
