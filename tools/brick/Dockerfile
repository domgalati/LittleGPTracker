# Cross-compiler for the TrimUI Brick (Knulli, aarch64, SDL2). Used by deploy.sh.
FROM debian:bookworm
RUN dpkg --add-architecture arm64 && apt-get update && apt-get install -y --no-install-recommends \
    make g++-aarch64-linux-gnu libsdl2-dev:arm64 python3 python3-pil && rm -rf /var/lib/apt/lists/*
