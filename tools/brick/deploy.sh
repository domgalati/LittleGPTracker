#!/bin/bash
# Build for the TrimUI Brick (Knulli) and push the binary over SSH.
#   tools/brick/deploy.sh            build, then push
#   tools/brick/deploy.sh --no-build push the last build
#   tools/brick/deploy.sh --log      pull lgpt/log.txt back to tools/brick/log.txt and stop
# BRICK_HOST (default knulli.local) and BRICK_USER (default root) can be set in the environment.
# Logs in with a password through sshpass (sudo apt install sshpass): BRICK_PASS, default Knulli's "linux".
# Key login does not work here: Knulli's home is on exFAT and sshd rejects the key file.
set -euo pipefail
cd "$(dirname "$0")/../.."
HOST="${BRICK_HOST:-knulli.local}"
USER_="${BRICK_USER:-root}"
DEST=/userdata/roms/ports/lgpt
export SSHPASS="${BRICK_PASS:-linux}"
OPTS=(-o ConnectTimeout=5 -o PubkeyAuthentication=no -o PreferredAuthentications=password -o StrictHostKeyChecking=accept-new)
SSH=(sshpass -e ssh "${OPTS[@]}" "$USER_@$HOST")
build=1; log=0
for a in "$@"; do case "$a" in
  --no-build) build=0;; --log) log=1;;
  *) echo "unknown option $a"; exit 2;; esac; done

if [ $log = 1 ]; then
  sshpass -e scp "${OPTS[@]}" "$USER_@$HOST:$DEST/log.txt" tools/brick/log.txt && echo "tools/brick/log.txt"; exit
fi

if [ $build = 1 ]; then
  docker image inspect lgpt-aarch64 >/dev/null 2>&1 || docker build -t lgpt-aarch64 tools/brick
  docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src/projects -e PWD=/src/projects lgpt-aarch64 \
    make PLATFORM=RG35XXPLUS DEVKIT=/usr/ CROSS_COMPILE=aarch64-linux-gnu- \
    SDL_CFLAGS="-I/usr/include/SDL2 -I/usr/include/aarch64-linux-gnu -D_REENTRANT" -j"$(nproc)"
fi

ELF=projects/lgpt-rg35xxplus.elf
[ -f "$ELF" ] || { echo "no $ELF, build first"; exit 1; }
"${SSH[@]}" true || { echo "can't reach $USER_@$HOST (Wi-Fi on? set BRICK_HOST to its IP)"; exit 1; }

# upload under a temporary name, then rename: replacing a running binary in place fails with "text file busy"
sshpass -e scp "${OPTS[@]}" "$ELF" "$USER_@$HOST:$DEST/lgpt-aarch64.elf.new"
"${SSH[@]}" "chmod +x $DEST/lgpt-aarch64.elf.new && mv -f $DEST/lgpt-aarch64.elf.new $DEST/lgpt-aarch64.elf && sync"
echo "pushed $(sha256sum "$ELF" | cut -c1-12) to $HOST:$DEST. Quit LGPT on the Brick and relaunch it."
