#!/usr/bin/env bash
#
# Build the standalone app as a universal (arm64 + x86_64) Release binary and
# package it for distribution.
#
#   ./native/packaging/package.sh              both formats (default)
#   ./native/packaging/package.sh --dmg        disk image only
#   ./native/packaging/package.sh --zip        zip archive only
#
# Output lands in native/dist/:
#   SoundEffect-<version>-macOS.dmg   drag-to-Applications image — the nice one,
#                                     for a download link or a file-sharing service
#   SoundEffect-<version>-macOS.zip   the .app in an archive — for email, because
#                                     Gmail blocks .dmg outright (and blocks it
#                                     inside .zip too, which is why this archives
#                                     the .app directly rather than the image)
#
# The bundle is signed ad-hoc ("-"), NOT with a Developer ID: there is no
# certificate on this machine, and an arm64 binary will not launch at all
# without at least an ad-hoc signature. Ad-hoc is enough to run; it is not
# enough to satisfy Gatekeeper on a file that arrives from outside, so first
# launch needs the Open Anyway dance documented in the root README.
#
set -euo pipefail

want_dmg=false
want_zip=false
for arg in "$@"; do
    case "$arg" in
        --dmg) want_dmg=true ;;
        --zip) want_zip=true ;;
        -h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $arg (try --help)" >&2; exit 2 ;;
    esac
done
# No format asked for means every format.
if ! $want_dmg && ! $want_zip; then want_dmg=true; want_zip=true; fi

NATIVE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$NATIVE_DIR/build-release"
DIST_DIR="$NATIVE_DIR/dist"

# The single source of truth for the version is CMakeLists.txt's project().
VERSION="$(sed -n 's/^project(SoundEffect VERSION \([0-9.]*\).*/\1/p' "$NATIVE_DIR/CMakeLists.txt")"
[ -n "$VERSION" ] || { echo "could not read version from CMakeLists.txt" >&2; exit 1; }

APP_NAME="SoundEffect.app"
STEM="SoundEffect-$VERSION-macOS"
VOL_NAME="SoundEffect $VERSION"

echo "==> configuring universal Release build"
cmake -B "$BUILD_DIR" -S "$NATIVE_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0

echo "==> building $APP_NAME"
cmake --build "$BUILD_DIR" --target SoundEffect_Standalone

APP_SRC="$BUILD_DIR/SoundEffect_artefacts/Release/Standalone/$APP_NAME"
[ -d "$APP_SRC" ] || { echo "build produced no $APP_SRC" >&2; exit 1; }

echo "==> verifying it is universal"
lipo -archs "$APP_SRC/Contents/MacOS/SoundEffect"

# Stage once and package from the staged copy, so both artefacts carry the same
# signed, attribute-free bundle.
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp -R "$APP_SRC" "$STAGE/$APP_NAME"

echo "==> signing ad-hoc"
codesign --force --deep --sign - --timestamp=none "$STAGE/$APP_NAME"
codesign --verify --deep --strict "$STAGE/$APP_NAME"

# Anything the build tools touched can carry a quarantine flag; strip it so the
# copy we ship is clean. (Whoever receives it gets quarantined again on their
# own machine regardless — that is Gatekeeper's job, not a bug here.)
xattr -cr "$STAGE/$APP_NAME"

mkdir -p "$DIST_DIR"

if $want_dmg; then
    echo "==> building $STEM.dmg"
    rm -f "$DIST_DIR/$STEM.dmg"
    # The image gets the app plus a link to /Applications, so the drag target is
    # right there in the window. Built from its own directory to keep the .zip
    # staging (which must contain the app ALONE) out of it.
    IMG="$STAGE/img"
    mkdir -p "$IMG"
    cp -R "$STAGE/$APP_NAME" "$IMG/$APP_NAME"
    ln -s /Applications "$IMG/Applications"
    hdiutil create \
        -volname "$VOL_NAME" \
        -srcfolder "$IMG" \
        -fs HFS+ \
        -format UDZO \
        -imagekey zlib-level=9 \
        -quiet \
        "$DIST_DIR/$STEM.dmg"
fi

if $want_zip; then
    echo "==> building $STEM.zip"
    rm -f "$DIST_DIR/$STEM.zip"
    # ditto, not zip(1): an .app is a bundle of symlinks and exec bits, and plain
    # zip flattens the symlinks — the unpacked app then fails to launch. This is
    # the same invocation Apple documents for notarisation upload.
    ditto -c -k --keepParent "$STAGE/$APP_NAME" "$DIST_DIR/$STEM.zip"
fi

echo
for f in "$DIST_DIR/$STEM.dmg" "$DIST_DIR/$STEM.zip"; do
    [ -f "$f" ] || continue
    printf '    %-46s %6s  sha256 %s…\n' \
        "${f#"$NATIVE_DIR/"}" \
        "$(du -h "$f" | cut -f1)" \
        "$(shasum -a 256 "$f" | cut -c1-16)"
done
