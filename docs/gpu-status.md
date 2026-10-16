# GPU acceleration status light

OBS-Studio-Rust shows a light in the status bar that says whether video is
rendered on a hardware GPU (obs-rust/obs-studio#846). Clicking it opens a
dialog that explains the state and lists likely causes. The same state is
logged at startup and returned by obs-websocket as `GetStats.gpuAcceleration`.

| Light | Meaning |
|---|---|
| Green | The active adapter is a hardware GPU. |
| Yellow | A GPU (NVIDIA, AMD or Intel) is present, but OBS renders on a software renderer (llvmpipe, softpipe, Microsoft Basic Render Driver, SwiftShader). |
| Red | Software rendering with no GPU detected, graphics initialization failed, or the average frame time is over the frame interval. |

The red state, mocked up (the wording is exact, the styling approximate):

![Status bar light, dialog and startup log in the red state](images/gpu-status-red.png)

## Where it lives

- Classifier, GPU detection (`/sys/class/drm`, `/dev/dri`, `nvidia-smi`) and
  the dialog text: `rust/obs-gpu-status`.
- Light and dialog: `frontend/utility/GpuStatus.cpp` and
  `frontend/widgets/OBSBasicStatusBar.cpp`. They are built only with
  `ENABLE_RUST_LIBOBS=ON`; other builds show no light.
- `GetStats.gpuAcceleration`: obs-websocket reads it from the frontend's
  `get_gpu_status` proc, and it is `null` when nothing provides it.
- Tier 4 check (OpenGL forced onto llvmpipe → not green):
  `test/websocket-tier4/run_tier4.py`.
