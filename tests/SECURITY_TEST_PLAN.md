# Release test plan (must run before claiming stable)

Run only in a disposable Ubuntu/Mint VM with a test VPN server.

- Build under Ubuntu 22.04/24.04 and current Linux Mint using system Qt6.
- Import profiles with tls-auth, tls-crypt, inline CA/private key, username/password and encrypted key.
- Confirm keyring survives restart without plaintext data in settings, logs or argv.
- Verify regular network remains functional before/during/after isolated mode.
- Test isolated mode tunnel loss (`kill -9` OpenVPN) with packet captures on host WAN; confirm zero non-endpoint egress.
- Test IPv4 DNS, IPv6, systemd-resolved, WebRTC, local network, proxy and D-Bus delegation leakage.
- Test namespace cleanup after crash, reboot, unexpected pkexec exit and power interruption.
- Validate nftables coexistence with UFW/firewalld and interface roaming.
- Confirm OpenVPN binaries chosen are root-owned; test malicious config directives and symlink races.
- Exercise Wayland GNOME and X11 Cinnamon tray, .desktop startup, polkit and log save.
- Install and upgrade DEB, verify dependencies; purge without touching unrelated system networking.

**Do not describe isolation as a guaranteed Kill Switch before these tests pass.**
