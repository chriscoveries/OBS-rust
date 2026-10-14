# Naming

One name per thing, so releases, crates and tags can't be confused (#320).
`ci/test_naming.py` checks the crate rules in `./lint`.

## Product

- Display name: **OBS-Studio-Rust**. Slug: **`obs-studio-rust`**.
- Release titles, artifact names and the app tag `obs-studio-rust-vX.Y.Z[-pre]`
  use them. `--version` keeps the upstream OBS version ("based on OBS ...").
- The command-line client (an obs-websocket v5 client for streaming control)
  is part of the product: crate and binary `obs-studio-rust` in
  `rust/obs-studio-rust/`. It gets no tag family of its own: published
  binaries go on the app's `obs-studio-rust-v*` releases.

## Rust crates (`rust/<dir>/`, directory name = package name)

| Kind | Name | Examples |
|---|---|---|
| libobs port | `obs-<area>`, named after the libobs directory | `obs-util`, `obs-graphics`, `obs-media-io`, `obs-codec` |
| libobs staticlib (C ABI drop-in) | `libobs-rust` | |
| Plugin staticlib | `obs-<plugin>-plugin` | `obs-nvenc-plugin` |
| Test oracle (dev-only) | `obs-c-oracle` | |
| Product binary | `obs-studio-rust` | |

No other crate takes a `-rust` suffix. A new tool is `obs-<tool>`.

## Tags

- OBS version tags (`X.Y.Z`, `X.Y.Z-betaN`, ...) come from upstream; every
  `git describe` that names an OBS version matches only those
  (`ci/test_describe_match.py`).
- `obs-studio-rust-v*` is the only tag family this fork adds. The retired
  `obs-stream-rust-v*` tag (0.1.0, the CLI's old name) stays for history; no
  new ones are cut.
