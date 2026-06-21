#!/usr/bin/env bash
# ==============================================================================
# install-deps.sh - Cài đặt tất cả dependencies cần thiết để build VNXKey
# ==============================================================================
# Sử dụng: ./scripts/install-deps.sh
# Yêu cầu: Ubuntu/Debian

set -e

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  VNXKey - Cài đặt Dependencies"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Check distro
if ! command -v apt-get &>/dev/null; then
    echo "ERROR: Script này chỉ hỗ trợ Debian/Ubuntu (apt-get)."
    exit 1
fi

echo ""
echo "▶ Cập nhật package list..."
apt-get update -qq

echo ""
echo "▶ Cài C++ build tools..."
apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    git

echo ""
echo "▶ Cài libevdev (keyboard interception)..."
apt-get install -y \
    libevdev-dev \
    libevdev2

echo ""
echo "▶ Cài Flutter Linux build dependencies..."
apt-get install -y \
    libgtk-3-dev \
    libblkid-dev \
    liblzma-dev \
    ninja-build \
    clang \
    cmake

echo ""
echo "▶ Cài packaging tools..."
apt-get install -y \
    fakeroot \
    dpkg-dev

echo ""
echo "▶ Cài optional: nlohmann-json..."
apt-get install -y nlohmann-json3-dev 2>/dev/null || \
    echo "  (optional - bỏ qua nếu lỗi)"

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  ✓ Tất cả dependencies đã được cài!"
echo ""
echo "  Bước tiếp theo:"
echo "  ./scripts/build-release.sh"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
