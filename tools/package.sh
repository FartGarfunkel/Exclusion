#!/usr/bin/env bash
# tools/package.sh — release packages of Exclusion from a CMake build (port of the HTML version's tools/package.sh):
#   dist/Exclusion-<ver>-x86_64.AppImage      Linux: make it executable and run it (the launcher starts)
#   dist/Exclusion-<ver>-linux-x64.tar.gz     Linux without AppImage/FUSE: unpack, run Exclusion/exclusion-launcher
#   dist/Exclusion-<ver>-windows-x64.zip      Windows: unzip anywhere, run Exclusion\exclusion-launcher.exe
# Each holds one Exclusion/ folder: exclusion (game), exclusion-launcher, exclusion-server, data.dqa (+ data/ for packs and
# sounds), assets/. The "-game" packages are the same without the launcher: what the launcher's updates download.
# The launcher's updater downloads the tar.gz / zip of newer releases (src/launcher/updater.h).
#
#   bash tools/package.sh <build dir> <version> [linux|windows]     (default: this platform)
# Needs: cmake, tar, curl (Linux: appimagetool is downloaded from its GitHub release into .cache/),
#        Windows (Git Bash): 7z or PowerShell for the zip.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
BUILD="${1:?build dir}"; VERSION="${2:?version, e.g. 1.0.0-alpha.1}"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) DEF=windows ;; *) DEF=linux ;; esac
WHAT="${3:-$DEF}"
OUT="$ROOT/dist"; CACHE="$ROOT/.cache"
mkdir -p "$OUT" "$CACHE"
CONFIG="${CONFIG:-Release}"

stage() { # dest folder: the install tree (programs + data + assets)
  rm -rf "$1"; mkdir -p "$1"
  cmake --install "$BUILD" --config "$CONFIG" --prefix "$1" > /dev/null
  # the data archive: data/ in one file, data.dqa (a zip the game reads; common/fs.cpp), instead of ~9500 loose files.
  # resourcepacks/ and sounds/ stay folders (players drop things in them); a loose file in data/ always wins.
  ( cd "$1/data" && set -- $(ls -A | grep -v -x -e resourcepacks -e sounds) && cmake -E tar cf ../data.dqa --format=zip "$@" && rm -rf "$@" )
  echo "  data.dqa: $(du -h "$1/data.dqa" | cut -f1)"
}

if [ "$WHAT" = linux ]; then
  echo "Packaging Exclusion $VERSION for Linux"
  STAGE="$OUT/stage-linux"; rm -rf "$STAGE"; mkdir -p "$STAGE"
  APP="$STAGE/Exclusion"
  stage "$APP"
  strip --strip-unneeded "$APP/exclusion" "$APP/exclusion-launcher" "$APP/exclusion-server" 2>/dev/null || true
  cat > "$APP/README.txt" <<EOS
Exclusion $VERSION for Linux

Run ./exclusion-launcher (the launcher: news, Play, options, the dedicated server and the Mod Studio).
./exclusion-server runs a server without a window: ./exclusion-server --world "My Server" --port 25580
Your worlds and settings are kept in ~/.local/share/Exclusion, so newer versions find them.
The launcher keeps itself up to date from GitHub Releases (Options > Updates).
EOS
  tar -C "$STAGE" -czf "$OUT/Exclusion-$VERSION-linux-x64.tar.gz" Exclusion
  # the game alone (no launcher): what the launcher's updates download (launcher/updater.cpp)
  tar -C "$STAGE" -czf "$OUT/Exclusion-$VERSION-linux-x64-game.tar.gz" --exclude=Exclusion/exclusion-launcher Exclusion

  # ---- AppImage: AppRun starts the launcher. Bundles the non-system libraries the programs need, but never
  # libGL/libEGL/libvulkan or the other driver/desktop libraries (those must come from the user's system).
  APPDIR="$STAGE/Exclusion.AppDir"
  mkdir -p "$APPDIR/usr/lib" "$APPDIR/usr/share/icons/hicolor/256x256/apps" "$APPDIR/usr/share/applications"
  cp -a "$APP" "$APPDIR/usr/lib/exclusion"
  LIBS="$APPDIR/usr/lib/exclusion/lib"; mkdir -p "$LIBS"
  SYSTEM='^(linux-vdso|ld-linux|libc|libm|libdl|libpthread|librt|libresolv|libutil|libstdc\+\+|libgcc_s|libGL|libGLX|libGLdispatch|libOpenGL|libEGL|libGLES|libvulkan|libdrm|libgbm|libX|libxcb|libxkbcommon|libwayland|libdecor|libasound|libpulse|libjack|libpipewire|libudev|libdbus|libsystemd|libz)[.-]'
  for bin in exclusion exclusion-launcher exclusion-server; do
    ldd "$APPDIR/usr/lib/exclusion/$bin" | awk '/=> \// {print $1" "$3}' | while read -r name path; do
      if ! [[ "$name" =~ $SYSTEM ]] && [ ! -e "$LIBS/$name" ]; then echo "  bundling $name"; cp -L "$path" "$LIBS/"; fi
    done
  done
  cat > "$APPDIR/AppRun" <<'EOS'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
D="$HERE/usr/lib/exclusion"
if [ -n "$(ls -A "$D/lib" 2>/dev/null)" ]; then export LD_LIBRARY_PATH="$D/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"; fi
case "$1" in
  --server) shift; exec "$D/exclusion-server" "$@" ;;   # ./Exclusion.AppImage --server --world NAME
  --game) shift; exec "$D/exclusion" "$@" ;;
esac
exec "$D/exclusion-launcher" "$@"
EOS
  chmod +x "$APPDIR/AppRun"
  cat > "$APPDIR/exclusion.desktop" <<EOS
[Desktop Entry]
Type=Application
Name=Exclusion
Comment=Voxel sandbox: dig, build, survive and explore
Exec=exclusion %U
Icon=exclusion
Categories=Game;
Terminal=false
StartupWMClass=Exclusion
X-AppImage-Version=$VERSION
EOS
  cp "$APPDIR/exclusion.desktop" "$APPDIR/usr/share/applications/"
  cp assets/icon.png "$APPDIR/exclusion.png"
  cp assets/icon.png "$APPDIR/usr/share/icons/hicolor/256x256/apps/exclusion.png"
  ln -sf exclusion.png "$APPDIR/.DirIcon"
  fetch() { # url file
    if [ ! -s "$CACHE/$2" ]; then echo "  downloading $2"; curl -fSL --retry 3 -o "$CACHE/$2.part" "$1"; mv "$CACHE/$2.part" "$CACHE/$2"; fi
  }
  fetch https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage appimagetool-x86_64.AppImage
  fetch https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64 runtime-x86_64
  chmod +x "$CACHE/appimagetool-x86_64.AppImage"
  rm -f "$OUT/Exclusion-$VERSION-x86_64.AppImage"
  ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$CACHE/appimagetool-x86_64.AppImage" --no-appstream --runtime-file "$CACHE/runtime-x86_64" \
    "$APPDIR" "$OUT/Exclusion-$VERSION-x86_64.AppImage"
  chmod +x "$OUT/Exclusion-$VERSION-x86_64.AppImage"
  rm -rf "$STAGE"
fi

if [ "$WHAT" = windows ]; then
  echo "Packaging Exclusion $VERSION for Windows"
  STAGE="$OUT/stage-win"; rm -rf "$STAGE"; mkdir -p "$STAGE"
  APP="$STAGE/Exclusion"
  stage "$APP"
  printf 'Exclusion %s for Windows\r\n\r\nRun exclusion-launcher.exe. Keep it in this folder with its other files.\r\nTo put it on your Start menu or desktop: right-click exclusion-launcher.exe > Show more options > Send to > Desktop (create shortcut).\r\nIf Windows SmartScreen warns about an unknown app: More info > Run anyway.\r\nexclusion-server.exe runs a server without a window.\r\nUpdates: the launcher installs new versions by itself (Options > Updates).\r\nYour worlds are kept in %%APPDATA%%\\Exclusion, so a newer version finds them.\r\n' "$VERSION" > "$APP/README.txt"
  ZIP="$OUT/Exclusion-$VERSION-windows-x64.zip"; rm -f "$ZIP"
  if command -v 7z > /dev/null; then (cd "$STAGE" && 7z a -tzip -mx=9 "$ZIP" Exclusion > /dev/null)
  else powershell -NoProfile -Command "Compress-Archive -Path '$(cygpath -w "$APP")' -DestinationPath '$(cygpath -w "$ZIP")'"; fi
  # the game alone (no launcher): what the launcher's updates download (launcher/updater.cpp)
  GAMEZIP="$OUT/Exclusion-$VERSION-windows-x64-game.zip"; rm -f "$GAMEZIP"
  mv "$APP/exclusion-launcher.exe" "$STAGE/launcher.exe.keep"
  if command -v 7z > /dev/null; then (cd "$STAGE" && 7z a -tzip -mx=9 "$GAMEZIP" Exclusion > /dev/null)
  else powershell -NoProfile -Command "Compress-Archive -Path '$(cygpath -w "$APP")' -DestinationPath '$(cygpath -w "$GAMEZIP")'"; fi
  mv "$STAGE/launcher.exe.keep" "$APP/exclusion-launcher.exe"
  rm -rf "$STAGE"
fi

ls -l "$OUT"
