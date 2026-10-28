# ReiMS/QEMU → Sunshine capture adapter (experimental, parallel checkout)

**Status (2026-10-28):** Isolated **video-only source prototype**. Not a deployed Moonlight stream or production Sunshine build. Do not point at the live ReiMS VM without coordinator approval. Contains no host/guest runtime modifications.

## Existing work reused

- [LizardByte/Sunshine](https://github.com/LizardByte/Sunshine) capture interface `platf::display_t`, platform Linux backend registration, NVENC encoder.
- [rdmitry0911/qsm-rd](https://github.com/rdmitry0911/qsm-rd) QEMU Display1 D-Bus receiver, shared-memory scanout, updated rectangle handling, DMA-BUF optional support, audio and input methods, replay/fake-QEMU tests. This is prior art for direct **WebRTC** streaming, not an existing Moonlight/Sunshine integration.
- QSM QEMU-side tests and fake-QEMU D-Bus integration tests passed on HPSERVER; bridge tests validate BGRA conversion, frame stride, stale-frame dropping, resize, fatal errors, and shutdown.

Both upstream source checkouts are pinned by their own git revisions; do not copy QSM's GPL code into a different-license project without preserving licensing/notices.

## Source layout

- `bridge/src/capture_feed.*`: independent C++20 frame feed that receives QSM FrameToken, latest-only buffering, validates pixman format and dimensions, copies into Sunshine-compatible top-origin BGRA destination.
- `Sunshine/src/platform/linux/qemudbusgrab.cpp`: video capture backend implementing Sunshine's `platf::display_t`; reads QEMU D-Bus directly, uses existing encoder abstraction.
- `Sunshine/src/platform/linux/misc.cpp`: opt-in backend selection `capture=qemu`.
- `Sunshine/cmake/compile_definitions/linux.cmake`: opt-in `-DSUNSHINE_ENABLE_QEMU_DBUS=ON` and links QSM libraries.
- `bridge/tests`: independently runnable C++ assertions and a narrow Sunshine API compilation stub. Stubs **do not** replace a production Sunshine build.

## Reproduce current tests on HPSERVER

```sh
cmake -S bridge -B build-bridge -DCMAKE_BUILD_TYPE=Release
cmake --build build-bridge -j6
ctest --test-dir build-bridge --output-on-failure
cmake -S qsm-rd/qsm-direct -B build-qsm -DQMDP_ENABLE_REAL_QEMU_E2E=OFF -DQMDP_ENABLE_DMABUF_READBACK=OFF
cmake --build build-qsm -j6
ctest --test-dir build-qsm -R '^qmdp_' --output-on-failure
```

## Wiring for a later *approved, reversible* test

1. Build the full Sunshine fork with `-DSUNSHINE_ENABLE_QEMU_DBUS=ON`; source is experimental, and complete Sunshine build **has not been performed** on HPSERVER because its full third-party dependencies/toolchain are absent.
2. Run QEMU with Display1 on a private shared UNIX socket, e.g. `-display dbus,addr=unix:path=/run/reims-display/qemu.sock`. Verify this launch mode against the ReiMS `apple-gfx-pci` CPU framebuffer before affecting the live macOS guest. Some display backends cannot co-exist with VNC; keep a rollback profile.
3. Give a Sunshine-derived sidecar read/write access to that Unix socket. Set `QEMU_DBUS_ADDRESS=unix:path=/run/reims-display/qemu.sock`, `QEMU_DBUS_DESTINATION=` (empty for peer mode), and Sunshine `capture=qemu`.
4. Explicitly select the **healthy** host RTX2080Ti NVENC device; do not use the broken eGPU. Enable video-only test first.
5. Route Moonlight input to QEMU's Display1 keyboard/mouse functions; current Sunshine uses host VirtualHID, **not wired** in this prototype. Do not enable host input forwarding until isolated key/button mapping and releases have been tested.
6. Expose QEMU USB audio and route to a private PipeWire sink, then Sunshine audio capture with A/V timing. No audio device or PipeWire routing installed here.
7. Benchmark frame age from QEMU readback through Sunshine encode/transport to client, reconnect, resize, stable FPS. Remote input-to-photon latency unmeasured.

## Known limitations / next technical gates

- Full Sunshine compilation/link test required with C++23 build dependencies, CUDA and FFmpeg; the stub compilation validates only the backend's method surface.
- Input/gamepads and USB audio are not implemented. The existing game streaming protocol is unchanged.
- The cursor overlay is not implemented (QEMU cursor events exist in QSM).
- Shared-memory CPU scanout only. No ReiMS Vulkan zero-copy, DMA-BUF GPU scanout integration, or video decoding in guest.
- Reconnect after a failed D-Bus session requires capture backend reconstruction. No auto-start or running Docker sidecar.
- QEMU's D-Bus display frame pacing and whether VNC can co-exist with this specific macOS renderer have **not yet been measured**.
- No production security review of IPC socket permissions, pairing, or network exposure.

**Coordination:** All source files reside in `/home/server/Documents/Projects/Sequoia-Reims-StreamBridge`; main ReiMS/Sequoia VM checkout and its Compose, image, QEMU process and guest input are deliberately untouched. The other agent owns live VM runtime.

### Exact upstream revisions used
- Sunshine: `7974a409bb82f2e52a45dad901acdd10fd1d61c1` (2026-10-27 upstream master snapshot, GPL-3.0-only)
- QSM Direct: `eb4b652cab71874c264204e8e4463496ed5d7786` (2026-10-27 snapshot; inspect GPL/AGPL source licenses before external distribution)
- All code lives on HPSERVER, not a synced/published GitHub PR. No build artifacts or credentials are exposed externally.
