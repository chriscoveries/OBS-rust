# Agent instructions

This repository is governed by the [Constitution of the obs-rust Project](https://github.com/obs-rust/constitution/blob/main/CONSTITUTION.md), which supersedes every other document here, this one included.

This repository is being converted from C/C++ to Rust while keeping the public
API (libobs C API/ABI, obs-websocket v5 protocol, frontend and scripting APIs)
compatible.

## Mandatory reading

Before porting any code, writing any test, or touching `rust/`, `Cargo.toml`,
or `test/`, read and follow
[docs/rust-port/testing-policy.md](docs/rust-port/testing-policy.md) exactly.
It defines the four required test tiers (Rust unit tests, unchanged C tests
against the Rust implementation, differential tests against the original C,
and black-box protocol tests) and the per-unit workflow.

Before picking up or filing an issue, or opening a PR, read
[docs/rust-port/coordination.md](docs/rust-port/coordination.md): claim
before coding, search open PRs first, target master, RED then GREEN commits.

## Local gate (run before every push)

- `./lint` (8 s): clang-format 22.1.3 and gersemi 0.25.0 pinned via uvx, rustfmt, clippy. `./lint --fix` rewrites files.
- `./unittest`: Rust Tiers 1-3, then Tier 2 (cmocka with `ENABLE_RUST_LIBOBS` OFF/ON in Docker; ~30-40 s warm thanks to ccache).
- To let CI skip the Linux unit-test jobs, commit, then run
  `uvx --from git+https://github.com/zackees/ci.yml@1a970f01574447d681571ff3d0b8de56cb983dcc ci-lint local-gate run`.
  It runs every lane in `local-gate.toml` (unchanged lanes are cached) and stamps HEAD with attestation trailers. Push the stamped commit as-is; any later change invalidates the stamp.
- PRs that only touch `rust/`, `test/cmocka/`, the local gate or docs skip the full OBS builds (quick gate in `build-project.yaml`). Add the `ci-full` label to force them.

## Quick rules

- Use `soldr cargo ...`, never bare `cargo`.
- Pin `rust-toolchain.toml` to the latest Rust the CI soldr version supports natively, never beyond; bump it only after a soldr bump (see the testing policy).
- Never edit existing C tests or public C headers to make a port pass.
- Every port starts RED (failing test against stubs) and ends GREEN.
- Do not add new GitHub Actions workflow files without explicit sign-off.
