# PSX3D build environment: the devkitPro image pinned by digest, plus the CIA tools and
# the packages the host tests need. Build it through tools/build/container.sh (which also
# works without Docker) or by hand:
#   docker build -t psx3d-build -f tools/build/Dockerfile tools/build
# The digest below must equal the `image` line of toolchain.lock (CI checks it).
FROM devkitpro/devkitarm@sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5

COPY toolchain.lock provision.sh /tmp/psx3d-build/
RUN bash /tmp/psx3d-build/provision.sh /tmp/psx3d-build/toolchain.lock && rm -rf /tmp/psx3d-build

ENV PSX3D_TOOLS=/opt/psx3d-tools \
    PATH=/opt/psx3d-tools/bin:/opt/devkitpro/tools/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
WORKDIR /work
