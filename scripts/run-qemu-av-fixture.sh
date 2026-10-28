#!/bin/sh
# Disposable QEMU fixture: VGA firmware output and PIT speaker -> D-Bus audio.
# No macOS disk, network or GPU passthrough. A qtest socket drives only this VM.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
STATE="$ROOT/bridge/run/av-fixture"
mkdir -p "$STATE"
printf 'server:x:1000:1000:Fixture:/tmp:/usr/sbin/nologin\n' > "$STATE/passwd"
cp "$ROOT/bridge/scripts/start-qemu-privatebus.sh" "$STATE/start-privatebus.sh"
exec docker run --rm --name reims-av-fixture --network none --user 1000:1000 \
  --mount "type=bind,src=$STATE,dst=/fixture" \
  --mount "type=bind,src=$STATE/passwd,dst=/etc/passwd,readonly" \
  --entrypoint sh sequoia-reims-metal-profile:local /fixture/start-privatebus.sh \
  -machine q35,accel=tcg,pcspk-audiodev=fixture_audio -m 128M -smp 1 \
  -L /usr/share/seabios -vga std \
  -audiodev dbus,id=fixture_audio,out.frequency=48000,out.channels=2,out.format=s16 \
  -device nec-usb-xhci,id=xhci \
  -device usb-audio,bus=xhci.0,audiodev=fixture_audio \
  -display vnc=:9 -display dbus,addr=unix:path=/fixture/displaybus.sock,audiodev=fixture_audio \
  -qmp unix:/fixture/qmp.sock,server=on,wait=off \
  -qtest unix:/fixture/qtest.sock,server=on,wait=off
