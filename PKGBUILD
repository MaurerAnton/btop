# Maintainer: Anton Maurer <anton@maureranton.com>
# Contributor: btop++ Panfrost Edition

pkgname=btop-panfrost
pkgver=1.4.7
pkgrel=1
pkgdesc="Resource monitor with Panfrost GPU support (for PineTab2 / Mali)"
arch=('aarch64' 'x86_64')
url="https://github.com/MaurerAnton/btop"
license=('Apache-2.0')
depends=('gcc-libs')
makedepends=('cmake' 'make')
source=("${pkgname}::git+https://github.com/MaurerAnton/btop.git#branch=main")
sha256sums=('SKIP')

build() {
  cd "${srcdir}/${pkgname}"
  make GPU_SUPPORT=true STATIC=false
}

package() {
  cd "${srcdir}/${pkgname}"
  install -Dm755 bin/btop "${pkgdir}/usr/bin/btop"
  install -Dm644 btop.desktop "${pkgdir}/usr/share/applications/btop.desktop"
  install -Dm644 Img/icon.png "${pkgdir}/usr/share/icons/hicolor/48x48/apps/btop.png"
  install -Dm644 Img/icon.svg "${pkgdir}/usr/share/icons/hicolor/scalable/apps/btop.svg"

  for theme in themes/*.theme; do
    install -Dm644 "$theme" "${pkgdir}/usr/share/btop/themes/$(basename $theme)"
  done
}
