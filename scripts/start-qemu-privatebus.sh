#!/bin/sh
set -eu
dbus-daemon --session --fork --address=unix:path=/fixture/displaybus.sock
exec qemu-system-x86_64 "$@"
