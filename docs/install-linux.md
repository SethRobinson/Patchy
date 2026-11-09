# Installing Patchy on Linux

[Back to Patchy](../README.md#download)

Patchy for Linux comes as a Flatpak (x86_64 and ARM64) from Patchy's own repository, as a
single-file Flatpak bundle, and as an AppImage (x86_64). The download links are in the
[README's download table](../README.md#download).

## Flatpak

Paste this into a terminal. It installs Patchy for your user from the Patchy Flatpak
repository and pulls the shared KDE runtime from Flathub. No root needed.

```sh
flatpak install --user https://rtsoft.com/flatpak/com.rtsoft.patchy.flatpakref
```

Patchy then updates with `flatpak update` or through your software center, and starts
from your application menu or with `flatpak run com.rtsoft.patchy`.

This needs the `flatpak` tool. Fedora and openSUSE desktops usually have it already. If
needed, install it first with `sudo apt install flatpak` (Ubuntu, Debian),
`sudo dnf install flatpak` (Fedora) or `sudo zypper install flatpak` (openSUSE).

If you already use flatpak system-wide, you can use `--system` in place of `--user`.
Patchy then reuses the runtime extensions you already have (graphics drivers, codecs)
instead of downloading them again. Installing that way may ask for an admin password.

The [`.flatpakref` link](https://rtsoft.com/flatpak/com.rtsoft.patchy.flatpakref) downloads a small file that a Flatpak-aware
software center (GNOME Software, KDE Discover) can open to install Patchy. Ubuntu has
none by default, and on Ubuntu 24.04 GNOME Software's Open button fails to start
Flatpak apps, so use the terminal command above instead.

The single-file bundle is the same build, for when you would rather download a file
first. Install it with `flatpak install --user PatchyLinux.flatpak`. It still fetches
the KDE runtime from Flathub, and it updates from the same repository afterwards.

My flatpak knowledge is limited, so I hope all this makes sense.  I hope to submit Patchy to Flathub eventually, but for now we have our own repository.

## Linux on ARM64 (Raspberry Pi 5 and others)

The Flatpak also comes in a 64-bit ARM (ARM64, also called aarch64) build, and the command
in [Flatpak](#flatpak) picks it automatically on an ARM computer. It needs a 64-bit Linux with Flatpak.
Tested on a Raspberry Pi 5 with Raspberry Pi OS. It should also run on:

- Raspberry Pi 5, Pi 500 and Pi 4 (Pi 400) with the 64-bit Raspberry Pi OS or Ubuntu
  Desktop for Raspberry Pi. The 32-bit Raspberry Pi OS cannot run it, and neither can a
  Pi 3 or older. A Pi 5 with 8 GB or more is the most comfortable; a Pi 4 works but is slow
  on large images.
- Apple Silicon Macs running Asahi Linux (Fedora Asahi Remix has Flatpak already)
- Boards with a Rockchip RK3588 (Orange Pi 5, Radxa Rock 5 and similar) running Armbian,
  Ubuntu or Debian
- Snapdragon X laptops running Linux, and other ARM64 Linux desktops

On a Raspberry Pi with Raspberry Pi OS:

```sh
sudo apt install flatpak
sudo reboot
```

The reboot adds Flatpak apps to the menu. Then install Patchy for your user (no root needed):

```sh
flatpak install --user https://rtsoft.com/flatpak/com.rtsoft.patchy.flatpakref
```

The first install also fetches the shared KDE runtime from Flathub (about 1.5 GB of disk
space).
Patchy then appears in the menu under Graphics, starts with `flatpak run com.rtsoft.patchy`,
and updates with `flatpak update`.

To download the single-file bundle instead:

```sh
wget https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyLinuxArm64.flatpak
flatpak install --user PatchyLinuxArm64.flatpak
```

The bundle updates from the same repository afterwards.

## AppImage

`PatchyLinux.AppImage` is one file that runs without installing anything, on Ubuntu
22.04 or newer (or another distro from 2022 on: Debian 12, Fedora 36, Mint 21 and so on).
It updates itself, so keep it somewhere you can write to, such as `~/Applications`:

```sh
mkdir -p ~/Applications && cd ~/Applications
wget -O PatchyLinux.AppImage https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyLinux.AppImage
chmod +x PatchyLinux.AppImage
./PatchyLinux.AppImage
```

(`curl -LO` works instead of `wget -O PatchyLinux.AppImage`.) You can also make it
executable from your file manager (Properties > Permissions, "Allow executing file as
program") and double-click it.

The AppImage needs the FUSE 3 tools and libraries (not `libfuse2`), which most desktops
already have. If it says it cannot mount itself, install it:

| Distro | Command |
| --- | --- |
| Ubuntu, Mint, Debian, Pop!_OS | `sudo apt install fuse3` |
| Fedora | `sudo dnf install fuse3` |
| openSUSE | `sudo zypper install fuse3` |
| Arch, Manjaro | `sudo pacman -S fuse3` |

Without FUSE (some containers and minimal systems), run it with
`./PatchyLinux.AppImage --appimage-extract-and-run`.

To add Patchy to your application menu, use an AppImage integration tool:
[Gear Lever](https://flathub.org/apps/it.mijorus.gearlever) (a Flatpak app: open the
AppImage with it), [AppImageLauncher](https://github.com/TheAssassin/AppImageLauncher)
(offers to integrate an AppImage the first time you open it),
[AM](https://github.com/ivan-hc/AM) (command line: `am --launcher ./PatchyLinux.AppImage`),
or [AppImage Manager](https://github.com/tunjayoff/appimagemanager).

When a new version is out, Patchy's update prompt can download it, replace the AppImage
file and start again (if the file sits in a folder you can write to; otherwise it offers
the download). AppImage tools such as Gear Lever or AppImageUpdate can also update it.
For AI control, the connector command is the AppImage file with the argument
`patchy-mcp` (Help > Set up AI Control shows the exact line). The AppImage opens HEIC
images through your distribution's libheif (Ubuntu and Debian: the `libheif1` package,
plus `libheif-plugin-libde265` on Ubuntu 24.04 and later); the Flatpak brings its own.
