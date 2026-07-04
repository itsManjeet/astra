#!/usr/bin/env python3
import os
import shutil
import subprocess

prefix = os.environ.get('MESON_INSTALL_DESTDIR_PREFIX') or os.environ.get('MESON_INSTALL_PREFIX') or ''
datadir = os.path.join(prefix, 'share')


def run_if_available(tool, *args):
    exe = shutil.which(tool)
    if not exe:
        return
    subprocess.run([exe, *args], check=False)


run_if_available('gtk-update-icon-cache', '-qtf', os.path.join(datadir, 'icons', 'hicolor'))
run_if_available('update-desktop-database', os.path.join(datadir, 'applications'))
