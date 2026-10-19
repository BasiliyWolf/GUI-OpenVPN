#!/usr/bin/env bash
set -euo pipefail
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev libqt6svg6-dev libxkbcommon-dev libxkbcommon-x11-dev libsecret-tools network-manager network-manager-openvpn network-manager-openvpn-gnome network-manager-gnome librsvg2-common iproute2 nftables policykit-1 openvpn
