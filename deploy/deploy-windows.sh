#!/usr/bin/env bash
# Packs CalibStudio into a self-contained folder + zip that runs on a clean
# Windows 10/11 x64 machine (no MSYS2, Qt or OpenCV installed).
#
# Run from the MSYS2 UCRT64 shell:
#   ./deploy/deploy-windows.sh                    # newest Release build
#   ./deploy/deploy-windows.sh path/to/CalibStudio.exe
#   ./deploy/deploy-windows.sh --test             # also start the packed app with a clean PATH
#
# Result: <project>/dist/CalibStudio/ and <project>/dist/CalibStudio-win64.zip
#
# How it works:
#   1. windeployqt copies the Qt plugins the app needs (platform, styles...).
#   2. Every .exe/.dll in the package is scanned with objdump; each imported DLL
#      that lives in $MINGW_PREFIX/bin (Qt, OpenCV, GCC runtime and all their
#      dependencies) is copied, recursively, until nothing is missing.
#   3. A final check proves every import is satisfied by the package itself or
#      by Windows, i.e. nothing is taken from the developer's MSYS2 install.

set -euo pipefail

APP_NAME="CalibStudio"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"     # .../CalibStudio/CalibStudio
PROJECT_DIR="$(cd "$SOURCE_DIR/.." && pwd)"    # .../CalibStudio (build-* folders live here)
DIST_ROOT="$PROJECT_DIR/dist"
DIST_DIR="$DIST_ROOT/$APP_NAME"
ZIP_PATH="$DIST_ROOT/$APP_NAME-win64.zip"

RUN_TEST=0
MAKE_SHORTCUT=0
EXE_PATH=""
for arg in "$@"; do
    case "$arg" in
        --test) RUN_TEST=1 ;;
        --shortcut) MAKE_SHORTCUT=1 ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) EXE_PATH="$arg" ;;
    esac
done

info() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mwarning:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

# --- environment -------------------------------------------------------------
[[ "${MSYSTEM:-}" == "UCRT64" ]] || die "run this script from the 'MSYS2 UCRT64' shell (MSYSTEM=$MSYSTEM)"
PREFIX_BIN="$MINGW_PREFIX/bin"
for tool in objdump windeployqt-qt5 qmake-qt5 bsdtar; do
    command -v "$tool" >/dev/null || die "'$tool' not found. Install: pacman -S --needed mingw-w64-ucrt-x86_64-binutils mingw-w64-ucrt-x86_64-qt5-tools mingw-w64-ucrt-x86_64-libarchive"
done

# --- locate the executable ----------------------------------------------------
if [[ -z "$EXE_PATH" ]]; then
    # Newest Release build next to the sources (Qt Creator shadow-build folders).
    EXE_PATH="$(ls -t "$PROJECT_DIR"/build-*-Release/release/"$APP_NAME".exe 2>/dev/null | head -n 1 || true)"
    [[ -n "$EXE_PATH" ]] || die "no Release build found in $PROJECT_DIR/build-*-Release/release/. Build Release in Qt Creator first."
fi
[[ -f "$EXE_PATH" ]] || die "file not found: $EXE_PATH"
info "Packaging $EXE_PATH"

# Helpers ---------------------------------------------------------------------
imports_of() {   # prints the DLL names a PE file imports, one per line
    objdump -p "$1" 2>/dev/null | sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p' | tr -d '\r'
}
is_windows_dll() {   # provided by Windows itself (System32 or API sets)
    local name="${1,,}"
    [[ "$name" == api-ms-win-* || "$name" == ext-ms-* ]] && return 0
    [[ -f "/c/Windows/System32/$1" ]]
}

# The exe must be built with the MSYS2 toolchain we are about to bundle:
# every non-Windows import has to exist in $MINGW_PREFIX/bin.
for dll in $(imports_of "$EXE_PATH"); do
    if ! is_windows_dll "$dll" && [[ ! -f "$PREFIX_BIN/$dll" ]]; then
        die "$APP_NAME.exe imports $dll, which is not in $PREFIX_BIN. Rebuild with the MSYS2 UCRT64 kit."
    fi
done

# --- 1. fresh package folder + Qt plugins -------------------------------------
rm -rf "$DIST_DIR" "$ZIP_PATH"
mkdir -p "$DIST_DIR"
cp "$EXE_PATH" "$DIST_DIR/"

info "Copying Qt plugins (windeployqt)"
if ! windeployqt-qt5 --qmake "$(command -v qmake-qt5)" --release --verbose 0 \
        --no-translations --no-system-d3d-compiler --no-compiler-runtime \
        --no-angle --no-opengl-sw "$DIST_DIR/$APP_NAME.exe"; then
    warn "windeployqt failed; copying the essential plugins by hand"
    PLUGINS="$(qmake-qt5 -query QT_INSTALL_PLUGINS | tr -d '\r')"
    PLUGINS="$(cygpath -u "$PLUGINS")"
    mkdir -p "$DIST_DIR/platforms" "$DIST_DIR/styles"
    cp "$PLUGINS/platforms/qwindows.dll" "$DIST_DIR/platforms/"
    cp "$PLUGINS/styles/qwindowsvistastyle.dll" "$DIST_DIR/styles/" 2>/dev/null || true
fi

# Look for plugins next to the exe only, never in the build machine's Qt prefix.
printf '[Paths]\nPrefix = .\nPlugins = .\n' > "$DIST_DIR/qt.conf"

# --- 2. collect every DLL from the MSYS2 prefix, recursively ------------------
info "Collecting DLL dependencies from $PREFIX_BIN"
declare -A seen=()
copied=0
changed=1
while (( changed )); do
    changed=0
    while IFS= read -r -d '' binary; do
        for dll in $(imports_of "$binary"); do
            key="${dll,,}"
            [[ -n "${seen[$key]:-}" ]] && continue
            seen[$key]=1
            if [[ -f "$PREFIX_BIN/$dll" && ! -f "$DIST_DIR/$dll" ]]; then
                cp "$PREFIX_BIN/$dll" "$DIST_DIR/"
                copied=$((copied + 1))
                changed=1
            fi
        done
    done < <(find "$DIST_DIR" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0)
done
info "Copied $copied DLLs"

# --- 3. verify: nothing may come from outside the package or Windows ----------
info "Verifying the package is self-contained"
missing=0
while IFS= read -r -d '' binary; do
    for dll in $(imports_of "$binary"); do
        if ! is_windows_dll "$dll" && [[ ! -f "$DIST_DIR/$dll" ]]; then
            warn "${binary#$DIST_DIR/} needs $dll, which is not in the package"
            missing=$((missing + 1))
        fi
    done
done < <(find "$DIST_DIR" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0)
(( missing == 0 )) || die "$missing unresolved imports (see warnings above)"

# --- 4. zip -------------------------------------------------------------------
info "Creating $(basename "$ZIP_PATH")"
( cd "$DIST_ROOT" && bsdtar -a -cf "$(basename "$ZIP_PATH")" "$APP_NAME" )

files=$(find "$DIST_DIR" -type f | wc -l)
info "Done: $files files, folder $(du -sh "$DIST_DIR" | cut -f1), zip $(du -sh "$ZIP_PATH" | cut -f1)"
echo "    folder: $(cygpath -w "$DIST_DIR")"
echo "    zip:    $(cygpath -w "$ZIP_PATH")"

if (( MAKE_SHORTCUT )); then
    info "Creating a desktop shortcut"
    EXE_WIN="$(cygpath -w "$DIST_DIR/$APP_NAME.exe")"
    DIR_WIN="$(cygpath -w "$DIST_DIR")"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "\$desktop = [Environment]::GetFolderPath('Desktop'); \
         \$link = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path \$desktop '$APP_NAME.lnk')); \
         \$link.TargetPath = '$EXE_WIN'; \
         \$link.WorkingDirectory = '$DIR_WIN'; \
         \$link.IconLocation = '$EXE_WIN,0'; \
         \$link.Description = '$APP_NAME'; \
         \$link.Save()" \
        && echo "    shortcut: Desktop\\$APP_NAME.lnk" \
        || warn "could not create the desktop shortcut"
fi

# --- optional: start the packed app without MSYS2 on PATH ---------------------
if (( RUN_TEST )); then
    info "Starting the packaged app with PATH limited to Windows folders"
    info "If the window opens, the package works on a machine without MSYS2. Close it to finish."
    ( cd "$DIST_DIR" && PATH="/c/Windows/System32:/c/Windows" ./"$APP_NAME".exe )
fi
