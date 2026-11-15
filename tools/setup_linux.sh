#!/usr/bin/env bash
# Install prerequisites for a native Linux ReDUB build/lift.
# Usage: bash tools/setup_linux.sh [--vulkan]
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$REPO/tools/env.sh"

# Some container/WSL images run as root and omit sudo. Provide a tiny local
# sudo shim only in that case so the shared dependency installer works there.
sudo_dir=""
if (( EUID == 0 )) && ! command -v sudo >/dev/null 2>&1; then
    sudo_dir="$(mktemp -d)"
    printf '#!/usr/bin/env bash\nexec "$@"\n' > "$sudo_dir/sudo"
    chmod 700 "$sudo_dir/sudo"
    PATH="$sudo_dir:$PATH"
    export PATH
    trap 'rm -f "$sudo_dir/sudo"; rmdir "$sudo_dir"' EXIT
elif ! command -v sudo >/dev/null 2>&1; then
    echo "Install sudo or run this setup script as root." >&2
    exit 1
fi

with_vulkan=0
for arg in "$@"; do
    case "$arg" in
        --vulkan) with_vulkan=1 ;;
        -h|--help)
            echo "Usage: bash tools/setup_linux.sh [--vulkan]"
            echo "Installs base Linux build/lift dependencies; --vulkan adds Vulkan shader tools."
            exit 0
            ;;
        *) echo "Unknown option: $arg" >&2; exit 2 ;;
    esac
done

bash "$XBOXRECOMP_DIR/tools/linux/install_deps.sh"
case "$(. /etc/os-release 2>/dev/null; echo "${ID:-}")" in
    ubuntu|debian|linuxmint|pop)
        packages=(ninja-build python3-venv python3-pip libdbus-1-dev)
        if (( with_vulkan )); then
            packages+=(libvulkan-dev vulkan-tools glslang-dev glslang-tools spirv-tools)
        fi
        sudo apt-get install -y "${packages[@]}"
        ;;
    arch|cachyos|manjaro)
        packages=(ninja python python-pip dbus)
        if (( with_vulkan )); then
            packages+=(vulkan-headers vulkan-tools glslang spirv-tools)
        fi
        sudo pacman -S --needed "${packages[@]}"
        ;;
    fedora)
        packages=(ninja-build python3 python3-pip dbus-devel)
        if (( with_vulkan )); then
            packages+=(vulkan-headers vulkan-tools glslang-devel glslang spirv-tools)
        fi
        sudo dnf install -y "${packages[@]}"
        ;;
    *)
        echo "Install Ninja, Python 3 with venv/pip, and optionally Vulkan SDK headers, glslang, and SPIR-V tools." >&2
        exit 2
        ;;
esac

mkdir -p "$MC3_WORK"
if [[ ! -x "$MC3_WORK/venv/bin/python3" ]] || ! "$MC3_WORK/venv/bin/python3" -c 'import capstone' >/dev/null 2>&1; then
    python3 -m venv --copies "$MC3_WORK/venv"
    "$MC3_WORK/venv/bin/python3" -m pip install --upgrade pip capstone numpy unicorn
fi
echo
echo "Linux prerequisites are ready. Next:"
echo "  MC3_GAME_DIR=/path/to/your/extracted-xbox-remix GAME=remix make lift"
echo "  MC3_GAME_DIR=/path/to/your/extracted-xbox-remix GAME=remix make build"
echo "  MC3_GAME_DIR=/path/to/your/extracted-xbox-remix GAME=remix make run"
