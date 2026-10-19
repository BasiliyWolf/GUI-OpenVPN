# GUI OpenVPN [aka BasiliyWolf] v1.0.1

A C++17 / Qt 6 OpenVPN desktop client for Linux Mint and Ubuntu.

## Features

- Import `.ovpn` profiles, connect, disconnect, and reconnect through NetworkManager.
- VPN credentials with optional system Keyring storage.
- Network statistics, public IPv4, system tray, and activity logs.
- IPv4/IPv6 route editor and routing modes.
- Russian and English language selection for the main user interface.
- Experimental isolated network namespace mode for running selected applications through a VPN with firewall restrictions.

## Installation

Download `gui-openvpn-basiliywolf-1.0.1-Linux.deb` from the release assets and run:

```bash
sudo apt update
sudo apt install ./gui-openvpn-basiliywolf-1.0.1-Linux.deb
```

Launch via the desktop menu or run `gui-openvpn-basiliywolf`.

## Compatibility and known limitations

The DEB targets `amd64` and requires Qt 6, NetworkManager and additional system components. It depends on Qt 6 `t64` libraries and may not install on older Ubuntu/Linux Mint releases.

Some dialogs are only partially translated. VPN interface selection and tunnel-only traffic statistics require verification. The experimental firewall / Kill Switch has **not** been independently audited or tested against DNS/IPv4/IPv6 leaks. Do not rely on it for guaranteed leak prevention.

Source code and setup instructions are available in the repository. The author must explicitly choose and publish a license before third parties may assume redistribution/modification rights.
