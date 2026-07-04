# Astra

Astra is a small native GTK 3 + WebKitGTK browser written in C.

It includes tabs and private windows, persistent login/session storage, installable web apps, adaptive header colors, local `file:///` browsing, developer tools, tracked downloads, history, bookmarks, and keyboard shortcuts.

## Features

- Normal and private browsing windows.
- Persistent cookies and website data under `~/.local/share/astra/webkit`.
- Settings, bookmarks, and recent history under `~/.config/astra/settings.ini`.
- Headerbar download indicator with circular progress while downloads are active.
- Download manager with progress, cancel, open, and show-in-folder actions.
- History and bookmark manager windows.
- Installable web apps with generated `.desktop` launchers and saved icons.
- WebKit developer tools / inspector through the menu, `F12`, `Ctrl+Shift+I`, or `Ctrl+Shift+J`.
- Drag-and-drop URLs, text, and local files onto the address bar, header bar, or tab labels.
- Custom `file:///` directory UI that uses GTK theme color hints.

## Dependencies

Runtime/build dependencies:

- C compiler
- Meson and Ninja
- GTK 3.24+
- WebKitGTK 4.1 by default, or WebKitGTK 4.0 with `-Dwebkitgtk_api=4.0`
- GLib/GIO, provided by the GTK/WebKitGTK dependency chain

Debian/Ubuntu:

```sh
sudo apt install build-essential meson ninja-build pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev
```

Fedora:

```sh
sudo dnf install gcc meson ninja-build pkgconf-pkg-config gtk3-devel webkit2gtk4.1-devel
```

Arch Linux:

```sh
sudo pacman -S base-devel meson ninja pkgconf gtk3 webkit2gtk-4.1
```

## Build from source

```sh
meson setup builddir --prefix=/usr/local
meson compile -C builddir
./builddir/astra
```

For older distributions that only provide WebKitGTK 4.0:

```sh
meson setup builddir -Dwebkitgtk_api=4.0
meson compile -C builddir
```

The root `Makefile` is now a convenience wrapper around Meson:

```sh
make
make run
```

A legacy non-Meson build is still available for quick local testing:

```sh
make legacy
./astra
```

## Install locally

```sh
meson setup builddir --prefix=/usr/local
meson compile -C builddir
sudo meson install -C builddir
```

Installed files include:

- `bin/astra`
- `share/applications/dev.avyos.Astra.desktop`
- `share/metainfo/dev.avyos.Astra.metainfo.xml`
- `share/icons/hicolor/scalable/apps/dev.avyos.Astra.svg`

After installation, Astra should appear in Linux application launchers as **Astra** and can register as a handler for `http://` and `https://` URLs.

## Command line

```sh
astra [URI]
astra --app https://example.com --name Example
astra --version
astra --help
```

App-mode launches are used by installed web apps. Astra handles each command-line invocation independently, so launching a normal browser window while a web app is already running opens a normal browser window, and launching a web app while Astra is already running opens an app-mode window.

## Progressive web apps

Open an `http://` or `https://` site and choose **Install site as app** from the menu. Astra writes a launcher to:

```txt
~/.local/share/applications/
```

and saves available icons under:

```txt
~/.local/share/icons/hicolor/*/apps/
```

Installed apps share Astra's persistent WebKit profile, so cookies, localStorage, IndexedDB, service-worker registrations, and HTTP/cache data are preserved between launches. Offline behavior still depends on whether the website itself implements offline support.

App-mode windows keep the headerbar minimal: back and forward buttons appear only when the current web app actually has backward or forward history available.

## Downloads, history, and bookmarks

- **Downloads**: menu item or `Ctrl+J`.
- **History**: menu item or `Ctrl+H`.
- **Bookmarks**: menu item or `Ctrl+Shift+O`.

Downloads are saved to the user's Downloads folder and duplicate filenames are numbered automatically.

## Source layout

- `src/astra.c` — application entry point and command-line handling
- `src/app.c` — persistent configuration, history, and bookmarks
- `src/browser.c` — browser windows, tabs, navigation, app mode, and WebKit integration
- `src/downloads.c` — WebKit download handling and downloads manager UI
- `src/file_page.c` — custom local-folder UI for `file:///` pages
- `src/library.c` — native history/bookmark list windows
- `src/pwa.c` — installable web-app launchers and icon support
- `src/service_scheme.c` — internal `service://` resource/action dispatcher
- `src/settings.c` — browser settings page
- `data/` — desktop entry, AppStream metadata, and application icon
- `meson/` — Meson install helper scripts
- `packaging/` — distro packaging notes

## Packaging notes

Use Meson for packages:

```sh
meson setup builddir --prefix=/usr --buildtype=plain
meson compile -C builddir
DESTDIR="$pkgdir" meson install -C builddir
```

Before public distribution, choose and declare the real project license. The Meson project and AppStream metadata currently use `NOASSERTION` instead of inventing a license.
