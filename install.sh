#!/usr/bin/env sh

set -eu

repo="Saladin1812/Roundtable"
version="latest"
install_dir=""
assume_yes="false"

usage() {
  cat <<'EOF'
Usage: install.sh [options]

Downloads and installs the latest Roundtable release by default.

Options:
  --yes, -y             Install without prompting.
  --repo OWNER/REPO     GitHub repository to download from.
  --version TAG         Release tag to install instead of latest.
  --install-dir PATH    Install directory override.
  --help, -h            Show this help.

Examples:
  ./install.sh
  ./install.sh --version v0.1.0-alpha
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --yes|-y)
      assume_yes="true"
      shift
      ;;
    --repo)
      repo="${2:?missing value for --repo}"
      shift 2
      ;;
    --version)
      version="${2:?missing value for --version}"
      shift 2
      ;;
    --install-dir)
      install_dir="${2:?missing value for --install-dir}"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

detect_os() {
  case "$(uname -s)" in
    Linux) echo "linux" ;;
    Darwin) echo "macos" ;;
    *)
      echo "Unsupported OS: $(uname -s)" >&2
      exit 1
      ;;
  esac
}

detect_arch() {
  case "$(uname -m)" in
    x86_64|amd64) echo "x86_64" ;;
    aarch64|arm64) echo "aarch64" ;;
    *)
      echo "Unsupported architecture: $(uname -m)" >&2
      exit 1
      ;;
  esac
}

download_file() {
  url="$1"
  output_path="$2"

  if command -v curl >/dev/null 2>&1; then
    curl -fL "$url" -o "$output_path"
    return
  fi

  if command -v wget >/dev/null 2>&1; then
    wget -O "$output_path" "$url"
    return
  fi

  echo "Install requires curl or wget." >&2
  exit 1
}

confirm_install() {
  if [ "$assume_yes" = "true" ]; then
    return
  fi

  printf "Continue? [y/N] "
  answer=""
  if [ -e /dev/tty ]; then
    answer="$(sh -c 'IFS= read -r value </dev/tty && printf "%s" "$value"' 2>/dev/null || true)"
  fi
  if [ -z "$answer" ]; then
    IFS= read -r answer || answer=""
  fi

  case "$answer" in
    y|Y|yes|YES) ;;
    *)
      echo "Installation cancelled."
      exit 0
      ;;
  esac
}

default_install_dir() {
  if [ "$(id -u)" -eq 0 ]; then
    echo "/usr/local/bin"
  else
    echo "$HOME/.local/bin"
  fi
}

if [ -z "$install_dir" ]; then
  install_dir="$(default_install_dir)"
fi

if [ "$(id -u)" -eq 0 ]; then
  echo "You are running as root, possibly through sudo."
  echo "This will install Roundtable system-wide to: $install_dir"
  echo "Recommended: run install.sh without sudo to install to ~/.local/bin instead."
else
  echo "This will install Roundtable for the current user to: $install_dir"
fi

tmp_dir=""
binary_path=""

os="$(detect_os)"
arch="$(detect_arch)"
asset="roundtable-${os}-${arch}.tar.gz"

if [ "$version" = "latest" ]; then
  url="https://github.com/${repo}/releases/latest/download/${asset}"
else
  url="https://github.com/${repo}/releases/download/${version}/${asset}"
fi

tmp_dir="$(mktemp -d)"
trap 'if [ -n "$tmp_dir" ]; then rm -rf "$tmp_dir"; fi' EXIT HUP INT TERM

archive_path="$tmp_dir/$asset"
echo "Downloading: $url"
download_file "$url" "$archive_path"

tar -xzf "$archive_path" -C "$tmp_dir"
binary_path="$(find "$tmp_dir" -type f -name roundtable | head -n 1)"

if [ -z "$binary_path" ]; then
  echo "Downloaded archive did not contain a roundtable binary." >&2
  exit 1
fi

if [ ! -x "$binary_path" ]; then
  chmod +x "$binary_path"
fi

confirm_install

mkdir -p "$install_dir"
cp "$binary_path" "$install_dir/roundtable"
chmod 755 "$install_dir/roundtable"

echo "Installed: $install_dir/roundtable"

case ":$PATH:" in
  *":$install_dir:"*)
    echo "Verified: $install_dir is on PATH."
    ;;
  *)
    echo "Warning: $install_dir is not on PATH."
    echo "Add it to your shell config or run: $install_dir/roundtable"
    ;;
esac
