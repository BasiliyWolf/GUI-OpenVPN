# Publish GUI OpenVPN v1.0.1 / Публикация GUI OpenVPN v1.0.1

Repository: https://github.com/BasiliyWolf/GUI-OpenVPN
License: GPLv3, included in `LICENSE`.

## Publish source and DEB / Опубликовать исходники и DEB

1. Extract the package / Распакуйте архив.
2. Install Git and GitHub CLI (`git`, `gh`).
3. Authenticate: `gh auth login`.
4. From the extracted folder, run `./publish-github.sh`.

The script clones the existing repository, commits source, GPLv3 license, documentation and ten real screenshots, pushes `main`, creates release `v1.0.1`, and uploads `gui-openvpn-basiliywolf-1.0.1-Linux.deb`. Existing releases get their DEB asset updated. `*.deb` files are excluded from Git source history.

The isolated-network Kill Switch is experimental and has not undergone a complete leak test. Some secondary dialogs mix Russian and English, and traffic accounting needs verification against the true tunnel interface. Check compatibility with Ubuntu/Mint versions before installing the DEB.
