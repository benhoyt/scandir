import copy
import fts
import os
import pathlib
import scandir
import stat

class ScandirDirEntry:
    def __init__(self, entry):
        self._entry = entry
    @property
    def name(self):
        return self._entry.name
    def lstat(self):
        return self._entry.lstat()
    def is_dir(self):
        return self._entry.is_dir()
    def is_file(self):
        return self._entry.is_file()
    def is_symlink(self):
        return self._entry.is_symlink()
    def __str__(self):
        return str(self._entry)
    __repr__ = __str__
    
def fts_scandir(path, *, logical=False, nochdir=False,
                nostat=False, seedot=False, xdev=False,
                _level=0):
    for entry in scandir.scandir(path):
        direntry = ScandirDirEntry(entry)
        direntry.error = None
        direntry.level = _level
        direntry.postorder = False
        direntry.skip = False
        if not nostat:
            try:
                direntry.lstat()
            except OSError as e:
                direntry.error = e
        if direntry.is_dir():
            yield direntry
            if not direntry.skip:
                yield from fts_scandir(os.path.join(path, direntry.name),
                                       logical=logical, nochdir=nochdir,
                                       nostat=nostat, seedot=seedot, xdev=xdev,
                                       _level=_level+1)
            direntry = copy.copy(direntry)
            direntry.postorder = True
            yield direntry
        else:
            yield direntry

class FtsDirEntry:
    def __init__(self, entry):
        self._entry = entry
        self._path = pathlib.Path(entry.accpath)
        self.skip = False
    @property
    def name(self):
        return self._entry.accpath
    @property
    def level(self):
        return self._entry.level
    @property
    def error(self):
        return self._entry.error
    @property
    def postorder(self):
        return self._entry.info == fts.Info.DP
    def stat(self):
        if self._entry.info.has_stat():
            return self._entry.stat or self._path.stat()
        return self._path.stat()
    def lstat(self):
        if self._entry.info == fts.Info.SL:
            return self._entry.stat or self._path.lstat()
        return self._path.lstat()
    def is_dir(self):
        if self._entry.info.is_dir():
            return True
        if self._entry.info != fts.Info.SL:
            return False
        return stat.ISDIR(self.lstat().st_mode)
    def is_file(self):
        if self._entry.is_file():
            return True
        if self._entry.info != fts.Info.SL:
            return False
        return stat.ISREG(self.lstat().st_mode)
    def is_symlink(self):
        return self._entry.is_symlink()
    def __str__(self):
        return '<{0}: {1!r}>'.format(self.__class__.__name__, self.name)
    __repr__ = __str__
            
def fts_fts(path, *, logical=False, nochdir=False,
            nostat=False, seedot=False, xdev=False):
    options = fts.Options.COMFOLLOW
    if logical: options |= fts.Options.LOGICAL
    if nochdir: options |= fts.Options.NOCHDIR
    if nostat: options |= fts.Options.NOSTAT
    if seedot: options |= fts.Options.SEEDOT
    if xdev: options |= fts.Options.XDEV
    with fts.FTS(path, options=options) as f:
        for entry in f:
            direntry = FtsDirEntry(entry)
            yield direntry
            if direntry.skip:
                f.skip(entry)

if __name__ == '__main__':
    import sys
    args = sys.argv[1:] or ['.']
    def test(f):
        for entry in f:
            if entry.name.startswith('_'):
                entry.skip = True
            print(entry.level, entry)            
    for arg in args:
        test(fts_scandir(arg))
        test(fts_fts(arg))
            
