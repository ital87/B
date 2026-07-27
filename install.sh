#!/bin/bash

set -e

echo "=========================================="
echo "B Compiler - Autarkes Installer"
echo "=========================================="
echo

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        echo "Windows detected (running inside $(uname -s))."
        echo "Please use the native Windows installer instead:"
        echo
        echo "     powershell -ExecutionPolicy Bypass -File install.ps1"
        echo
        echo "Or the one-liner (PowerShell):"
        echo "     irm https://raw.githubusercontent.com/ital87/Arc/1.0.0/get.ps1 | iex"
        exit 1
        ;;
esac

REAL_USER="${SUDO_USER:-${USER:-$(id -un)}}"
REAL_HOME="$(eval echo "~$REAL_USER")"

detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo "$ID"
    elif [ -f /etc/redhat-release ]; then
        echo "rhel"
    elif [ -f /etc/debian_version ]; then
        echo "debian"
    elif [ -f /etc/arch-release ]; then
        echo "arch"
    else
        echo "unknown"
    fi
}

need_sudo() {
    if [ "$EUID" -ne 0 ]; then
        echo "sudo"
    else
        echo ""
    fi
}

verify_toolchain() {
    local missing=0
    for tool in g++ llvm-config llc git cmake; do
        if ! command -v "$tool" > /dev/null 2>&1; then
            echo "Error: Required tool not found: $tool"
            missing=1
        fi
    done
    return $missing
}

install_deps_debian() {
    echo "[1/3] Installing build dependencies (Debian/Ubuntu)..."
    local SUDO_CMD
    SUDO_CMD=$(need_sudo)

    $SUDO_CMD apt-get update -qq || true

    BUILD_DEPS=(
        "build-essential"
        "cmake"
        "git"
        "llvm"
        "llvm-dev"
        "clang"
    )

    $SUDO_CMD apt-get install -y -qq "${BUILD_DEPS[@]}" || true
}

install_deps_fedora() {
    echo "[1/3] Installing build dependencies (Fedora/RHEL)..."
    local SUDO_CMD
    SUDO_CMD=$(need_sudo)

    $SUDO_CMD dnf groupinstall -y "Development Tools" > /dev/null 2>&1 || true

    BUILD_DEPS=(
        "cmake"
        "git"
        "llvm-devel"
        "clang"
    )

    $SUDO_CMD dnf install -y "${BUILD_DEPS[@]}" > /dev/null 2>&1 || true
}

install_deps_arch() {
    echo "[1/3] Installing build dependencies (Arch Linux)..."
    local SUDO_CMD
    SUDO_CMD=$(need_sudo)

    $SUDO_CMD pacman -Sy --noconfirm > /dev/null 2>&1 || true

    BUILD_DEPS=(
        "base-devel"
        "cmake"
        "git"
        "llvm"
        "clang"
    )

    $SUDO_CMD pacman -S --needed --noconfirm "${BUILD_DEPS[@]}" > /dev/null 2>&1 || true
}

DISTRO=$(detect_distro)

echo "Detected Linux distribution: $DISTRO"
echo "Installing for user: $REAL_USER (home: $REAL_HOME)"
echo

case "$DISTRO" in
    ubuntu|debian)
        install_deps_debian
        ;;
    fedora|rhel|centos)
        install_deps_fedora
        ;;
    arch|manjaro)
        install_deps_arch
        ;;
    *)
        echo "Error: Unsupported distribution: $DISTRO"
        echo "Please install the following manually:"
        echo "  - build-essential (or equivalent development tools)"
        echo "  - cmake"
        echo "  - git"
        echo "  - llvm-devel (or llvm-dev)"
        exit 1
        ;;
esac

if ! verify_toolchain; then
    echo "Error: Missing required build tools after dependency installation."
    echo "Please install g++, llvm-config, llc, git, and cmake manually and re-run this script."
    exit 1
fi

echo
echo "[2/3] Compiling B compiler..."

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
SRC_FILE="$SCRIPT_DIR/src/b_combined.cpp"
B_BINARY="$BUILD_DIR/b"

if [ ! -f "$SRC_FILE" ]; then
    echo "Error: Source file not found at $SRC_FILE"
    exit 1
fi

mkdir -p "$BUILD_DIR"

CXXFLAGS="-std=c++17 $(llvm-config --cxxflags) -fexceptions"
LDFLAGS="$(llvm-config --ldflags --system-libs --libs all)"

if ! g++ $CXXFLAGS "$SRC_FILE" -o "$B_BINARY" $LDFLAGS 2>&1; then
    echo "Error: Compilation failed"
    exit 1
fi

chmod +x "$B_BINARY"
echo "✓ Compilation successful: $B_BINARY"
echo

echo "[3/3] Installing B compiler..."

INSTALL_DIR="$REAL_HOME/.b/bin"

mkdir -p "$INSTALL_DIR"
cp "$B_BINARY" "$INSTALL_DIR/b.new"
chmod +x "$INSTALL_DIR/b.new"
mv -f "$INSTALL_DIR/b.new" "$INSTALL_DIR/b"

if [ "$EUID" -eq 0 ] && [ -n "$SUDO_USER" ]; then
    chown -R "$REAL_USER":"$REAL_USER" "$REAL_HOME/.b"
fi

echo "✓ B compiler installed to: $INSTALL_DIR/b"
echo

update_shell_config() {
    local config_file="$1"
    local export_line="export PATH=\"\$HOME/.b/bin:\$PATH\""

    if [ -f "$config_file" ]; then
        if ! grep -q "\.b/bin" "$config_file"; then
            echo "" >> "$config_file"
            echo "$export_line" >> "$config_file"
            echo "  Updated: $config_file"
        fi
    else
        echo "$export_line" > "$config_file"
        echo "  Created: $config_file"
    fi

    if [ "$EUID" -eq 0 ] && [ -n "$SUDO_USER" ]; then
        chown "$REAL_USER":"$REAL_USER" "$config_file"
    fi
}

echo "Updating shell configuration files..."
update_shell_config "$REAL_HOME/.bashrc"
update_shell_config "$REAL_HOME/.zshrc"
echo

echo "=========================================="
echo "✓ Installation complete!"
echo "=========================================="
echo
echo "Test the B compiler:"
echo "     b --version"
echo
echo "Compile a B file:"
echo "     b examples/hello.b"
echo

reload_shell() {
    if [ -n "$B_NO_SHELL_RELOAD" ]; then
        return
    fi

    if [ ! -t 0 ] || [ ! -t 1 ]; then
        echo "Reload your shell to use 'b':"
        echo "     source ~/.bashrc  (for bash)"
        echo "     source ~/.zshrc   (for zsh)"
        echo
        return
    fi

    export PATH="$INSTALL_DIR:$PATH"
    echo "Starting a new shell session with the updated PATH..."
    echo "(type 'exit' to return)"
    echo
    exec "${SHELL:-bash}" -l
}

reload_shell

