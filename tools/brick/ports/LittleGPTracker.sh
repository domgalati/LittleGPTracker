#!/bin/bash
# LittleGPTracker (domgalati fork, with PIGTAIL) for Knulli on the TrimUI Brick.
# Projects are created and listed in roms/ports/lgpt. The log goes to roms/ports/lgpt/log.txt.
progdir="$(dirname "$(readlink -f "$0")")/lgpt"
cd "$progdir"
export HOME="$progdir"
./lgpt-aarch64.elf > log.txt 2>&1
sync
