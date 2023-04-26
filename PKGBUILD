#Maintainer: Jerzy Kołosowski jerzy@kolosowscy.pl

pkgname=krunner-yoath
pkgver=0.0.1
pkgrel=1
pkgdesc="KRunner extension for Yoath"
arch=('x86_64')
url="https://git.kolosowscy.pl/jurek/krunner-yoath.git"
license=('LGPL')
depends=('dotnet-runtime')
makedepends=('git' 'dotnet-sdk')
source=("git+${url}")
sha256sums=('SKIP')

prepare() {
	cd "$srcdir/krunner-yoath"
	git submodule update --init --recursive
}

build() {
	cd "$srcdir/krunner-yoath"
	dotnet publish -c Release
}

package() {
	cd "$srcdir/krunner-yoath"

	#Install the binary
	install -Dm755 "bin/Release/net7.0/linux-x64/publish/krunner-yoath" "$pkgdir/usr/bin/krunner-yoath"

	#Install the systemd user service
	install -Dm644 "resources/krunner-yoath.service" "$pkgdir/usr/lib/systemd/user/krunner-yoath.service"

	#Install the icon
	install -Dm644 "resources/krunner-yoath.png" "$pkgdir/usr/share/pixmaps/krunner-yoath.png"
}
#vim:set ts=2 sw=2 et:
