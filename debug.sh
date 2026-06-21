#!/bin/sh
set -e

# Usage: debug.sh <elf-file> [stlink-serial]
#
# The optional second argument pins OpenOCD to a specific ST-LINK probe
# (adapter serial), so `make debug` can target the chosen board even when
# multiple ST-LINKs are connected.  When omitted, OpenOCD auto-selects a probe
# (the original behavior).
ELF="$1"
STLINK_SERIAL="$2"

if [ -z "$ELF" ]; then
    echo "Usage: $0 <elf-file> [stlink-serial]"
    exit 1
fi

OCD_ARGS="-f board/st_nucleo_f4.cfg"
if [ -n "$STLINK_SERIAL" ]; then
    OCD_ARGS="-c \"adapter serial $STLINK_SERIAL\" $OCD_ARGS"
fi

eval "openocd $OCD_ARGS" > build/openocd.log 2>&1 &
OCD_PID=$!

cleanup() {
    echo "Killing OpenOCD (PID $OCD_PID)..."
    kill $OCD_PID 2>/dev/null || pkill -f "openocd -f board/st_nucleo_f4.cfg"
    wait $OCD_PID 2>/dev/null || true
}
trap cleanup EXIT INT TERM

sleep 2

arm-none-eabi-gdb -q -ex "target remote localhost:3333" -ex "monitor reset init" "$ELF"