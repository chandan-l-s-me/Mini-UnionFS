#!/bin/bash
# build.sh - Platform-aware build script for Mini-UnionFS
#
# This script detects the operating system and sets up the appropriate
# build environment for Mini-UnionFS.

set -e

echo "========================================"
echo "Mini-UnionFS Build System"
echo "========================================"
echo ""

# Detect OS
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS="Linux"
    DISTRO=""
    
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO=$ID
    fi
    
    echo "Detected OS: Linux ($DISTRO)"
    echo ""
    
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macOS"
    echo "Detected OS: macOS"
    echo ""
    echo "⚠️  Note: Mini-UnionFS is designed for Linux with FUSE 3.x"
    echo "   macOS support requires OSXFuse or macFUSE installation."
    echo ""
    echo "To build on macOS:"
    echo "  1. Install macFUSE: brew install macfuse"
    echo "  2. Or install OSXFuse from https://osxfuse.github.io/"
    exit 1
    
else
    echo "⚠️  Unknown/unsupported operating system: $OSTYPE"
    exit 1
fi

# Check for required dependencies
echo "Checking dependencies..."
echo ""

# Check for compiler
if ! command -v gcc &> /dev/null && ! command -v clang &> /dev/null; then
    echo "❌ C compiler not found"
    echo "   Install with: sudo apt-get install build-essential"
    exit 1
fi
echo "✓ C compiler found"

# Check for pkg-config
if ! command -v pkg-config &> /dev/null; then
    echo "❌ pkg-config not found"
    echo "   Install with: sudo apt-get install pkg-config"
    exit 1
fi
echo "✓ pkg-config found"

# Check for FUSE
if ! pkg-config --exists fuse; then
    echo "❌ FUSE 3.x development libraries not found"
    echo ""
    echo "   Install with:"
    if [ "$DISTRO" = "ubuntu" ] || [ "$DISTRO" = "debian" ]; then
        echo "     sudo apt-get update"
        echo "     sudo apt-get install libfuse-dev"
    elif [ "$DISTRO" = "fedora" ]; then
        echo "     sudo dnf install fuse-devel"
    elif [ "$DISTRO" = "arch" ]; then
        echo "     sudo pacman -S fuse3"
    else
        echo "     Installing FUSE for your distro - consult your package manager"
    fi
    exit 1
fi
echo "✓ FUSE 3.x found"

echo ""
echo "========================================"
echo "Starting build..."
echo "========================================"
echo ""

# Clean old builds
make clean 2>/dev/null || true

# Build
if make; then
    echo ""
    echo "========================================"
    echo "✓ Build successful!"
    echo "========================================"
    echo ""
    echo "Next steps:"
    echo "  1. Run tests:  make test"
    echo "  2. Mount:      ./mini_unionfs <lower> <upper> <mount_point>"
    echo "  3. Unmount:    fusermount -u <mount_point>"
    echo ""
else
    echo ""
    echo "❌ Build failed"
    exit 1
fi
