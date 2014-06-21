import collections
from cffi import FFI
from enum import IntEnum
import os
import sys

__all__ = ['Options', 'Info', 'Entry', 'FTS']

def _cmp(x, y):
    if x < y:
        return -1
    elif y < x:
        return 1
    else:
        return 0

def key_to_cmp(key, reverse=False):
    if key:
        if reverse:
            def cmp(x, y):
                return _cmp(key(y), key(x))
        else:
            def cmp(x, y):
                return _cmp(key(x), key(y))
    else:
        if reverse:
            def cmp(x, y):
                return _cmp(y, x)
        else:
            def cmp(x, y):
                return _cmp(x, y)
    return cmp

class Options(IntEnum):
    COMFOLLOW   = 0x001 # follow command line symlinks
    LOGICAL     = 0x002 # logical walk
    NOCHDIR     = 0x004 # don't change directories
    NOSTAT      = 0x008 # don't get stat info
    PHYSICAL    = 0x010 # physical walk
    SEEDOT      = 0x020 # return dot and dot-dot
    XDEV        = 0x040 # don't cross devices
    WHITEOUT    = 0x080 # return whiteout information
    COMFOLLOWDIR= 0x400 # (non-std) follow command line symlinks for directories only
    OPTIONMASK  = 0x4ff # valid user option mask

    NAMEONLY    = 0x100 # (private) child names only
    STOP        = 0x200

class SetOptions(IntEnum):
    AGAIN       = 1
    FOLLOW      = 2
    NOINSTR     = 3
    SKIP        = 4

class Info(IntEnum):
    D       =  1 # preorder directory
    DC      =  2 # directory that causes cycles
    DEFAULT =  3 # none of the above
    DNR     =  4 # unreadable directory
    DOT     =  5 # dot or dot-dot
    DP      =  6 # postorder directory
    ERR     =  7 # error; errno is set
    F       =  8 # regular file
    INIT    =  9 # initialized only
    NS      = 10 # stat(2) failed
    NSOK    = 11 # no stat(2) requested
    SL      = 12 # symbolic link
    SLNONE  = 13 # symbolic link without target
    W       = 14 # whiteout object
    def is_dir(self):
        return self in (Info.D, Info.DC, Info.DNR, Info.DOT, Info.DP)
    def is_file(self):
        return self == Info.F
    def is_symlink(self):
        return self in (Info.SL, Info.SLNONE)
    def is_error(self):
        return self in (Info.DNR, Info.ERR, Info.NS)
    def has_stat(self):
        return self not in (Info.DNR, Info.ERR, Info.NS, Info.NSOK)

def get_sizes():
    types = ('dev_t', 'mode_t', 'nlink_t', 'ino_t', 'uid_t', 'gid_t',
             'off_t', 'blkcnt_t', 'blksize_t', 'time_t')
    ffi = FFI()
    ffi.cdef(''.join('#define SIZE_OF_{} ...\n'.format(t) for t in types))
    lib = ffi.verify('#include <sys/types.h>\n' +
                     ''.join('#define SIZE_OF_{} sizeof({})\n'.format(t, t)
                             for t in types))
    return ''.join(
        'typedef uint{}_t {};\n'.format(getattr(lib, 'SIZE_OF_'+t)*8, t)
        for t in types)

ffi = FFI()
ffi.cdef(get_sizes() + """
    typedef unsigned short u_short;
    struct timespec {
        time_t tv_sec;
        long tv_nsec;
        ...;
    };
    /* Note that POSIX does not require the timespec fields, or even mention
       them beyond saying "The timespec structure may be defined as described
       in <time.h>." However, both BSD (including OS X) and linux define them
       like this. */
    struct stat {
        dev_t           st_dev;           /* ID of device containing file */
        mode_t          st_mode;          /* Mode of file (see below) */
        nlink_t         st_nlink;         /* Number of hard links */
        ino_t           st_ino;           /* File serial number */
        uid_t           st_uid;           /* User ID of the file */
        gid_t           st_gid;           /* Group ID of the file */
        dev_t           st_rdev;          /* Device ID */
        struct timespec st_atimespec;     /* time of last access */
        struct timespec st_mtimespec;     /* time of last data modification */
        struct timespec st_ctimespec;     /* time of last status change */
        struct timespec st_birthtimespec; /* time of file creation(birth) */
        off_t           st_size;          /* file size, in bytes */
        blkcnt_t        st_blocks;        /* blocks allocated for file */
        blksize_t       st_blksize;       /* optimal blocksize for I/O */
    };     

    typedef ... FTS;
     
    typedef struct _ftsent {
            u_short fts_info;               /* flags for FTSENT structure */
            char *fts_accpath;              /* access path */
            char *fts_path;                 /* root path */
            u_short fts_pathlen;            /* strlen(fts_path) */
            char fts_name[];                /* file name */
            u_short fts_namelen;            /* strlen(fts_name) */
            short fts_level;                /* depth (-1 to N) */
            int fts_errno;                  /* file errno */
            long fts_number;                /* local numeric value */
            void *fts_pointer;              /* local address value */
            struct ftsent *fts_parent;      /* parent directory */
            struct ftsent *fts_link;        /* next file structure */
            struct ftsent *fts_cycle;       /* cycle structure */
            struct stat *fts_statp;         /* stat(2) information */
            ...;
    } FTSENT;

    FTS *
    fts_open(char * const *path_argv, int options,
        int (*compar)(const FTSENT **, const FTSENT **));

    FTSENT *
    fts_read(FTS *ftsp);

    FTSENT *
    fts_children(FTS *ftsp, int options);

    int
    fts_set(FTS *ftsp, FTSENT *f, int options);

    int
    fts_close(FTS *ftsp);     
""")

libc = ffi.verify("""
#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>
""")

def _make_error(*args, code=None):
    if code is None:
        code = ffi.errno
    return OSError(code, os.strerror(code), *args)

def _make_path(ent):
    if isinstance(ent, Entry):
        ent = ent.ftsent
    if ent.fts_path:
        return ffi.string(ent.fts_path).decode(sys.getfilesystemencoding())
    else:
        return ffi.string(ent.fts_name).decode(sys.getfilesystemencoding())

class Entry:
    __slots__ = ('ftsent', '_nostat', '_stat')
    def __init__(self, ftsent, nostat=False):
        self.ftsent = ftsent
        self._nostat = nostat
        self._stat = None
    @property
    def info(self):
        return Info(self.ftsent.fts_info)
    @staticmethod
    def _stringify(s):
        return ffi.string(s).decode(sys.getfilesystemencoding()) if s else '<NULL>'
    @property
    def accpath(self):
        return self._stringify(self.ftsent.fts_accpath)
    @property
    def path(self):
        return self._stringify(self.ftsent.fts_path)
    @property
    def name(self):
        return self._stringify(self.ftsent.fts_name)
    @property
    def level(self):
        return self.ftsent.fts_level
    @property
    def errno(self):
        return self.ftsent.fts_errno
    @property
    def stat(self):
        if self._nostat:
            return None
        if self._stat is None:
            if not self.info.has_stat():
                self._nostat = True
                return None
            def dts(ts):
                return ts.tv_sec + ts.tv_nsec / 1000000000
            def dtsns(ts):
                return ts.tv_sec * 1000000000 + ts.tv_nsec
            # Note: This works in CPython 3.4 on platforms that have all of
            # the relevant fields (which includes BSD/OS X and Linux).
            statp = self.ftsent.fts_statp
            self._stat = os.stat_result((statp.st_mode, statp.st_ino, statp.st_dev,
                                         statp.st_nlink, statp.st_uid, statp.st_gid,
                                         statp.st_size,
                                         dts(statp.st_atimespec),
                                         dts(statp.st_mtimespec),
                                         dts(statp.st_ctimespec),
                                         int(dts(statp.st_atimespec)),
                                         int(dts(statp.st_mtimespec)),
                                         int(dts(statp.st_ctimespec)),
                                         dtsns(statp.st_atimespec),
                                         dtsns(statp.st_mtimespec),
                                         dtsns(statp.st_ctimespec),
                                         statp.st_blksize, statp.st_blocks,
                                         0, 0, 0, # rdev, flags, gen
                                         dts(statp.st_birthtimespec)))
        return self._stat

class FTS:
    def __init__(self, *paths, options=None, key=None, reverse=False):
        """FTS(path, options=None, key=None, reverse=False)

        Begins an FTS traversal on the specified paths, iterating the
        filesystem tree in depth-first order, yielding Entry structures.
        Note that by default, unlike os.walk, directories are yielded twice,
        both before and after their contents. (See Info for details.)

        The paths can be either strings (in which case they're assumed to
        be in the default file system encoding) or bytes.

        The options are a bitmask of Options values, defaulting to
        PHYSICAL if nothing is passed.
        
        If a key function is given, it will be called on pairs of Entry
        structures; otherwise, the traversal order is the order of the
        paths, and the native directory entry order within each path.
        Note that accpath and path cannot be used (but name can), and
        stat can only be used if stat was called (info is not NS or NSOK).         
        The key objects only need to define <, and it is legal for two
        objects to be incomparable (that is, not x<y and not y<x).

        See your platform's fts(3) documentation for more details."""

        self._fts = None
        enc = sys.getfilesystemencoding()
        def encpath(path):
            try:
                return path.encode(enc)
            except AttributeError:
                return path
        def decpath(path):
            try:
                return path.decode(enc)
            except AttributeError:
                return path
        path_cstrs = [ffi.new("char[]", encpath(path)) for path in paths]
        path_cstrs.append(ffi.NULL)
        path_argv = ffi.new("char *[]", path_cstrs)
        self.paths = [decpath(path) for path in paths]
        self.options = options
        if options is None:
            options = 0
        if not options | Options.PHYSICAL | Options.LOGICAL:
            options |= Options.PHYSICAL
        if key:
            compar = ffi.callback("int(FTSENT **, FTSENT**)",
                                  key_to_cmp(key, reverse))
        else:
            compar = ffi.NULL
        self._fts = libc.fts_open(path_argv, options, compar)
        if not self._fts:
            raise _make_error(':'.join(self.paths))
    def close(self):
        if self._fts:
            libc.fts_close(self._fts)
            self._fts = None
    def __del__(self):
        self.close()
    def __enter__(self):
        return self
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False
    def __iter__(self):
        return self
    def __next__(self):
        ent = libc.fts_read(self._fts)
        if not ent:
            if ffi.errno:
                raise _make_error(':'.join(self.paths))
            else:
                raise StopIteration
        return Entry(ent, self.options & Options.NOSTAT)
    #def children(self):
    #    ent = children(self._fts), walk the links and return a sequence
    def set(self, entry, option):
        if isinstance(entry, Entry):
            entry = entry.ftsent
        if libc.fts_set(self._fts, entry, option):
            raise _make_error(_make_path(entry))
    def again(self, ent):
        self.set(ent, SetOptions.AGAIN)
    def follow(self, ent):
        self.set(ent, SetOptions.FOLLOW)
    def skip(self, ent):
        self.set(ent, SetOptions.SKIP)

if __name__ == '__main__':
    args = sys.argv[1:] if len(sys.argv) > 1 else '.'

    with FTS(*args, options=Options.COMFOLLOW | Options.NOSTAT) as f:
        for e in f:
            #if e.info==Info.D and e.name.startswith('_'):
            #    f.skip(e)
            #elif e.info==Info.SL:
            #    f.follow(e)
            if e.info.is_error():
                path = _make_error(_make_path(e), code=e.errno)
            else:
                path = e.path
            print(e.info.name.ljust(7), e.level, path)
