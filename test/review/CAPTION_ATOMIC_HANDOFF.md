# Caption scheduler: atomic rejection and frame limits

Owner-review only. Base: `2fad7d9125e5e409beaf97e49f9cfa7f59c611f7`.
Do not merge into the base branch or send upstream changes without approval.
The base branch backs the owner's existing upstream caption draft.

## Bugs addressed

`word()` used to advance `last_ts` before validating characters. For example,
accepting a word at 1 second, rejecting `A*B` at 10 seconds, then accepting a
valid word at 2 seconds incorrectly produced `NonMonotonicTimestamp`.

The frame conversion also saturated very large finite floats to `u32::MAX`,
and `put()` incremented a `u32` cursor without checking overflow. Debug builds
could panic; release arithmetic could wrap. A failure while emitting several
pairs could leave a partial word or one half of a doubled control queued.

## Implemented

Stage only the current word, commit its pairs/cursor/timestamp together after
success, and discard the staging buffer on any error. The accumulated queue
is never cloned. Check the rounded timestamp before narrowing to `u32`.
Use a `u64` internal cursor with checked `u32` emission so the last legal
frame remains usable without wrapping the next slot.

`RollupError` gains `FrameIndexOverflow`; `ScheduledPair` stays `(u32, u16)`.
This is a new Rust error variant, so an external exhaustive match would need
an extra arm. No C API, FFI or OBS integration is added. Ordinary successful
word spacing/wrapping and the 30/60 fps schedules remain as before.

Ten new regression tests cover rollback, invalid timestamps, large finite
timestamps, partial-word/control overflow and the final 30/60 fps frame slots.
The existing decode-back tests remain unchanged.

## Run

```sh
git fetch https://github.com/chriscoveries/obsrust-fork.git review/caption-atomic-20261015
git worktree add --detach ../obsrust-caption-review FETCH_HEAD
cd ../obsrust-caption-review
bash test/review/run-caption-atomic.sh
```

The runner uses `soldr`, executes all crate tests in debug and release, then
runs clippy and rustfmt checks. It writes logs to `build_review_captions/`.
A GPU, speech recognizer or running OBS instance is unnecessary. Cargo may
need normal dependency/toolchain cache access. No dependencies are added.

## RED checkpoint

`5d2b08f1c2e9efdb2ace360d70bdcd854b740293` contains only the ten new tests.

```sh
git worktree add --detach ../obsrust-caption-red 5d2b08f1c2e9efdb2ace360d70bdcd854b740293
cd ../obsrust-caption-red
soldr cargo test -p obs-captions --locked --test rollup_atomic
soldr cargo test -p obs-captions --locked --release --test rollup_atomic
```

Expected RED includes `unmappable_word_does_not_advance_the_timestamp` and
frame-limit failures. Environment/build errors do not establish RED.
The fixed checkpoint must pass these and all existing tests in both profiles.
Afterward run the repository's `./lint` and record the exact tested SHA.

## Validation status

The source and test logic have been reviewed here, including floating-point
boundary calculations. The shell runner passes `bash -n`.
This container has no Rust compiler: these Rust tests have NOT been compiled
or executed here. The Mac agent must supply that result before promotion.

The original emitter's integration into OBS and fractional frame-rate
support remain separate work. This is a bounded correctness patch.
