<!-- docs/rust-port/coordination.md -->
Fixes #  <!-- or Refs # when this does not complete the issue -->

### What and why

### RED → GREEN
- RED commit: <!-- SHA, and how it fails (test names / counts) -->
- GREEN: <!-- result, e.g. Tier 2 N/N with ENABLE_RUST_LIBOBS OFF and ON -->

### Checklist
- [ ] Base is `master` (or the description starts with a **Stacked:** note)
- [ ] I claimed the issue, or no one else has an open PR for it
- [ ] Characterization test (if the unit had none), then RED, then GREEN commits
- [ ] (Optional) Local gate passed and the pushed head is stamped; otherwise PR CI runs the full suite
- [ ] Warning or flag changes (`-W...`, ratchets): built with the `ci-full` label, or with scripting ON and cross-checked with clang and MSVC
- [ ] Intentional differences from C listed in the parity test header and rust/README.md (ports)
