"""scandir, a better directory iterator that exposes all file info OS provides

scandir is a generator version of os.listdir() that returns an iterator over
files in a directory, and also exposes the extra information most OSes provide
while iterating files in a directory.

See README.md or https://github.com/benhoyt/scandir for rationale and docs.

scandir is released under the new BSD 3-clause license. See LICENSE.txt for
the full license text.
"""

from __future__ import division

import collections
import ctypes
import os
import stat
import sys
import warnings

from _scandir_common import DirEntry, Dirent

__version__ = '0.1'
__all__ = ['scandir', 'walk']

try:
    unicode
except NameError:
    unicode = str

_join = os.path.join
_lstat = os.lstat
_stat_result = os.stat_result

DT_UNKNOWN = 0
DT_FIFO = 1
DT_CHR = 2
DT_DIR = 4
DT_BLK = 6
DT_REG = 8
DT_LNK = 10
DT_SOCK = 12

S_IFDIR = stat.S_IFDIR
S_IFREG = stat.S_IFREG
S_IFLNK = stat.S_IFLNK

if sys.platform == 'win32':
    from _scandir_ctypes import scandir

    try:
        import _scandir
        def scandir(path='.'):
            for name, st in _scandir.scandir_helper(unicode(path)):
                yield DirEntry(path, name, None, st)
    except ImportError:
        warnings.warn('Using slow Python version of scandir()')


# Linux, OS X, and BSD implementation
elif sys.platform.startswith(('linux', 'darwin')) or 'bsd' in sys.platform:
    try:
        from _scandir_cffi import scandir, opendir, closedir
    except ImportError:
        from _scandir_ctypes import scandir, opendir, closedir

    if not '__pypy__' in sys.builtin_module_names:
        try:
            import _scandir
            def scandir(path='.'):
                for name, d_ino, d_type in _scandir.scandir_helper(path):
                    scandir_dirent = Dirent(d_ino, d_type)
                    yield DirEntry(path, name, scandir_dirent, None)
        except ImportError:
            warnings.warn('Using slow Python version of scandir(), please build _scandir.c using setup.py')

# Some other system -- no d_type or stat information
else:
    def scandir(path='.'):
        for name in os.listdir(path):
            yield DirEntry(path, name, None, None)


def walk(top, topdown=True, onerror=None, followlinks=False):
    """Just like os.walk(), but faster, as it uses scandir() internally."""
    # Determine which are files and which are directories
    dirs = []
    nondirs = []
    try:
        for entry in scandir(top):
            if entry.isdir():
                dirs.append(entry)
            else:
                nondirs.append(entry)
    except OSError as error:
        if onerror is not None:
            onerror(error)
        return

    # Yield before recursion if going top down
    if topdown:
        # Need to do some fancy footwork here as caller is allowed to modify
        # dir_names, and we really want them to modify dirs (list of DirEntry
        # objects) instead. Keep a mapping of entries keyed by name.
        dir_names = []
        entries_by_name = {}
        for entry in dirs:
            dir_names.append(entry.name)
            entries_by_name[entry.name] = entry

        yield top, dir_names, [e.name for e in nondirs]

        dirs = []
        for dir_name in dir_names:
            entry = entries_by_name.get(dir_name)
            if entry is None:
                entry = DirEntry(top, dir_name, None, None)
            dirs.append(entry)

    # Recurse into sub-directories, following symbolic links if "followlinks"
    for entry in dirs:
        if followlinks or not entry.islink():
            new_path = _join(top, entry.name)
            for x in walk(new_path, topdown, onerror, followlinks):
                yield x

    # Yield before recursion if going bottom up
    if not topdown:
        yield top, [e.name for e in dirs], [e.name for e in nondirs]
