# Security policy

obs-rust is a fork of OBS Studio that is being converted to Rust. This
policy covers obs-rust: this repository's code, its CI, and the builds
it publishes. The upstream OBS Project has its own policy and channels
(see [Upstream OBS Studio](#upstream-obs-studio)).

## Reporting a vulnerability

**Do not open a public issue, pull request or discussion for a
suspected vulnerability.**

Report it privately through GitHub:

1. Open the repository's **Security** tab and choose **Report a
   vulnerability**
   (<https://github.com/obs-rust/obs-studio/security/advisories/new>).
2. Say what is affected (file, function, build or release), how to
   reproduce it, and what an attacker gains. A short proof of concept
   helps.
3. If you already have a fix, say so. A maintainer will open a
   temporary private fork on the advisory, and you can push the fix
   there. Do not push it to a public branch or fork.

This applies at every severity, and to code obs-rust inherited from
upstream as well as to code it added. A maintainer decides whether a
report can be handled in public, not the reporter.

## What happens next

- **Within 72 hours:** a maintainer acknowledges the report on the
  advisory.
- **Triage:** we confirm the issue and agree on its severity (CVSS)
  with you, on the advisory.
- **Fix:** the fix is developed and reviewed in the advisory's private
  fork. It follows the normal rules for this repository: a failing test
  that shows the bug first, then the fix (see
  [docs/rust-port/testing-policy.md](docs/rust-port/testing-policy.md)).
- **Release and disclosure:** we merge the fix, publish the advisory
  (requesting a CVE where it applies), and credit you unless you ask us
  not to. We aim to disclose within 90 days of the report, sooner once
  a fixed build is out.

After disclosure, any public follow-up work (hardening, regression
tests, release notes) is tracked in normal issues with the `security`
label. The label is never used for an undisclosed vulnerability.

## Upstream OBS Studio

Much of obs-rust's C/C++ code comes from OBS Studio. If a vulnerability
also affects upstream OBS Studio:

- Report it to obs-rust as described above. You do not need to report
  it upstream yourself.
- The obs-rust maintainers report it to the OBS Project through its
  official channel (security@obsproject.com) and coordinate disclosure,
  so that neither project publishes the details before the other has a
  fix. You are credited in both reports unless you ask us not to.
- If you have already reported it upstream, tell us, so that we follow
  the same disclosure timeline.

A bug that affects only upstream OBS Studio, and not obs-rust, should
go to the OBS Project directly: <https://github.com/obsproject/obs-studio/security/policy>.

## Scope

In scope:

- Code in this repository, including the Rust ports, the C/C++ code
  inherited from upstream, plugins and scripting.
- Network-facing interfaces, such as the obs-websocket server, RTMP,
  SRT, WHIP and other outputs, and media inputs that parse files or
  streams.
- Builds and release artifacts published by this repository (AppImage,
  Flatpak, DMG, Windows packages) and the CI that produces them.

Out of scope:

- Attacks that require physical access to the device, or that require
  the victim to install non-standard software or take active steps to
  make themselves vulnerable (this includes DLL planting).
- Reports from automated tools or scans that have not been checked by
  hand.
- Vulnerabilities in third-party dependencies that obs-rust does not
  change. Report those to the dependency's maintainers; tell us as
  well if obs-rust ships an affected version.

## Safe harbor

We will not pursue or support legal action against research done in
good faith under this policy. Good faith means avoiding privacy
violations, data destruction and service disruption, testing only
against your own installations, and keeping details confidential until
the advisory is published.

## No bug bounty

obs-rust does not offer payment for reports.
