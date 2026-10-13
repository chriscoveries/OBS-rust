# Rust implementation release criteria

This is the decision for #141. A port is **complete** when the applicable
four testing tiers in [testing-policy.md](testing-policy.md) pass. It is
**shipped** when a release artifact uses that Rust implementation by default.
Passing a unit test or building a Rust static library alone does not ship it.

## Flip `ENABLE_RUST_LIBOBS` to ON by default

The default changes in a separate PR only after all of these gates pass on a
release candidate. Keep an explicit OFF option for rollback during the first
ON-default release.

1. **Compatibility:** Tier 2 passes with OFF and ON on Linux, macOS, and
   Windows, on both x64 and arm64 wherever this project ships that platform.
   Both configurations export the same public libobs symbols and pass the
   unchanged C tests. Record the CI runs and release-candidate commit.
2. **Running application:** a full ON app builds and launches on every
   supported release platform. The unchanged obs-websocket Tier 4 suite
   passes against both the C++ and ON servers. A headless smoke run loads a
   scene collection containing a text source, records five seconds, and
   exits cleanly. Record artifacts and logs, not just a green job summary.
3. **Performance:** record the C and Rust results from #142 on the same
   machine, toolchain, build profile, and input set. No tracked hot-path
   benchmark may regress by more than 10% in median time or throughput;
   the full-app recording workload may not regress by more than 5% in frame
   time or dropped-frame rate. Repeat each benchmark enough times to report
   a median and its run-to-run range. A regression over a bound blocks the
   flip until fixed or explicitly accepted in a separate issue with the
   measured user impact.
4. **Nightly stability:** run ON release-like artifacts on every supported
   release platform for 14 consecutive daily builds. Each build must pass
   the startup, scene-load, recording, and clean-exit smoke run. Archive the
   logs and crash reports. A new crash signature attributable to the Rust
   build or a failed smoke run resets the 14-day count after its fix. Review
   open release-blocking issues before the flip.

If a platform cannot yet meet these gates, the default remains OFF for that
platform. State the platform-specific exception and its evidence in the
default-flip PR; do not infer success from another platform's runs.

## Retire the C implementation of a unit

Keep the original C file while the global OFF configuration still needs it.
After one stable release with ON as the default, a separate PR may retire the
OFF fallback for a unit only when the supported OFF build remains linkable
through another implementation or the OFF option itself is retired. That PR
must show the unchanged C tests, public symbol/layout checks, and Tier 3
parity on the supported configurations. Move the original C source to
`rust/obs-c-oracle/legacy/` in the same PR, update the oracle wrapper to use
it, and keep differential tests running. Preserve the file's upstream
copyright and license notices. Removing a production C file must not silently
remove the behavioral baseline used to test its Rust replacement.
