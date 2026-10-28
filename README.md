# QEMU Display1 → Sunshine adapter

Experimental, isolated HPSERVER project. **Real QEMU video/PCM and GPU encode/decode are validated; paired Moonlight playback and live macOS integration are not.**

## Current evidence — 2026-10-28

The tests use a disposable 128 MiB SeaBIOS QEMU VM, not the production macOS disk.

- Full Sunshine fork builds with the QEMU capture backend, CUDA/NVENC, input routing, cursor compositing, and optional PCM forwarding.
- Real QEMU capture: 192 frames at 720×400; 440,640 individual floating-point audio samples, stereo 48 kHz; zero connection/audio errors.
- The fixture's PIT speaker produces a requested 440 Hz tone. The captured PCM's measured dominant frequency is **440.0 Hz**. This is actual audio data, not just a successful listener registration.
- Captured video was converted to NV12, encoded on HPSERVER's healthy RTX 2080 Ti using `h264_nvenc`, decoded, and compared: all 192 frames decoded; first-frame mean absolute RGB difference **0.209 levels on a 0–255 scale**. This is a local capture/codec round-trip, **not** a Moonlight network session.
- Twelve real QEMU connection/shutdown cycles passed; each shutdown took 43–51 ms in the recorded run. This exercises immediate stop and stop after frames arrive.
- Four adapter test executables pass (frame handling, input routing, bounded audio queue, cursor blending). Six upstream QSM receiver/core tests previously passed.
- VNC is genuinely still listening alongside D-Bus: the same fixture returns `RFB 003.008` on port 5909.
- Sunshine startup successfully detects H.264/HEVC NVENC and serves its GameStream discovery endpoint and HTTPS management interface. AV1/HDR are not supported by this implementation/hardware path.

Machine-readable evidence and logs are retained under `evidence/` and larger raw fixture captures under ignored `run/av-proof/`. Capture age is only receiver-side frame age, **not input-to-photon latency**. The 720×400 firmware workload is not a 1080p game benchmark. A separate earlier 1080p/60 H.264 NVENC throughput check passed; do not combine the two into an unmeasured 1080p streaming claim.

## Architecture

`QEMU CPU scanout → native GLib/GIO Display1 listener → latest-frame buffer → Sunshine capture interface → CUDA conversion/NVENC → existing Moonlight protocol`

The bridge reuses QSM Direct's immutable framebuffer storage and frame mailbox, but uses its own GIO transport to interoperate with QEMU's actual GLib authentication. QSM's fake-service tests alone did not establish interoperability with this QEMU build.

A small private QMP broker passes a **new** Display1 peer socket for each capture initialization. Reusing a consumed D-Bus socket breaks Sunshine's repeated encoder probes. The broker uses standard QMP `getfd`/`add_client` and lets GIO do normal authentication. It does not alter authentication policy.

The Sunshine sidecar must share QEMU's PID namespace (`pid: service:qemu` or `--pid container:...`) and use the same UID. Otherwise the QEMU peer may see PID 0 and reject authentication. Do not replace this with host-wide PID sharing or anonymous authentication.

Video remains CPU-backed. This does not change ReiMS shaders, guest Metal drivers, or implement GPU zero-copy. There is no intermediate Linux desktop/compositor. No host `/dev/uinput` access is required; supported input is routed directly into QEMU.

## Audio boundaries

`GioQemuAudio` receives and validates actual QEMU PCM, including signed/unsigned integer and floating formats. PCM reception and the generated tone are verified. Optional `PulseAudioBridge` forwards it to a **separately supplied private** PulseAudio/PipeWire sink for stock Sunshine capture.

**Audible Moonlight playback and A/V synchronization have not been verified.** The runtime fixture defaults to audio off. Do not enable it without providing a sink and verifying both directions. The synchronous PulseAudio writer still needs shutdown/failure testing against an unresponsive sound server. Microphone return and surround are not acceptance-tested.

## Known limitations / remaining gates

1. **Pairing and playback:** the headless Moonlight client reached pairing, but automated approval was blocked by execution controls. No authentication bypass was added. Approve the test client through Sunshine's normal management UI before asserting a working network stream.
2. **Live macOS:** untouched by this project; another coordinator owns its runtime. No Compose override or guest restart has been applied. Guest integration needs a controlled, reversible restart after renderer acceptance.
3. **Input:** keyboard, mouse buttons, vertical wheel and cursor composition are implemented. Held keys/buttons are released on capture detach. QEMU's absolute USB tablet has a bounded fallback for relative desktop movement; this is not validated FPS-game relative-mouse capture. Gamepads, horizontal scrolling and Unicode paste are not implemented.
4. **Reliability/performance:** resizing triggers capture reinitialization; reconnect tests pass, but sustained Moonlight reconnects, audio drift, long-duration high-motion streaming and client latency remain unmeasured.
5. **Deployment:** local prototype commits only, no public PR or package publication. The sidecar is not a stock upstream Sunshine image.

## Build and tests

Run from `/home/server/Documents/Projects/Sequoia-Reims-StreamBridge`:

```sh
python3 bridge/scripts/build_current.py --configure --target sunshine reims_gio_smoke reims_av_probe reims_gio_lifecycle
cmake -S bridge -B build-bridge -DCMAKE_BUILD_TYPE=Release
cmake --build build-bridge --parallel 5
ctest --test-dir build-bridge --output-on-failure
python3 bridge/scripts/package_runtime.py
```

The saved builder uses GCC 14, CUDA 12.8 and the pinned upstream dependency revisions. It builds in a separate container and does not install system packages on HPSERVER. Current web assets must already exist in `build-sunshine/assets/web`; build them from Sunshine's package lock with Node 24 and copy the result there when rebuilding from a fresh checkout. `Dockerfile.build` describes the builder; its GLAD generation also requires Jinja2 (`PYTHONPATH=/src/.python` in the retained build). These are development prerequisites, not a fully self-contained public release.

For the real AV proof, start `sh bridge/scripts/run-qemu-av-fixture.sh` in a separate terminal. Then:

```sh
python3 bridge/scripts/run_av_proof.py
python3 bridge/scripts/validate_av_artifacts.py
```

The proof script programs only the disposable fixture's PIT speaker through its private qtest socket. It never accesses the production guest. The validator uses NumPy and FFmpeg, and explicitly selects healthy GPU UUID `GPU-020906b4-0904-3471-e4ee-e41ec2350be3` through NVIDIA CDI. The legacy `--gpus` path can fail because of the already-broken eGPU; no GPU reset is needed.

## Isolated Compose fixture

```sh
python3 bridge/scripts/prepare_fixture.py
docker compose -f bridge/compose.fixture.yml config
# Start explicitly only after checking the host ports are free:
docker compose -f bridge/compose.fixture.yml up -d
```

This starts a new diskless QEMU fixture and Sunshine sidecar, **not macOS**. Ports bind to host loopback only. Sunshine's first-run setup and pairing remain normal interactive operations. Existing configuration is never overwritten by the preparation script. The file was staged and schema-checked, not automatically applied.

## Future live macOS attachment

Retain the exact working image, disks, guest kexts, CPU/RAM settings and VNC endpoint. Add a private QMP Unix socket and `-display dbus,p2p=yes` during a coordinated restart. Mount that socket directory into the Sunshine service and share only the VM's PID namespace. Match its UID. Enable a D-Bus audiodev and virtual USB sound card separately when testing audio. Keep a configuration rollback and do not combine this change with renderer A/B tests.

## Provenance and licensing

- Sunshine upstream snapshot: `7974a409bb82f2e52a45dad901acdd10fd1d61c1`.
- QSM Direct snapshot: `eb4b652cab71874c264204e8e4463496ed5d7786`.
- Both retained source trees provide GPLv3 license texts. Preserve their notices and source availability when distributing derivatives.
- Runtime images contain no generated pairing/admin credentials; mutable state is supplied through `/config`.
