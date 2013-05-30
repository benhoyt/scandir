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
import os
import stat
import sys
import warnings

__version__ = '0.1'

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

Dirent = collections.namedtuple('Dirent', ['d_ino', 'd_type'])

class DirEntry(object):
    __slots__ = ('_path', 'name', 'dirent', '_lstat')

    def __init__(self, path, name, dirent, lstat):
        # TODO ben: make path absolute? Do this in scandir?
        self._path = path
        self.name = name
        self.dirent = dirent
        self._lstat = lstat

    def lstat(self):
        if self._lstat is None:
            self._lstat = _lstat(_join(self._path, self.name))
        return self._lstat

    # Ridiculous duplication between these is* functions -- helps a little bit
    # with os.walk() performance compared to calling another function. Won't
    # be an issue in C, but Python function calls are relatively expensive.
    def isdir(self):
        if self._lstat is None:
            d_type = getattr(self.dirent, 'd_type', DT_UNKNOWN)
            if d_type != DT_UNKNOWN:
                return d_type == DT_DIR
            else:
                try:
                    self.lstat()
                except OSError:
                    return False
        return self._lstat.st_mode & 0o170000 == S_IFDIR

    def isfile(self):
        if self._lstat is None:
            d_type = getattr(self.dirent, 'd_type', DT_UNKNOWN)
            if d_type != DT_UNKNOWN:
                return d_type == DT_REG
            else:
                try:
                    self.lstat()
                except OSError:
                    return False
        return self._lstat.st_mode & 0o170000 == S_IFREG

    def islink(self):
        if self._lstat is None:
            d_type = getattr(self.dirent, 'd_type', DT_UNKNOWN)
            if d_type != DT_UNKNOWN:
                return d_type == DT_LNK
            else:
                try:
                    self.lstat()
                except OSError:
                    return False
        return self._lstat.st_mode & 0o170000 == S_IFLNK

    def __str__(self):
        return '<{0}: {1!r}{2}{3}>'.format(
                self.__class__.__name__,
                self.name,
                ' dirent' if self.dirent else '',
                ' stat' if self._lstat else '')

    __repr__ = __str__

