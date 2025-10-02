# Maintainer: YubiKey Runner Developer <dev@example.com>

pkgname=krunner-yubikey
pkgver=1.0.0
pkgrel=1
pkgdesc="KRunner plugin for YubiKey OATH TOTP/HOTP codes"
arch=('x86_64')
url="https://github.com/yourusername/krunner-yoath.cpp"
license=('GPL-3.0-or-later')
depends=(
    'qt6-base'
    'qt6-qml'
    'qt6-quick'
    'krunner'
    'ki18n'
    'knotifications'
    'kconfig'
    'kcoreaddons'
    'pcsclite'
)
makedepends=(
    'cmake'
    'extra-cmake-modules'
    'git'
)
optdepends=(
    'xdotool: For typing codes on X11'
    'wtype: For typing codes on Wayland'
    'ydotool: Alternative for typing codes on Wayland'
)
provides=('krunner-yubikey')
conflicts=('krunner-yubikey')
source=("git+$url.git#tag=v$pkgver")
sha256sums=('SKIP')

build() {
    cd "$srcdir/$pkgname"

    cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DKDE_INSTALL_USE_QT_SYS_PATHS=ON \
        -Wno-dev

    cmake --build build
}

check() {
    cd "$srcdir/$pkgname"

    # Basic build verification
    test -f "build/src/libkrunner_yubikey.so" || {
        echo "Plugin library not found!"
        return 1
    }

    # Check required files
    test -f "src/metadata.json" || {
        echo "Metadata file not found!"
        return 1
    }

    test -f "config/yubikeyrunner.kcfg" || {
        echo "Configuration schema not found!"
        return 1
    }
}

package() {
    cd "$srcdir/$pkgname"

    DESTDIR="$pkgdir" cmake --install build

    # Install documentation
    install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"

    # Install license
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE" 2>/dev/null || true
}

# Post-install message
post_install() {
    echo "YubiKey OATH plugin for KRunner has been installed."
    echo ""
    echo "To use the plugin:"
    echo "1. Ensure your YubiKey has OATH application configured"
    echo "2. Make sure pcscd service is running: systemctl enable --now pcscd"
    echo "3. Restart KRunner: kquitapp6 krunner"
    echo "4. Test by typing an OATH account name in KRunner (Alt+Space)"
    echo ""
    echo "Configuration is available in System Settings → Search → KRunner → YubiKey OATH"
    echo ""
    echo "For X11 typing support, install xdotool:"
    echo "  pacman -S xdotool"
    echo ""
    echo "For Wayland typing support, install wtype or ydotool:"
    echo "  pacman -S wtype"
    echo "  # or"
    echo "  pacman -S ydotool"
}

post_upgrade() {
    post_install
}

post_remove() {
    echo "YubiKey OATH plugin has been removed."
    echo "You may want to restart KRunner: kquitapp6 krunner"
}