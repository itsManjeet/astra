# Packaging notes

Astra is now installable with Meson:

```sh
meson setup builddir --prefix=/usr
meson compile -C builddir
DESTDIR="$pkgdir" meson install -C builddir
```

Installed files:

- `/usr/bin/astra`
- `/usr/share/applications/dev.avyos.Astra.desktop`
- `/usr/share/metainfo/dev.avyos.Astra.metainfo.xml`
- `/usr/share/icons/hicolor/scalable/apps/dev.avyos.Astra.svg`

Before publishing distro packages, set a real project license in `meson.build`
and `data/dev.avyos.Astra.metainfo.xml.in`.
