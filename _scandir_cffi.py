"""scandir, a better directory iterator that exposes all file info OS provides

scandir is released under the new BSD 3-clause license. See LICENSE.txt for
the full license text.

This is the backend implemented with cffi.
"""

from __future__ import division

import cffi
import os
import sys

from _scandir_common import DirEntry, Dirent

__version__ = '0.1'
__all__ = ['scandir']

try:
    unicode
except NameError:
    unicode = str

scandir = None

# Linux, OS X, and BSD implementation
if sys.platform.startswith(('linux', 'darwin')) or 'bsd' in sys.platform:
    
    dirent_h = """
typedef uint64_t __ino_t;
typedef int64_t __ino64_t;

struct dirent
  {
    __ino_t d_ino;
    unsigned char d_type;
    char d_name[...];
    ...;
  };
  
#define DT_UNKNOWN ...
#define DT_FIFO ...
#define DT_CHR ...
#define DT_DIR ...
#define DT_BLK ...
#define DT_REG ...
#define DT_LNK ...
#define DT_SOCK ...
#define DT_WHT ...

typedef ... DIR;

extern DIR *opendir (const char *__name);

extern int closedir (DIR *__dirp);

extern int readdir_r (DIR * __dirp,
        struct dirent * __entry,
        struct dirent ** __result);

extern int scandir (const char * __dir,
      struct dirent *** __namelist,
      int (*__selector) (const struct dirent *),
      int (*__cmp) (const struct dirent **,
      const struct dirent **));

    """
    
    ffi = cffi.FFI()
    ffi.cdef(dirent_h)
    libc = ffi.verify("#include <dirent.h>")

    # Rather annoying how the dirent struct is slightly different on each
    # platform. The only fields we care about are d_name and d_type.
    def dirent():
        return ffi.new(dirent_p)

    for attr in dir(libc):
        if attr.startswith("DT_"):
            globals()[attr] = getattr(libc, attr)
    
    dirent_p = ffi.typeof("struct dirent *")
    dirent_pp = ffi.typeof("struct dirent **")

    opendir = libc.opendir

    readdir_r = libc.readdir_r

    closedir = libc.closedir

    file_system_encoding = sys.getfilesystemencoding()

    def posix_error(filename):
        errno = ffi.errno
        exc = OSError(errno, os.strerror(errno))
        exc.filename = filename
        return exc

    def scandir(path='.'):
        dir_p = opendir(path.encode(file_system_encoding))
        if not dir_p:
            raise posix_error(path)
        try:
            entry = ffi.new(dirent_p)
            result = ffi.new(dirent_pp)
            while True:
                rc = readdir_r(dir_p, entry, result)
                if rc:
                    raise posix_error(path)
                if not result[0]:
                    break
                name = ffi.string(entry.d_name).decode(file_system_encoding)
                if name not in ('.', '..'):
                    scandir_dirent = Dirent(entry.d_ino, entry.d_type)
                    yield DirEntry(path, name, scandir_dirent, None)
        finally:
            if closedir(dir_p):
                raise posix_error(path)

# Some other system -- no d_type or stat information
else:
    def scandir(path='.'):
        for name in os.listdir(path):
            yield DirEntry(path, name, None, None)

