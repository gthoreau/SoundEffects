#!/usr/bin/env bash
#
# Build the standalone app and the VST3 / AU plug-ins as universal (arm64 +
# x86_64) Release binaries and package them for distribution.
#
#   ./native/packaging/package.sh              both formats (default)
#   ./native/packaging/package.sh --dmg        disk image only
#   ./native/packaging/package.sh --zip        zip archive only
#
# Output lands in native/dist/:
#   SoundEffects-<version>-macOS.dmg   drag-to-Applications image — the nice one,
#                                      for a download link or a file-sharing service
#   SoundEffects-<version>-macOS.zip   the same in an archive — for email, because
#                                      Gmail blocks .dmg outright (and blocks it
#                                      inside .zip too, which is why this archives
#                                      the bundles directly rather than the image)
#
# Both hold the app, and a "Plug-ins" folder with SoundEffects.vst3,
# SoundEffects.component and a note saying where each one goes.
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
VERSION="$(sed -n 's/^project(SoundEffects VERSION \([0-9.]*\).*/\1/p' "$NATIVE_DIR/CMakeLists.txt")"
[ -n "$VERSION" ] || { echo "could not read version from CMakeLists.txt" >&2; exit 1; }

APP_NAME="SoundEffects.app"
STEM="SoundEffects-$VERSION-macOS"
VOL_NAME="SoundEffects $VERSION"

echo "==> configuring universal Release build"
cmake -B "$BUILD_DIR" -S "$NATIVE_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0

echo "==> building $APP_NAME and the plug-ins"
cmake --build "$BUILD_DIR" --target SoundEffects_Standalone SoundEffects_VST3 SoundEffects_AU

ARTEFACTS="$BUILD_DIR/SoundEffects_artefacts/Release"
APP_SRC="$ARTEFACTS/Standalone/$APP_NAME"
VST3_SRC="$ARTEFACTS/VST3/SoundEffects.vst3"
AU_SRC="$ARTEFACTS/AU/SoundEffects.component"
for b in "$APP_SRC" "$VST3_SRC" "$AU_SRC"; do
    [ -d "$b" ] || { echo "build produced no $b" >&2; exit 1; }
done

echo "==> verifying they are universal"
for b in "$APP_SRC" "$VST3_SRC" "$AU_SRC"; do
    printf '    %-28s %s\n' "$(basename "$b")" "$(lipo -archs "$b/Contents/MacOS/SoundEffects")"
done

# Stage once and package from the staged copy, so both artefacts carry the same
# signed, attribute-free bundles.
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
PLUGINS="Plug-ins"
mkdir -p "$STAGE/$PLUGINS"
cp -R "$APP_SRC"  "$STAGE/$APP_NAME"
cp -R "$VST3_SRC" "$STAGE/$PLUGINS/SoundEffects.vst3"
cp -R "$AU_SRC"   "$STAGE/$PLUGINS/SoundEffects.component"
cat > "$STAGE/$PLUGINS/Read Me.txt" <<NOTE
SoundEffects $VERSION plug-ins (VST3 and Audio Unit), universal (Apple Silicon + Intel).

To install, in the Finder choose Go > Go to Folder... and copy:

  SoundEffects.vst3       into   ~/Library/Audio/Plug-Ins/VST3
  SoundEffects.component  into   ~/Library/Audio/Plug-Ins/Components

(Create the VST3 folder if it is not there.) Then restart your DAW, or rescan its
plug-ins. In Ableton Live, SoundEffects shows up under Plug-ins > SoundEffects.

Upgrading: replace the old copies. If an older SoundEffects.vst3 sits in
/Library/Audio/Plug-Ins/VST3 (the computer-wide folder), replace or remove it too,
so the DAW does not find two.

The plug-ins are signed ad-hoc, like the app. If your DAW or macOS refuses to load
them the first time, see "Installing" in the README:
https://github.com/gthoreau/SoundEffects#installing
NOTE

echo "==> signing ad-hoc"
for b in "$STAGE/$APP_NAME" "$STAGE/$PLUGINS/SoundEffects.vst3" "$STAGE/$PLUGINS/SoundEffects.component"; do
    codesign --force --deep --sign - --timestamp=none "$b"
    codesign --verify --deep --strict "$b"
done

# Anything the build tools touched can carry a quarantine flag; strip it so the
# copy we ship is clean. (Whoever receives it gets quarantined again on their
# own machine regardless — that is Gatekeeper's job, not a bug here.)
xattr -cr "$STAGE/$APP_NAME" "$STAGE/$PLUGINS"

mkdir -p "$DIST_DIR"

if $want_dmg; then
    echo "==> building $STEM.dmg"
    rm -f "$DIST_DIR/$STEM.dmg"
    # The image gets the app plus a link to /Applications, so the drag target is
    # right there in the window, and the plug-ins folder beside them. Built from
    # its own directory to keep the Applications link out of the .zip.
    IMG="$STAGE/img"
    mkdir -p "$IMG"
    cp -R "$STAGE/$APP_NAME" "$IMG/$APP_NAME"
    cp -R "$STAGE/$PLUGINS" "$IMG/$PLUGINS"
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
    # the same invocation Apple documents for notarisation upload. One folder,
    # named after the release, holding the app and the plug-ins.
    ZIPROOT="$STAGE/zip/SoundEffects $VERSION"
    mkdir -p "$ZIPROOT"
    cp -R "$STAGE/$APP_NAME" "$STAGE/$PLUGINS" "$ZIPROOT/"
    ditto -c -k --keepParent "$ZIPROOT" "$DIST_DIR/$STEM.zip"
fi

echo
for f in "$DIST_DIR/$STEM.dmg" "$DIST_DIR/$STEM.zip"; do
    [ -f "$f" ] || continue
    printf '    %-46s %6s  sha256 %s…\n' \
        "${f#"$NATIVE_DIR/"}" \
        "$(du -h "$f" | cut -f1)" \
        "$(shasum -a 256 "$f" | cut -c1-16)"
done
