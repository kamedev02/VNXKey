#!/usr/bin/env bash
# ==============================================================================
# build-release.sh - Build và đóng gói VNXKey thành .deb
# ==============================================================================
#
# Sử dụng:
#   ./scripts/build-release.sh              # Build bình thường
#   ./scripts/build-release.sh --skip-gui   # Bỏ qua Flutter build
#   ./scripts/build-release.sh --daemon-only # Chỉ build C++ daemon
#
# Output:
#   dist/vnxkey_<version>_amd64.deb
#
# Yêu cầu:
#   - libevdev-dev đã cài (chạy install-deps.sh trước)
#   - Flutter SDK trong PATH hoặc $FLUTTER_SDK
#   - fakeroot, dpkg-deb

set -euo pipefail

# ── Màu sắc cho output ──
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m' # No Color

log_info()    { echo -e "${BLUE}▶${NC} $*"; }
log_success() { echo -e "${GREEN}✓${NC} $*"; }
log_warn()    { echo -e "${YELLOW}⚠${NC} $*"; }
log_error()   { echo -e "${RED}✗${NC} $*" >&2; }
log_section() { echo -e "\n${BOLD}━━━ $* ━━━${NC}"; }

# ── Cấu hình ──
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
# Calculate auto-increment version: YY.MM.XXX
VERSION_FILE="$PROJECT_ROOT/VERSION"
CURRENT_YY=$(date +"%y")
CURRENT_MM=$(date +"%m")

if [ -f "$VERSION_FILE" ]; then
    LAST_VERSION=$(cat "$VERSION_FILE")
    # Parse YY.MM.XXX
    LAST_YY=$(echo "$LAST_VERSION" | cut -d'.' -f1)
    LAST_MM=$(echo "$LAST_VERSION" | cut -d'.' -f2)
    LAST_BUILD=$(echo "$LAST_VERSION" | cut -d'.' -f3)

    if [ "$CURRENT_YY" = "$LAST_YY" ] && [ "$CURRENT_MM" = "$LAST_MM" ]; then
        # Increment build number (using 10# to avoid octal interpretation)
        BUILD_NUM=$((10#$LAST_BUILD + 1))
    else
        # Reset build number for new month/year
        BUILD_NUM=1
    fi
else
    BUILD_NUM=1
fi

# Format BUILD_NUM to 3 digits
BUILD_STR=$(printf "%03d" $BUILD_NUM)
VERSION="${CURRENT_YY}.${CURRENT_MM}.${BUILD_STR}"

# Save new version
echo "$VERSION" > "$VERSION_FILE"

ARCH="amd64"
PACKAGE_NAME="vnxkey"
DEB_FILENAME="${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"

BE_DIR="$PROJECT_ROOT/daemon"
FE_DIR="$PROJECT_ROOT/gui"
PACKAGING_DIR="$PROJECT_ROOT/packaging"
DIST_DIR="$PROJECT_ROOT/dist"
STAGING_DIR="$DIST_DIR/staging"

# Sync version to Flutter and CMake
sed -i -E "s/^version: .*/version: $VERSION/" "$FE_DIR/pubspec.yaml"
sed -i -E "s/project\(vnxkey_daemon VERSION .* LANGUAGES CXX\)/project(vnxkey_daemon VERSION $VERSION LANGUAGES CXX)/" "$BE_DIR/CMakeLists.txt"

# Parse arguments
SKIP_GUI=false
DAEMON_ONLY=false
for arg in "$@"; do
    case $arg in
        --skip-gui)    SKIP_GUI=true ;;
        --daemon-only) DAEMON_ONLY=true; SKIP_GUI=true ;;
        --help|-h)
            echo "Usage: $0 [--skip-gui] [--daemon-only]"
            exit 0
            ;;
    esac
done

# ── Tìm Flutter SDK ──
find_flutter() {
    if command -v flutter &>/dev/null; then
        echo "flutter"
        return
    fi
    for candidate in \
        "$HOME/dev/flutter/bin/flutter" \
        "$HOME/flutter/bin/flutter" \
        "/opt/flutter/bin/flutter" \
        "/usr/lib/flutter/bin/flutter"
    do
        if [ -x "$candidate" ]; then
            echo "$candidate"
            return
        fi
    done
    echo ""
}

FLUTTER_CMD=$(find_flutter)

# ── Banner ──
echo ""
echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BOLD}  VNXKey Build & Package Script v${VERSION}${NC}"
echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
log_info "Project root : $PROJECT_ROOT"
log_info "Output       : $DIST_DIR/$DEB_FILENAME"
log_info "Flutter      : ${FLUTTER_CMD:-NOT FOUND}"
echo ""

# ── Kiểm tra dependencies ──
log_section "Kiểm tra môi trường"

check_cmd() {
    if command -v "$1" &>/dev/null; then
        log_success "$1 found"
    else
        log_error "$1 NOT found. Install: $2"
        exit 1
    fi
}

check_cmd cmake     "sudo apt install cmake"
check_cmd g++       "sudo apt install build-essential"
check_cmd fakeroot  "sudo apt install fakeroot"
check_cmd dpkg-deb  "sudo apt install dpkg-dev"

# Kiểm tra libevdev-dev
if ! pkg-config --exists libevdev 2>/dev/null; then
    log_warn "libevdev-dev not found! Daemon sẽ build WITHOUT keyboard support."
    log_warn "Cài: sudo apt install libevdev-dev"
    HAS_LIBEVDEV=false
else
    log_success "libevdev $(pkg-config --modversion libevdev) found"
    HAS_LIBEVDEV=true
fi

if [ "$SKIP_GUI" = false ] && [ -z "$FLUTTER_CMD" ]; then
    log_warn "Flutter not found! Bỏ qua Flutter build."
    log_warn "Tìm Flutter tại: https://flutter.dev/docs/get-started/install/linux"
    SKIP_GUI=true
elif [ "$SKIP_GUI" = false ]; then
    log_success "Flutter found: $FLUTTER_CMD"
fi

# ── Bước 1: Dọn dẹp staging directory ──
log_section "Chuẩn bị staging directory"

rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR"

# Tạo cấu trúc thư mục FHS
mkdir -p "$STAGING_DIR/usr/lib/vnxkey"
mkdir -p "$STAGING_DIR/usr/bin"
mkdir -p "$STAGING_DIR/lib/systemd/system"
mkdir -p "$STAGING_DIR/usr/share/applications"
mkdir -p "$STAGING_DIR/usr/share/doc/vnxkey"
mkdir -p "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps"
mkdir -p "$STAGING_DIR/DEBIAN"

log_success "Staging directory ready: $STAGING_DIR"

# ── Bước 2: Build C++ Daemon ──
log_section "Build C++ Daemon"

BUILD_DIR="$BE_DIR/build-release"
mkdir -p "$BUILD_DIR"

log_info "Running cmake..."
cmake -B "$BUILD_DIR" -S "$BE_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    2>&1 | grep -E "WARNING|ERROR|libevdev|Build type" || true

log_info "Building vnxkey-daemon..."
cmake --build "$BUILD_DIR" --target vnxkey-daemon -j"$(nproc)"

DAEMON_BIN="$BUILD_DIR/vnxkey-daemon"
if [ ! -f "$DAEMON_BIN" ]; then
    log_error "Build failed: $DAEMON_BIN not found"
    exit 1
fi

# Copy daemon binary
cp "$DAEMON_BIN" "$STAGING_DIR/usr/lib/vnxkey/vnxkey-daemon"
chmod 755 "$STAGING_DIR/usr/lib/vnxkey/vnxkey-daemon"
log_success "Daemon binary: $(du -sh "$STAGING_DIR/usr/lib/vnxkey/vnxkey-daemon" | cut -f1)"

# Copy systemd service
cp "$BE_DIR/systemd/vnxkey.service" "$STAGING_DIR/lib/systemd/system/"
log_success "Systemd service copied"

# ── Bước 3: Build Flutter GUI ──
if [ "$SKIP_GUI" = false ]; then
    log_section "Build Flutter GUI"

    log_info "Running flutter clean (fixing permission issues)..."
    cd "$FE_DIR"
    "$FLUTTER_CMD" clean > /dev/null 2>&1 || true

    log_info "Running flutter build linux --release..."
    "$FLUTTER_CMD" build linux --release 2>&1 | tail -5
    cd "$PROJECT_ROOT"

    FLUTTER_BUNDLE="$FE_DIR/build/linux/x64/release/bundle"
    if [ ! -d "$FLUTTER_BUNDLE" ]; then
        log_error "Flutter build failed: bundle not found at $FLUTTER_BUNDLE"
        exit 1
    fi

    # Copy Flutter bundle
    mkdir -p "$STAGING_DIR/usr/lib/vnxkey/gui"
    cp -r "$FLUTTER_BUNDLE/." "$STAGING_DIR/usr/lib/vnxkey/gui/"
    
    # Rename binary from vnxkey_ui to vnxkey-ui (or just vnxkey? Wait, it was vnxkey before)
    if [ -f "$STAGING_DIR/usr/lib/vnxkey/gui/vnxkey_ui" ]; then
        mv "$STAGING_DIR/usr/lib/vnxkey/gui/vnxkey_ui" "$STAGING_DIR/usr/lib/vnxkey/gui/vnxkey"
    fi
    chmod 755 "$STAGING_DIR/usr/lib/vnxkey/gui/vnxkey"

    # Tạo launcher script trong /usr/bin/
    cat > "$STAGING_DIR/usr/bin/vnxkey-gui" << 'LAUNCHER'
#!/bin/bash
# VNXKey GUI launcher
exec /usr/lib/vnxkey/gui/vnxkey "$@"
LAUNCHER
    chmod 755 "$STAGING_DIR/usr/bin/vnxkey-gui"

    log_success "Flutter bundle: $(du -sh "$STAGING_DIR/usr/lib/vnxkey/gui" | cut -f1)"
else
    log_warn "Flutter GUI skipped (--skip-gui or Flutter not found)"
fi

# ── Bước 4: Copy packaging files ──
log_section "Copy packaging files"

# Desktop file
if [ -f "$PACKAGING_DIR/usr/share/applications/vnxkey.desktop" ]; then
    cp "$PACKAGING_DIR/usr/share/applications/vnxkey.desktop" \
       "$STAGING_DIR/usr/share/applications/"
    log_success "Desktop entry copied"
fi

# Copyright
if [ -f "$PACKAGING_DIR/usr/share/doc/vnxkey/copyright" ]; then
    cp "$PACKAGING_DIR/usr/share/doc/vnxkey/copyright" \
       "$STAGING_DIR/usr/share/doc/vnxkey/"
fi

# Icon
mkdir -p "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps"
if [ -f "$FE_DIR/assets/logo/512x512.svg" ]; then
    cp "$FE_DIR/assets/logo/512x512.svg" "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/vnxkey.svg"
    log_success "Icon copied"
fi

# Tạo changelog.gz (required by Debian policy)
echo "vnxkey ($VERSION) stable; urgency=low

  * Initial release
  * Telex and VNI input methods
  * GTK Unicode output via Ctrl+Shift+U
  * systemd service with auto-restart

 -- VNXKey Team <maintainer@vnxkey.dev>  $(date -R)" | gzip -9 > \
    "$STAGING_DIR/usr/share/doc/vnxkey/changelog.gz"

log_success "Documentation files ready"

# ── Bước 5: Tạo DEBIAN control files ──
log_section "Tạo DEBIAN control files"

# Tính installed size (KB)
INSTALLED_SIZE=$(du -sk "$STAGING_DIR" | cut -f1)

# Tạo control file với installed size thực tế
cat > "$STAGING_DIR/DEBIAN/control" << CONTROL
Package: ${PACKAGE_NAME}
Version: ${VERSION}-1
Architecture: ${ARCH}
Maintainer: VNXKey Team <maintainer@vnxkey.dev>
Installed-Size: ${INSTALLED_SIZE}
Depends: libevdev2 (>= 1.0), libc6 (>= 2.17)
Section: utils
Priority: optional
Homepage: https://github.com/vnxkey/vnxkey
Description: VNXKey - Vietnamese Input Method for Linux
 Kernel-level Vietnamese input method daemon that intercepts keyboard
 events via evdev and outputs Vietnamese characters (Telex/VNI).
 .
 Features:
  - No IBus/Fcitx5 required
  - Works on both X11 and Wayland
  - Telex and VNI input methods
  - Real-time config via JSON + inotify
  - System tray settings GUI
CONTROL

# Copy DEBIAN scripts
for script in postinst prerm postrm; do
    if [ -f "$PACKAGING_DIR/DEBIAN/$script" ]; then
        cp "$PACKAGING_DIR/DEBIAN/$script" "$STAGING_DIR/DEBIAN/$script"
        chmod 755 "$STAGING_DIR/DEBIAN/$script"
        log_success "Copied $script"
    else
        log_warn "$script not found in $PACKAGING_DIR/DEBIAN/"
    fi
done

# ── Bước 6: Đóng gói .deb ──
log_section "Đóng gói .deb"

mkdir -p "$DIST_DIR"
DEB_OUTPUT="$DIST_DIR/$DEB_FILENAME"

# Phải dùng fakeroot để dpkg-deb set correct permissions
fakeroot dpkg-deb --build --root-owner-group "$STAGING_DIR" "$DEB_OUTPUT"

if [ ! -f "$DEB_OUTPUT" ]; then
    log_error "dpkg-deb failed: $DEB_OUTPUT not found"
    exit 1
fi

# ── Kết quả ──
log_section "Hoàn thành!"

DEB_SIZE=$(du -sh "$DEB_OUTPUT" | cut -f1)
echo ""
echo -e "${GREEN}${BOLD}  ✓ Package tạo thành công!${NC}"
echo ""
echo -e "  📦 File   : ${BOLD}$DEB_OUTPUT${NC}"
echo -e "  📏 Size   : ${BOLD}$DEB_SIZE${NC}"
echo ""
echo -e "${BOLD}  Cài đặt:${NC}"
echo -e "    sudo dpkg -i $DEB_OUTPUT"
echo -e "    sudo apt-get install -f  # Fix dependencies nếu cần"
echo ""
echo -e "${BOLD}  Kiểm tra:${NC}"
echo -e "    dpkg --info $DEB_OUTPUT"
echo -e "    dpkg -c $DEB_OUTPUT      # Xem nội dung"
echo ""
