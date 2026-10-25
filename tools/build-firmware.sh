#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
set -euo pipefail

readonly FREETZ_COMMIT="995afdf2ded44fbb342e69e5941d9ad36a274e93"
readonly FREETZ_URL="https://github.com/Freetz-NG/freetz-ng.git"

usage() {
    cat <<'EOF'
Usage: tools/build-firmware.sh [options]

Build the validated FRITZ!Box 7530 / FRITZ!OS 8.25 image locally.

Options:
  --freetz-dir PATH   Checkout/build directory (default: .build/freetz-ng)
  --jobs N            Parallel build jobs (default: number of CPUs)
  --install-prerequisites
                      Ask Freetz-NG to install missing host packages
  --menuconfig        Open Freetz menuconfig before building
  --prepare-only      Prepare checkout and configuration, do not build
  --flash             Build, then start Freetz-NG's interactive first-install
  --box-ip ADDRESS    Bootloader address for --flash (default: 192.168.178.1)
  -h, --help          Show this help

The script never downloads or publishes a prebuilt modified firmware. Freetz
obtains the original firmware during the user's local build.
EOF
}

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$script_dir/.." && pwd)"
freetz_dir="$project_dir/.build/freetz-ng"
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
menuconfig=no
prepare_only=no
flash=no
install_prerequisites=no
box_ip="192.168.178.1"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --freetz-dir)
            [[ $# -ge 2 ]] || { echo "--freetz-dir needs a path" >&2; exit 2; }
            freetz_dir="$2"
            shift 2
            ;;
        --jobs)
            [[ $# -ge 2 && "$2" =~ ^[1-9][0-9]*$ ]] || { echo "invalid --jobs" >&2; exit 2; }
            jobs="$2"
            shift 2
            ;;
        --install-prerequisites) install_prerequisites=yes; shift ;;
        --menuconfig) menuconfig=yes; shift ;;
        --prepare-only) prepare_only=yes; shift ;;
        --flash) flash=yes; shift ;;
        --box-ip)
            [[ $# -ge 2 && -n "$2" ]] || { echo "--box-ip needs an address" >&2; exit 2; }
            box_ip="$2"
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ "$prepare_only" == yes && "$flash" == yes ]]; then
    echo "--prepare-only and --flash cannot be used together" >&2
    exit 2
fi

command -v git >/dev/null || {
    echo "Git is required to fetch Freetz-NG." >&2
    exit 1
}

if [[ ! -d "$freetz_dir/.git" ]]; then
    mkdir -p "$(dirname -- "$freetz_dir")"
    git clone "$FREETZ_URL" "$freetz_dir"
fi

current_commit="$(git -C "$freetz_dir" rev-parse HEAD)"
if [[ "$current_commit" != "$FREETZ_COMMIT" ]]; then
    if [[ -n "$(git -C "$freetz_dir" status --porcelain)" ]]; then
        echo "Freetz checkout has local changes and is not at the validated commit." >&2
        echo "Use a fresh --freetz-dir or handle those changes manually." >&2
        exit 1
    fi
    git -C "$freetz_dir" fetch origin "$FREETZ_COMMIT"
    git -C "$freetz_dir" checkout --detach "$FREETZ_COMMIT"
fi

if [[ "$install_prerequisites" == yes ]]; then
    echo "Checking and installing Freetz-NG build prerequisites..."
    "$freetz_dir/tools/prerequisites" install
fi

for command in make tar sha256sum sed; do
    command -v "$command" >/dev/null || {
        echo "Missing build command after the prerequisite check: $command" >&2
        exit 1
    }
done

"$script_dir/install-freetz-package.sh" "$freetz_dir"
cp "$project_dir/freetz/configs/7530_08.25-miniconfig" "$freetz_dir/.config"

echo "Resolving the pinned FRITZ!Box 7530 configuration..."
make -C "$freetz_dir" olddefconfig

if [[ "$menuconfig" == yes ]]; then
    make -C "$freetz_dir" menuconfig
fi

if [[ "$prepare_only" == yes ]]; then
    echo "Prepared $freetz_dir; build later with: make -C '$freetz_dir' -j'$jobs'"
    exit 0
fi

echo "Building locally. The first Freetz build can take a long time."
make -C "$freetz_dir" -j"$jobs"

echo "Build finished. Candidate images:"
find "$freetz_dir/images" -maxdepth 1 -type f -name '*.image' -print
echo "Read docs/installation.md before flashing. Do not redistribute the image."

if [[ "$flash" == yes ]]; then
    image_path="$freetz_dir/images/latest.image"
    [[ -f "$image_path" ]] || {
        echo "Freetz did not create images/latest.image; refusing to flash." >&2
        exit 1
    }
    echo
    echo "The interactive Freetz installer will now wait for the bootloader at $box_ip."
    echo "Connect the dedicated 7530 directly by Ethernet and follow its prompts."
    "$freetz_dir/tools/push_firmware" "$image_path" -ip "$box_ip"
fi
