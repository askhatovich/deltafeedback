#!/usr/bin/env bash
# Builds a .deb for deltafeedback. Expects:
#   - the deltafeedback binary already built at ./build/deltafeedback
#   - libdeltachat.so available via $DC_LIB (defaults to
#     ../deltachatbot-cpp/deltachat-core-rust/target/release/libdeltachat.so)
#
# Output: dist/deltafeedback_<version>_<arch>.deb
#
# Override version via $VERSION (default = 0.1.0+<short git sha>).
# Override Debian codename in package metadata via $DEBIAN_CODENAME (e.g. bookworm, trixie).

set -euo pipefail

cd "$(dirname "$0")/.."

# Version comes from `git describe --tags`:
#   - on a tagged commit:        `v1.0.0`        → `1.0.0`
#   - on a commit past a tag:    `v1.0.0-3-g..`  → `1.0.0-3-g..`
#   - no tags / no git available: `0+<sha>`      → `0+<sha>`
# Override via $VERSION for one-off builds.
if [ -z "${VERSION:-}" ]; then
    if VERSION=$(git describe --tags --always --dirty 2>/dev/null); then
        VERSION="${VERSION#v}"
    else
        VERSION="0+$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    fi
fi
ARCH="${ARCH:-$(dpkg --print-architecture)}"
DEBIAN_CODENAME="${DEBIAN_CODENAME:-$(. /etc/os-release && echo "${VERSION_CODENAME:-unknown}")}"
BIN="${BIN:-./build/deltafeedback}"
DC_LIB="${DC_LIB:-../deltachatbot-cpp/deltachat-core-rust/target/release/libdeltachat.so}"

[ -x "$BIN" ]      || { echo "FATAL: $BIN not found — run cmake --build build first" >&2; exit 1; }
[ -e "$DC_LIB" ]   || { echo "FATAL: libdeltachat.so not found at $DC_LIB" >&2; exit 1; }

PKGNAME="deltafeedback"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

# ----- file layout -----
install -d "$STAGE/DEBIAN"
install -d "$STAGE/usr/bin"
install -d "$STAGE/usr/lib/$PKGNAME"
install -d "$STAGE/usr/share/$PKGNAME/web"
install -d "$STAGE/usr/share/$PKGNAME/web/locales"
install -d "$STAGE/etc/$PKGNAME"
install -d "$STAGE/lib/systemd/system"

# Binary + bundled libdeltachat (rpath in CMakeLists points to the bundled location).
install -m 0755 "$BIN" "$STAGE/usr/bin/$PKGNAME"
install -m 0644 "$DC_LIB" "$STAGE/usr/lib/$PKGNAME/libdeltachat.so"
patchelf --set-rpath '$ORIGIN/../lib/'"$PKGNAME" "$STAGE/usr/bin/$PKGNAME" 2>/dev/null || true

# Web assets — replaced on every upgrade (NOT conffiles).
cp -r web/. "$STAGE/usr/share/$PKGNAME/web/"

# Config example — postinst copies to config.ini ONLY if it doesn't exist.
install -m 0644 config.example.ini "$STAGE/etc/$PKGNAME/config.example.ini"

install -m 0644 packaging/deltafeedback.service "$STAGE/lib/systemd/system/$PKGNAME.service"
install -m 0755 packaging/postinst              "$STAGE/DEBIAN/postinst"
install -m 0755 packaging/prerm                 "$STAGE/DEBIAN/prerm"
install -m 0755 packaging/postrm                "$STAGE/DEBIAN/postrm"

# ----- control file -----
INSTALLED_KB=$(du -ks --exclude=DEBIAN "$STAGE" | cut -f1)
sed -e "s/@VERSION@/${VERSION}/g" \
    -e "s/@ARCH@/${ARCH}/g" \
    -e "s/@INSTALLED_SIZE@/${INSTALLED_KB}/g" \
    packaging/control.in > "$STAGE/DEBIAN/control"

# ----- build -----
mkdir -p dist
DEB="dist/${PKGNAME}_${VERSION}_${DEBIAN_CODENAME}_${ARCH}.deb"
dpkg-deb --root-owner-group --build "$STAGE" "$DEB"
echo "built $DEB"
