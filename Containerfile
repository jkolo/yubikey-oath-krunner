# syntax=docker/dockerfile:1
# OCI-compliant Containerfile for KRunner YubiKey OATH Plugin
# Compatible with: podman, docker, buildah, kubernetes
# Base: Arch Linux (rolling release with latest Qt6 and KDE Frameworks 6)

# =============================================================================
# Stage 1: Builder - Build environment with all compile-time dependencies
# =============================================================================
FROM archlinux:latest AS builder

# Update system and install build dependencies
RUN pacman -Syu --noconfirm && \
    pacman -S --noconfirm \
    # Build tools
    base-devel \
    cmake \
    extra-cmake-modules \
    ninja \
    ccache \
    clang \
    lld \
    git \
    # Qt 6 development
    qt6-base \
    qt6-declarative \
    qt6-tools \
    qt6-svg \
    # KDE Frameworks 6 development
    krunner \
    ki18n \
    kconfig \
    kconfigwidgets \
    knotifications \
    kcoreaddons \
    kwallet \
    kcmutils \
    kwidgetsaddons \
    # PC/SC Lite
    pcsclite \
    ccid \
    # Other dependencies
    libxkbcommon \
    libportal-qt6 \
    kwayland \
    zxing-cpp \
    libxtst \
    # Icon generation (optional, can be disabled with CI_BUILD)
    imagemagick \
    optipng \
    # Static analysis
    clang-tools-extra \
    && pacman -Scc --noconfirm

# Setup ccache for faster rebuilds
ENV PATH="/usr/lib/ccache/bin:$PATH" \
    CCACHE_DIR=/workspace/.ccache \
    CCACHE_MAXSIZE=2G

# Create workspace
WORKDIR /workspace

# Copy source code (respects .containerignore)
COPY . /workspace/

# Build the project using clang-release preset
RUN cmake --preset clang-release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/opt/krunner-yubikey && \
    cmake --build build-clang-release -j$(nproc)

# =============================================================================
# Stage 2: Test - Runtime environment for running tests
# =============================================================================
FROM archlinux:latest AS test

# Update system and install runtime dependencies + test tools
RUN pacman -Syu --noconfirm && \
    pacman -S --noconfirm \
    # Qt 6 runtime
    qt6-base \
    qt6-declarative \
    qt6-svg \
    # KDE Frameworks 6 runtime
    krunner \
    ki18n \
    kconfig \
    kconfigwidgets \
    knotifications \
    kcoreaddons \
    kwallet \
    kcmutils \
    kwidgetsaddons \
    # PC/SC (libpcsclite only, no daemon - we use virtual devices)
    pcsclite \
    # Other runtime deps
    libxkbcommon \
    libportal-qt6 \
    kwayland \
    zxing-cpp \
    libxtst \
    # D-Bus for test isolation
    dbus \
    # X11 virtual framebuffer for input tests
    xorg-server-xvfb \
    xorg-xauth \
    # Test utilities
    procps-ng \
    cmake \
    # Coverage tools
    lcov \
    gcovr \
    # Git for version detection
    git \
    && pacman -Scc --noconfirm

# Setup environment
ENV QT_QPA_PLATFORM=offscreen \
    DISPLAY=:99

WORKDIR /workspace

# Copy build artifacts from builder stage
COPY --from=builder /workspace/build-clang-release /workspace/build-clang-release
COPY --from=builder /workspace/src /workspace/src
COPY --from=builder /workspace/tests /workspace/tests
COPY --from=builder /workspace/scripts /workspace/scripts
COPY --from=builder /workspace/CMakeLists.txt /workspace/
COPY --from=builder /workspace/CMakePresets.json /workspace/

# Install daemon binary for E2E tests that require it
COPY --from=builder /workspace/build-clang-release/bin/yubikey-oath-daemon /usr/bin/

# Setup D-Bus directories
RUN mkdir -p /run/dbus /var/run/dbus /tmp/.X11-unix && \
    chmod 1777 /tmp/.X11-unix

# Default command runs all tests
CMD ["ctest", "--test-dir", "build-clang-release", "--output-on-failure"]

# =============================================================================
# Stage 3: Artifacts - Minimal image with installed binaries
# =============================================================================
FROM archlinux:latest AS artifacts

# Install only runtime dependencies (minimal)
RUN pacman -Syu --noconfirm && \
    pacman -S --noconfirm \
    qt6-base \
    qt6-declarative \
    qt6-svg \
    krunner \
    ki18n \
    kconfig \
    kconfigwidgets \
    knotifications \
    kcoreaddons \
    kwallet \
    kcmutils \
    kwidgetsaddons \
    pcsclite \
    libxkbcommon \
    libportal-qt6 \
    kwayland \
    zxing-cpp \
    libxtst \
    dbus \
    && pacman -Scc --noconfirm

# Install binaries from builder stage
COPY --from=builder /workspace/build-clang-release/bin/yubikey-oath-daemon /usr/bin/
COPY --from=builder /workspace/build-clang-release/bin/krunner_yubikey.so /usr/lib/qt6/plugins/kf6/krunner/
COPY --from=builder /workspace/build-clang-release/bin/kcm_krunner_yubikey.so /usr/lib/qt6/plugins/
COPY --from=builder /workspace/src/daemon/dbus/systemd/app-pl.jkolo.yubikey.oath.daemon.service /usr/lib/systemd/user/

# Copy icons (if generated)
COPY --from=builder /workspace/icons/hicolor /usr/share/icons/hicolor/ || true

# Copy translation files
COPY --from=builder /workspace/build-clang-release/src/shared/po/*.qm /usr/share/locale/ || true

# Setup D-Bus
RUN mkdir -p /run/dbus /var/run/dbus

# Metadata
LABEL org.opencontainers.image.title="KRunner YubiKey OATH Plugin" \
      org.opencontainers.image.description="KDE Plasma 6 plugin for YubiKey/Nitrokey OATH TOTP/HOTP" \
      org.opencontainers.image.vendor="Jerzy Kołoszewski" \
      org.opencontainers.image.url="https://git.kolosowscy.pl/jkolo/krunner-yubikey-oath" \
      org.opencontainers.image.source="https://github.com/jkolo/yubikey-oath-krunner" \
      org.opencontainers.image.licenses="GPL-3.0-or-later" \
      org.opencontainers.image.base.name="archlinux:latest"

CMD ["/usr/bin/yubikey-oath-daemon"]
