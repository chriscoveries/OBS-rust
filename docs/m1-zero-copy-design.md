# M1 Metal to VideoToolbox texture path

## Audit

At the audited base (`215a1dfd8`), VideoToolbox registered only `.encode = vt_encode` at `plugins/mac-videotoolbox/encoder.c:1429-1439`; it did not advertise `OBS_ENCODER_CAP_PASS_TEXTURE`. Core therefore selected its raw route in `libobs/obs-encoder.c:219-225`. For raw output, `libobs/obs-video.c:889-908` maps staged planes through `download_frame()` (`:580-598`). Metal stages into a shared `MTLBuffer` and exposes its CPU pointer in `libobs-metal/metal-stagesurf.swift:109-119`. `vt_encode()` then copied the frame planes into VideoToolbox's pixel-buffer pool at `plugins/mac-videotoolbox/encoder.c:1131-1155` through Muse's `vt_copy_frame_to_pixbuf()` seam.

OpenGL already had NV12/P010 texture support and IOSurface sharing, but VideoToolbox did not request that route. This patch opts in only the Metal VideoToolbox instance after a pixel-format and IOSurface preflight. OpenGL remains on the existing raw route.

**Impact cap:** macOS still defaults to OpenGL (`OBSApp.cpp:338-339`), so users benefit from this change only when they select the Metal renderer. M1b follow-up: add the VideoToolbox texture path on OpenGL using its existing IOSurface sharing (`61ea6e024`); fork #55 tracks the separate Metal-default discussion.

This wires an existing encoder interface rather than inventing a new one: the author of obsproject/obs-studio#8312 noted that macOS/OpenGL texture encoding “can/should” be supported. The missing pieces here were Metal's bi-planar IOSurface render targets, VideoToolbox registration, and a lifetime contract for textures retained by asynchronous callbacks.

## Design

- Metal allocates NV12 (`R8Unorm` Y and `RG8Unorm` UV) or P010 (`R16Unorm` Y and `RG16Unorm` UV) `CVPixelBuffer`s with IOSurface and Metal compatibility. `CVMetalTextureCache` maps each plane; the wrappers retain their `CVPixelBuffer` and `CVMetalTexture` objects. VideoToolbox wraps that same IOSurface as a `CVPixelBuffer`, checks format and dimensions, and receives the existing colorspace/HDR attachments.
- `OBS_ENCODER_CAP_ASYNC_TEXTURE` opts an encoder into IOSurface and lease callbacks. Other encoders receive the same texture callback arguments and recycle their slots immediately. Each VT submission takes a unique token; its output callback releases the token. Core retires a slot after the final duplicate, and returns it to the pool only after every VT callback releases its token.
- The async pool uses stable IOSurface slots and GPU-to-GPU copies from the conversion target. It does not swap the conversion target with a leased slot, so the lease stays attached to the surface passed to VT.
- When no slot is available, core repeats the most recent queued frame if one exists; otherwise it drops the newest frame. It never blocks the render producer on VT latency.
- `queue_frame()` commits the producing work with `gs_flush()`. On the encode thread, Metal commits an empty command buffer on the same command queue and waits for completion before `VTCompressionSessionEncodeFrame()`. This M1 CPU wait preserves the existing synchronous packet-dequeue contract; the M2 work can replace it with an event/fence.
- Unsupported canvas formats, non-Metal renderers, and IOSurface or VT wrapping failures found during startup preflight select the existing raw route. The route logs one fallback message per encoder. Raw input continues through Muse's `vt_copy_frame_to_pixbuf()` seam.

## Validation

`test_texture_lease` covers duplicate submissions, out-of-order and repeated token release, pool exhaustion policy, and stop/shutdown drain. `test_vt_texture_surface` is a windowless macOS test that maps full- and video-range NV12/P010 IOSurface planes through `CVMetalTextureCache`, compares their bytes against the raw copy helper output, and checks that the VT-facing buffer aliases the original IOSurface. For NV12, it encodes each buffer through VideoToolbox, decodes both samples, and compares their active output plane bytes.

The texture callback does not call the copy helper; the raw callback does. The test asserts the raw helper's byte count and proves identical decoded NV12 output from raw-upload and IOSurface-backed inputs. P010 currently has plane-level parity coverage. The test calls VideoToolbox directly rather than exercising the OBS encoder callback/core dispatch; the Mac CI run remains required before this branch meets the owner’s acceptance gate.
