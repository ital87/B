#!/bin/bash

set -e

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        echo "Windows detected (running inside $(uname -s))."
        echo "Please use the native Windows one-liner instead (PowerShell):"
        echo
        echo "     irm https://raw.githubusercontent.com/ital87/B/main/get.ps1 | iex"
        exit 1
        ;;
esac

REPO_URL="https://github.com/ital87/B.git"
REPO_BRANCH="main"
INSTALL_ROOT="$HOME/.b"
REPO_DIR="$INSTALL_ROOT/repo"
BIN_DIR="$INSTALL_ROOT/bin"

prompt_choice() {
    local prompt="$1"
    local choice
    if exec 3< /dev/tty 2> /dev/null; then
        read -rp "$prompt" choice <&3
        exec 3<&-
    else
        read -rp "$prompt" choice
    fi
    echo "$choice"
}

ensure_git() {
    if command -v git > /dev/null 2>&1; then
        return
    fi

    echo "git wird benötigt, versuche Installation..."
    if command -v pacman > /dev/null 2>&1; then
        sudo pacman -Sy --noconfirm git
    elif command -v apt-get > /dev/null 2>&1; then
        sudo apt-get update -qq && sudo apt-get install -y -qq git
    elif command -v dnf > /dev/null 2>&1; then
        sudo dnf install -y git
    else
        echo "Error: Bitte git manuell installieren und dieses Skript erneut ausführen."
        exit 1
    fi
}

do_install() {
    ensure_git

    echo "=========================================="
    echo "B wird heruntergeladen und installiert..."
    echo "=========================================="
    echo

    mkdir -p "$INSTALL_ROOT"
    rm -rf "$REPO_DIR"
    git clone --depth=1 --branch "$REPO_BRANCH" "$REPO_URL" "$REPO_DIR"

    ARC_NO_SHELL_RELOAD=1 bash "$REPO_DIR/install.sh"
}

print_change_summary() {
    local old_head="$1"
    local new_head="$2"

    if [ -z "$old_head" ] || [ "$old_head" = "$new_head" ]; then
        echo "Bereits auf dem neuesten Stand."
        return
    fi

    local diff_output added deleted modified
    diff_output=$(git diff --name-status "$old_head" "$new_head" 2> /dev/null)
    added=$(echo "$diff_output" | grep -c '^A' || true)
    deleted=$(echo "$diff_output" | grep -c '^D' || true)
    modified=$(echo "$diff_output" | grep -c '^M' || true)

    echo "Änderungen seit deiner letzten Version:"
    echo "  Neue Dateien:      $added"
    echo "  Geänderte Dateien: $modified"
    echo "  Gelöschte Dateien: $deleted"
}

do_update() {
    echo "=========================================="
    echo "B wird aktualisiert..."
    echo "=========================================="
    echo

    cd "$REPO_DIR"
    local old_head
    old_head=$(git rev-parse HEAD 2> /dev/null)

    git fetch --depth=1 origin "$REPO_BRANCH"
    git reset --hard "origin/$REPO_BRANCH"

    local new_head
    new_head=$(git rev-parse HEAD 2> /dev/null)

    echo
    print_change_summary "$old_head" "$new_head"
    echo

    ARC_NO_SHELL_RELOAD=1 bash "$REPO_DIR/install.sh"

    echo
    echo "✓ Update abgeschlossen."
}

reload_shell() {
    if [ ! -t 0 ] || [ ! -t 1 ]; then
        echo "Reload your shell to use 'b':"
        echo "     source ~/.bashrc  (for bash)"
        echo "     source ~/.zshrc   (for zsh)"
        echo
        return
    fi

    export PATH="$BIN_DIR:$PATH"
    echo "Starting a new shell session with the updated PATH..."
    echo "(type 'exit' to return)"
    echo
    exec "${SHELL:-bash}" -l
}

do_uninstall() {
    echo "=========================================="
    echo "B wird deinstalliert..."
    echo "=========================================="
    echo

    rm -rf "$INSTALL_ROOT"

    for rc in "$HOME/.bashrc" "$HOME/.zshrc"; do
        if [ -f "$rc" ]; then
            sed -i '/\.b\/bin/d' "$rc"
        fi
    done

    echo "✓ Arc wurde vollständig entfernt."
    echo "  Starte deine Shell neu, damit der PATH-Eintrag verschwindet:"
    echo "     source ~/.bashrc  (oder ~/.zshrc)"
}

if [ "$1" = "update" ]; then
    if [ -d "$REPO_DIR/.git" ]; then
        do_update
    else
        do_install
    fi
    reload_shell
    exit 0
fi

if [ -d "$REPO_DIR/.git" ] && [ -x "$BIN_DIR/b" ]; then
    echo "=========================================="
    echo "B ist bereits installiert."
    echo "=========================================="
    echo
    echo "[1] Update"
    echo "[2] Uninstall"
    echo
    CHOICE=$(prompt_choice "Auswahl [1/2]: ")

    case "$CHOICE" in
        1)
            do_update
            reload_shell
            ;;
        2)
            do_uninstall
            ;;
        *)
            echo "Ungültige Auswahl. Abbruch."
            exit 1
            ;;
    esac
else
    do_install
    reload_shell
fi

