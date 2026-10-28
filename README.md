# QEMU Display1 → Sunshine adapter

Experimental public source project; validated in an isolated QEMU fixture, not a production macOS VM. **Paired Moonlight network video, decoded test-tone audio and keyboard delivery to a disposable guest are validated. Live macOS and physical-display latency are not yet validated here.**

## Earlier local checks — 2026-10-28

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

**Decoded Moonlight network audio is verified; physical-speaker playback and A/V synchronization have not been measured.** The runtime fixture defaults to audio off. Do not enable it without providing a sink and verifying both directions. The synchronous PulseAudio writer still needs shutdown/failure testing against an unresponsive sound server. Microphone return and surround are not acceptance-tested.

## Known limitations / remaining gates

1. **Pairing and playback:** standard terminal-PIN pairing succeeded. Received H.264 and Opus packets were decoded and validated. Remaining: native-client display/speaker observation and measured A/V synchronization; no authentication bypass was added.
2. **Live macOS:** untouched by this project; another coordinator owns its runtime. No Compose override or guest restart has been applied. Guest integration needs a controlled, reversible restart after renderer acceptance.
3. **Input:** keyboard, mouse buttons, vertical wheel and cursor composition are implemented. Held keys/buttons are released on capture detach. QEMU's absolute USB tablet has a bounded fallback for relative desktop movement; this is not validated FPS-game relative-mouse capture. Gamepads, horizontal scrolling and Unicode paste are not implemented.
4. **Reliability/performance:** resizing triggers capture reinitialization; reconnect tests pass, but sustained Moonlight reconnects, audio drift, long-duration high-motion streaming and client latency remain unmeasured.
5. **Deployment:** public source available; no upstream PR or prebuilt package release. The sidecar is not a stock upstream Sunshine image.

## Build and tests

Run from the parent directory containing sibling `bridge/`, `Sunshine/`, and `qsm-rd/` checkouts:

```sh
python3 bridge/scripts/build_current.py --configure --target sunshine reims_gio_smoke reims_av_probe reims_gio_lifecycle reims_audio_forwarder
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

This starts a new diskless QEMU fixture and Sunshine sidecar, **not macOS**. Ports bind to host loopback only. Sunshine's first-run setup and pairing remain normal interactive operations. Existing configuration is never overwritten by the preparation script. The video-only fixture was started and paired during testing; the newer AV fixture below is the accepted full media topology.

## Future live macOS attachment

Retain the exact working image, disks, guest kexts, CPU/RAM settings and VNC endpoint. For the verified full-AV path, expose QEMU Display1 on a private Unix message bus during a coordinated restart; keep its private QMP socket for lifecycle operations. The p2p display variant is independently verified for video only. Mount that socket directory into the Sunshine service and share only the VM's PID namespace. Match its UID. Enable a D-Bus audiodev and virtual USB sound card separately when testing audio. Keep a configuration rollback and do not combine this change with renderer A/B tests.

## Provenance and licensing

- Sunshine upstream snapshot: `7974a409bb82f2e52a45dad901acdd10fd1d61c1`.
- QSM Direct snapshot: `eb4b652cab71874c264204e8e4463496ed5d7786`.
- Both retained source trees provide GPLv3 license texts. Preserve their notices and source availability when distributing derivatives.
- Runtime images contain no generated pairing/admin credentials; mutable state is supplied through `/config`.

## Paired network acceptance — 2026-10-28 17:36 ICT
The latest complete session delivered 628 decodable H.264 frames at 720×400 and 3,384 Opus packets (812,160 stereo PCM frames at 48 kHz). The intended 440 Hz fixture tone measured 441 Hz after the network audio path. `evidence/network/paired-av.json` contains media hashes and the exact image ID. This was a headless client, not an on-screen latency measurement.
A separate repeated Moonlight input test advanced the SeaBIOS keyboard-buffer tail from 0x26 to 0x28 and inserted ASCII `a` / scan code 0x1e. `scripts/verify_network_input.py` checks the guest memory rather than relying on transport logs; `evidence/network/keyboard-verified.json` stores the result.

## Tested full-AV topology
Use `compose.audio-fixture.yml`: a private Unix D-Bus message bus in QEMU, one persistent `reims_audio_forwarder`, an isolated PulseAudio null sink, and Sunshine with `REIMS_ENABLE_AUDIO=0` (video capture does not own audio). `QEMU_DBUS_DESTINATION=org.qemu`; the services share QEMU's PID namespace and UID. The original QMP/p2p route remains a video-only option, not the full-AV acceptance path.
Run `decode_moonlight_audio.py` then `save_network_evidence.py` against captures from the paired, instrumented Moonlight client. `verify_network_input.py` requires the named, diskless fixture and the prepared bounded input-test client. `ReimsEvidence.h` in that test-only client writes received packets for offline decoding; it does not alter Sunshine's protocol or pairing policy.

## Public source checkout

The [Sunshine fork](https://github.com/swittk/Sunshine/tree/feat/qemu-dbus-capture-prototype) and this [QEMU bridge](https://github.com/swittk/sunshine-qemu-bridge) are separate GPLv3 repositories. The bridge also uses [QSM Direct](https://github.com/rdmitry0911/qsm-rd).

Clone these **as siblings** in one directory (the build scripts expect this layout):

```bash
git clone --branch feat/qemu-dbus-capture-prototype https://github.com/swittk/Sunshine.git Sunshine
git -C Sunshine submodule update --init --recursive
git clone https://github.com/rdmitry0911/qsm-rd.git qsm-rd
git -C qsm-rd checkout eb4b652cab71874c264204e8e4463496ed5d7786
git clone https://github.com/swittk/sunshine-qemu-bridge.git bridge
docker build -f bridge/Dockerfile.build -t reims-sunshine-builder:local bridge
```

Build details, web-asset prerequisites, tests and the isolated Compose fixture are documented above. This is source code, not a published binary release. Never expose a guest's QMP socket or the Sunshine management UI without access controls.
