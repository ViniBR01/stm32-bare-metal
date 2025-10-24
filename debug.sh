#!/bin/sh
set -e

openocd -f board/st_nucleo_f4.cfg > build/openocd.log 2>&1 &
OCD_PID=$!

cleanup() {
    echo "Killing OpenOCD (PID $OCD_PID)..."
    kill $OCD_PID 2>/dev/null || pkill -f "openocd -f board/st_nucleo_f4.cfg"
    wait $OCD_PID 2>/dev/null || true
}
trap cleanup EXIT INT TERM

sleep 2

if [ -z "$1" ]; then
    echo "Usage: $0 <elf-file>"
    cleanup
    exit 1
fi

arm-none-eabi-gdb -q -ex "target remote localhost:3333" -ex "monitor reset init" "$1"