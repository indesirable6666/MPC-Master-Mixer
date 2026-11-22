# Native insert-effect ownership

## Owner component

`MixerInsertEffectOwner` is the control-thread ownership layer for the
non-owning pointers published by `MixerController`. It owns one current and
at most one pending `std::unique_ptr<AudioInsert>` per track/slot.

- `publish(track, slot, candidate, ticket)` transfers ownership only after
  the controller successfully queues the pointer. On failure, the caller
  retains the candidate.
- `clear(track, slot, ticket)` queues a null pointer but retains the current
  effect until acknowledgement.
- `collectApplied()` polls tickets and promotes pending to current only after
  acknowledgement. The replaced effect is destroyed on the control thread.
- Slots are independent; calls must still be serialized on the one SPSC
  producer/control thread.

Do not mutate a published effect. Prepare a replacement and publish it.
Queue-full rejection leaves the candidate and current slot unchanged.

## Shutdown ordering

The owner cannot stop an arbitrary audio backend. Stop and shut down the audio
host, ensuring no callback can still dereference inserts, before destroying
this owner. A queued clear alone is not a shutdown barrier.

## Validation

`mixer_insert_effect_owner_test` uses the offline command bridge to verify
replacement and clear acknowledgements, delayed destruction, independent
slots, queue-full rollback, and invalid-coordinate rejection. It does not
open an audio device or exercise MPC hardware.
