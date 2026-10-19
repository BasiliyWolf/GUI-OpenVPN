# Публикация GUI OpenVPN [aka BasiliyWolf]

## 1. GitHub

Рекомендуемое имя репозитория: `GUI-OpenVPN-BasiliyWolf`.

Описание (About):

> OpenVPN GUI for Linux Mint and Ubuntu · C++17 / Qt 6 · NetworkManager · VPN profiles, routing, traffic monitoring, tray and RU/EN interface.

Темы: `openvpn`, `linux-mint`, `ubuntu`, `qt6`, `cpp`, `vpn`, `networkmanager`, `linux`, `networking`.

Перед публичным размещением: выбрать лицензию, проверить, что исходники не содержат ключей, учётных данных и личных адресов; проверить запуск после установки DEB на чистой Ubuntu/Mint. Скриншоты также проверить на личные данные.

Создать новый репозиторий на GitHub (без автогенерации README), затем из корня проекта:

```bash
git init
git add .
git commit -m "Release v1.0.1"
git branch -M main
git remote add origin https://github.com/YOUR_ACCOUNT/GUI-OpenVPN-BasiliyWolf.git
git push -u origin main
git tag -a v1.0.1 -m "v1.0.1"
git push origin v1.0.1
```

Заменить `YOUR_ACCOUNT` на свой логин. В GitHub открыть **Releases → Draft a new release**, выбрать `v1.0.1`, прикрепить отдельно DEB, добавить текст из `GITHUB_RELEASE.en.md` и, при желании, ссылку на русскую версию. DEB лучше публиковать как вложение к Release, а не коммитить двоичный пакет в историю исходников.

## 2. Подготовка для Linux Mint

Наиболее реалистичный путь в Менеджер программ Mint — публикация через Flathub. Это **отдельный процесс**: наличие DEB на GitHub не добавляет приложение в каталог Mint автоматически.

Требуется:

1. Обеспечить устойчивую сборку и запуск в Linux Mint и Ubuntu, исправить определение реального VPN-интерфейса.
2. Перевести все пользовательские сообщения на английский без смешения языков. Заполнить русскую локализацию.
3. Завершить тесты изолированного режима, аварийного отключения, DNS/IPv4/IPv6 и правил firewall. Пока такой аудит не проведён — не заявлять гарантированный Kill Switch.
4. Пересобрать DEB с проверенными зависимостями на целевых версиях Ubuntu/Mint; устранить конфликтующие/лишние Qt 6 `t64` зависимости.
5. Разработать Flatpak-совместимую модель управления NetworkManager и системным helper. Flatpak sandbox не даёт неограниченных привилегий; не пытаться обходить это широким доступом к системной шине. Возможно, понадобится отдельный системный helper, устанавливаемый вне Flatpak, либо Flatpak-версия с ограниченной функциональностью.
6. Выбрать корректный reverse-DNS App ID с учётом реального GitHub-аккаунта, например `io.github.ACCOUNT.GUIOpenVPN`, где `ACCOUNT` заменяется реальным владельцем.
7. Добавить и валидировать AppStream metainfo, `.desktop`, иконку, Flatpak manifest и локальные тесты Flatpak.
8. Подать manifest в Flathub по официальной процедуре и пройти review.

Официальные правила:
- https://docs.flathub.org/docs/for-app-authors/requirements
- https://docs.flathub.org/docs/for-app-authors/submission
- https://docs.flathub.org/docs/for-app-authors/metainfo-guidelines
- https://docs.flatpak.org/en/latest/manifests.html

Альтернатива — собственный подписанный APT-репозиторий или подача пакета сопровождающим дистрибутива. Собственный APT-репозиторий не включается автоматически в стандартные источники Mint: пользователи должны добавлять его вручную.

## 3. Минимальные проверки перед подачей

```bash
dpkg-deb --info gui-openvpn-basiliywolf-1.0.1-Linux.deb
dpkg-deb --contents gui-openvpn-basiliywolf-1.0.1-Linux.deb
sudo apt install ./gui-openvpn-basiliywolf-1.0.1-Linux.deb
```

Проверьте также установку/удаление на чистых VM, работу Keyring, NetworkManager, tray в Cinnamon и GNOME, перевод каждого диалога и диагностику сети.

## 4. Статус

Документы для GitHub подготовлены. GitHub-репозиторий не создан и релиз не опубликован. Flatpak manifest ещё не готов, заявка в Flathub не отправлена. До этих шагов необходимы исправления и проверки, перечисленные выше.
