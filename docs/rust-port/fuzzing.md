# Scheduled differential fuzzing

The cargo-fuzz differential targets in `rust/obs-codec/fuzz/` compare the Rust
ports with the original C (via `obs-c-oracle`). They run nightly from a local
cron job, not from GitHub Actions.

| Target | Compares | Documented exclusions |
|---|---|---|
| `avc_diff`, `hevc_diff`, `av1_differential` | bitstream parsers | listed in each target's header comment |
| `nal_diff` | `obs_nal_find_startcode`: C, Rust C ABI, safe Rust | NULL, near-zero and reversed ranges are not generated (C UB). Inputs sit inside a padded allocation at four alignments. |
| `config_ini_diff` | `config_open_string`, section order and string lookups | Duplicate section names and keys are compared (uthash model, #681). File I/O and NULL handles are not exercised. |

AMF has no Rust port yet. Add a target when it lands.

## Requirements

- Linux (the runner uses `flock`), `soldr`, `uv`, and a C toolchain for
  `obs-c-oracle`.
- The pinned nightly and cargo-fuzz:

  ```sh
  soldr rustup toolchain install nightly-2026-10-14 --profile minimal
  soldr cargo install cargo-fuzz --locked
  ```

## Running

```sh
uv run rust/tools/fuzz/run.py --state-dir ~/.local/state/obs-fuzz          # 1 h per target
uv run rust/tools/fuzz/run.py --state-dir /tmp/f --seconds 60 --target nal_diff
```

The runner builds every target, then runs each one in turn with
`-max_total_time`, `-max_len=4096`, `-timeout=30` and `-rss_limit_mb=2048`. A
failing target does not stop the run. The runner exits 1 if the build or any
target failed, and 75 if another run holds the state directory's lock.

Nightly cron entry (01:00 local time, all targets). Cron has a minimal
`PATH`, so set one that finds `uv` and `soldr`:

```cron
PATH=/home/you/.cargo/bin:/home/you/.local/bin:/usr/local/bin:/usr/bin:/bin
0 1 * * * mkdir -p $HOME/.local/state/obs-fuzz && cd $HOME/dev/obs-rust/obs-studio && git pull --ff-only -q && uv run rust/tools/fuzz/run.py --state-dir $HOME/.local/state/obs-fuzz >> $HOME/.local/state/obs-fuzz/cron.log 2>&1
```

## State directory

- `corpus/<target>/`: the libFuzzer corpus, kept and grown across runs. Back
  it up. Never delete it to "fix" a run.
- `artifacts/<target>/`: crash, timeout and OOM inputs.
- `runs/<UTC stamp>/`: `build.log`, one log per target, and `summary.json`.
  The summary records HEAD, dirty files, toolchain, `rustc -V`, the cargo-fuzz
  version, and each command with its exit code.

## Handling a finding

```sh
cd rust/obs-codec
soldr cargo +nightly-2026-10-14 fuzz run <target> <state>/artifacts/<target>/crash-...   # replay
soldr cargo +nightly-2026-10-14 fuzz tmin <target> <state>/artifacts/<target>/crash-...  # minimise
```

Find out whether the mismatch is a port bug or a harness bug before changing
anything. For a port bug, file an issue with the minimised input and
regression-test it in Tier 3. Exclude it from the target only with a comment
that links the issue.
