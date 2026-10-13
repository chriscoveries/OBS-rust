# obs-websocket v5 port boundary

This is the Phase 5 design decision for #147. Port the protocol and session
core first, behind the existing C++ plugin. Keep the WebSocket++/Asio listener,
OBS request and event handlers, plugin API, configuration, and Qt/frontend UI
in C++ during that stage. A whole-plugin rewrite would mix protocol behavior
with Qt and OBS lifecycle changes, making a Tier 4 failure hard to isolate.

## First Rust unit

The Rust core owns v5 message envelopes, Hello/Identify/Reidentify state,
authentication validation, subscription masks, request and batch validation,
response envelope construction, and protocol close/status codes. The C++
listener owns connections and passes frame bytes, opcode, and a stable opaque
session handle across a private C ABI. Rust returns owned response bytes or a
close code; a paired free function releases every returned buffer. The
boundary must not pass `nlohmann::json`, Qt objects, C++ exceptions, or OBS
object pointers into Rust. C++ dispatch callbacks execute the existing
request handlers and return serialized results; events still originate in the
existing C++ handler and pass through the core for subscription filtering and
envelope construction. Document ownership, buffer limits, and callback
reentrancy in the C ABI before implementing it.

The public `obs-websocket-api` and wire protocol stay compatible. The plugin
does not acquire a new public Rust ABI. Keep the existing C++ path buildable
behind a switch until the Rust path passes the gates below; remove it only
under the release and rollback criteria.

## Threads and lifecycle

WebSocket++ continues to run on its server I/O thread. Its message handler
may hand frames to the existing Qt thread pool. Each session is processed
serially, preserving Identify before requests and the current per-session
operation order. Distinct sessions may run concurrently. The Rust core owns
only session state and never calls Qt or OBS directly; the C++ dispatch layer
preserves the current main-thread routing required by individual handlers.
Batch execution keeps the current serial and parallel modes. Stop accepting
new work before draining workers and destroying sessions; no callback may
cross into a destroyed plugin or Rust core. Add concurrency and shutdown
tests alongside protocol tests, including disconnect during a batch.

## Security and acceptance

Fix #99 in the existing C++ path before swapping the parser. Bound both frame
size and JSON/MsgPack nesting depth before recursive parsing, including
pre-auth frames. Apply the same limits in Rust and add wire tests for both
formats at and beyond the limits. Reject malformed frames without crashing
the OBS process.

Use the unchanged `test/websocket-tier4` suite as the acceptance oracle. It
already passed 179 checks, including all 147 documented request types,
against the C++ server. Run it against the C++ and Rust paths using the same
OBS build/profile setup; compare envelopes, close and status codes, auth,
events, and batch behavior. Unit tests exercise pure Rust state and malformed
input; differential tests feed the same frames to both protocol cores where
deterministic. #140 supplies a full `ENABLE_RUST_LIBOBS=ON` OBS build and a
headless Tier 4 CI job. Keep the C++ baseline job while the Rust path is
being introduced, and run the Rust job before merging the swap. The local
gate remains `./lint`, `./unittest`, and the stamped local gate; Tier 4 is an
additional full-app gate for this port.

After this unit is stable, claim request-handler groups and event-handler
groups separately. Each group needs the applicable four test tiers and must
keep Qt/frontend access in a C++ adapter until that behavior has its own
acceptance coverage. Do not replace the listener or UI as part of the first
protocol-core PR.
