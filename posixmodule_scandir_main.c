/*
Ben's notes:

TODO:
  - factor out close and call closedir/FindClose also when there's an
    error half way through iteration
  - open bug on listdir('a\0b') issue
  - change repr to include .name and id()
  - add something to docs that DirEntry is not pickleable (near the
    bit that says it's not intended to be stored in long-term data
    structures)
  - ensure we have a test that DirEntry isn't pickleable
  - ensure we have tests for all cases of is_dir/is_file/is_symlink
    with a file, dir, symlink to file, symlink to dir
  - ensure we have a test that tests consuming iterator twice
  - speed test of parsing follow_symlinks keyword param in is_dir/is_file

* haypo's suggestions:
  - new tests
  - add test to check that invalid types are rejected, and
    a test to ensure that DirEntry.stat() and is_dir/is_file
    parameters are keyword-only parameters
  - documentation, Victor's or mine or combination of both?

* three files should be #included in posixmodule.c
  - posixmodule_scandir_main.c (this file) after posix_set_blocking
  - posixmodule_scandir_methods.c at end of posix_methods (before Sentinel)
  - posixmodule_scandir_init.c after "PyStructSequence_InitType2(&TerminalSizeType"

* something I noticed while looking at _listdir_windows_no_opendir():
  - "bufptr" is not used
  - initial Py_ARRAY_LENGTH(namebuf)-4 value of "len" is not used
  - "po" is not used

*/

#include "structmember.h"

PyDoc_STRVAR(posix_scandir__doc__,
"scandir(path='.') -> iterator of DirEntry objects for given path");

static char *follow_symlinks_keywords[] = {"follow_symlinks", NULL};

typedef struct {
    PyObject_HEAD
    PyObject *name;
    PyObject *path;
    PyObject *stat;
    PyObject *lstat;
#ifdef MS_WINDOWS
    struct _Py_stat_struct win32_lstat;
    __int64 win32_file_index;
    int got_file_index;
#else /* POSIX */
    unsigned char d_type;
    ino_t d_ino;
#endif
} DirEntry;

typedef struct {
    PyObject_HEAD
    path_t path;
#ifdef MS_WINDOWS
    HANDLE handle;
    WIN32_FIND_DATAW file_data;
    int first_time;
#else /* POSIX */
    DIR *dirp;
#endif
} ScandirIterator;

static void
DirEntry_dealloc(DirEntry *entry)
{
    Py_XDECREF(entry->name);
    Py_XDECREF(entry->path);
    Py_XDECREF(entry->stat);
    Py_XDECREF(entry->lstat);
    Py_TYPE(entry)->tp_free((PyObject *)entry);
}

/* Forward reference to make it easier to implement DirEntry_is_symlink etc */
static PyObject *
DirEntry_test_mode(DirEntry *self, int follow_symlinks, unsigned short mode_bits);

static PyObject *
DirEntry_is_symlink(DirEntry *self)
{
#ifdef MS_WINDOWS
    return PyBool_FromLong((self->win32_lstat.st_mode & S_IFMT) == S_IFLNK);
#else /* POSIX */
    if (self->d_type != DT_UNKNOWN) {
        return PyBool_FromLong(self->d_type == DT_LNK);
    }
    else {
        return DirEntry_test_mode(self, 0, S_IFLNK);
    }
#endif
}

#ifndef MS_WINDOWS /* POSIX */
static PyObject *
DirEntry_fetch_stat(DirEntry *self, int follow_symlinks)
{
    PyObject *result;
    path_t path = PATH_T_INITIALIZE("DirEntry.stat", NULL, 0, 0);

    if (!path_converter(self->path, &path)) {
        return NULL;
    }
    result = posix_do_stat("DirEntry.stat", &path, DEFAULT_DIR_FD, follow_symlinks);
    path_cleanup(&path);
    return result;
}
#endif

static PyObject *
DirEntry_get_lstat(DirEntry *self)
{
    if (!self->lstat) {
#ifdef MS_WINDOWS
        self->lstat = _pystat_fromstructstat(&self->win32_lstat);
#else /* POSIX */
        self->lstat = DirEntry_fetch_stat(self, 0);
#endif
    }
    Py_XINCREF(self->lstat);
    return self->lstat;
}

static PyObject *
DirEntry_get_stat(DirEntry *self, int follow_symlinks)
{
    if (follow_symlinks) {
        if (!self->stat) {
#ifdef MS_WINDOWS
            if ((self->win32_lstat.st_mode & S_IFMT) == S_IFLNK) {
                path_t path = PATH_T_INITIALIZE("DirEntry.stat", NULL, 0, 0);

                if (!path_converter(self->path, &path)) {
                    return NULL;
                }
                self->stat = posix_do_stat("DirEntry.stat", &path, DEFAULT_DIR_FD, 1);
                path_cleanup(&path);
            }
            else {
                self->stat = DirEntry_get_lstat(self);
            }
#else /* POSIX */
            int is_symlink;
            PyObject *po_is_symlink = DirEntry_is_symlink(self);
            if (!po_is_symlink) {
                return NULL;
            }
            is_symlink = PyObject_IsTrue(po_is_symlink);
            Py_DECREF(po_is_symlink);

            if (is_symlink) {
                self->stat = DirEntry_fetch_stat(self, 1);
            }
            else {
                self->stat = DirEntry_get_lstat(self);
            }
#endif
        }
        Py_XINCREF(self->stat);
        return self->stat;
    }
    else {
        return DirEntry_get_lstat(self);
    }
}

static PyObject *
DirEntry_stat(DirEntry *self, PyObject *args, PyObject *kwargs)
{
    int follow_symlinks = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$p:DirEntry.stat",
                                     follow_symlinks_keywords,
                                     &follow_symlinks)) {
        return NULL;
    }

    return DirEntry_get_stat(self, follow_symlinks);
}

static PyObject *
DirEntry_test_mode(DirEntry *self, int follow_symlinks, unsigned short mode_bits)
{
    PyObject *stat = NULL;
    PyObject *st_mode = NULL;
    int mode;
    int result = 0;
    int is_symlink;
    int need_stat;
    unsigned long dir_bits;

#ifdef MS_WINDOWS
    is_symlink = (self->win32_lstat.st_mode & S_IFMT) == S_IFLNK;
    need_stat = follow_symlinks && is_symlink;
#else /* POSIX */
    is_symlink = self->d_type == DT_LNK;
    need_stat = self->d_type == DT_UNKNOWN || (follow_symlinks && is_symlink);
#endif
    if (need_stat) {
        stat = DirEntry_get_stat(self, follow_symlinks);
        if (!stat) {
            if (PyErr_ExceptionMatches(PyExc_OSError) && errno == ENOENT) {
                /* If file doesn't exist (anymore), then return False
                   (say it's not a directory) */
                PyErr_Clear();
                Py_RETURN_FALSE;
            }
            goto error;
        }
        st_mode = PyObject_GetAttrString(stat, "st_mode");
        if (!st_mode) {
            goto error;
        }

        mode = PyLong_AsLong(st_mode);
        if (mode == -1 && PyErr_Occurred()) {
            goto error;
        }
        Py_DECREF(st_mode);
        Py_DECREF(stat);
        result = (mode & S_IFMT) == mode_bits;
    }
    else if (is_symlink) {
        assert(mode_bits != S_IFLNK);
        result = 0;
    }
    else {
        assert(mode_bits == S_IFDIR || mode_bits == S_IFREG);
#ifdef MS_WINDOWS
        dir_bits = self->win32_lstat.st_file_attributes & FILE_ATTRIBUTE_DIRECTORY;
        if (mode_bits == S_IFDIR) {
            result = dir_bits != 0;
        }
        else {
            result = dir_bits == 0;
        }
#else /* POSIX */
        if (mode_bits == S_IFDIR) {
            result = self->d_type == DT_DIR;
        }
        else {
            result = self->d_type == DT_REG;
        }
#endif
    }

    return PyBool_FromLong(result);

error:
    Py_XDECREF(st_mode);
    Py_XDECREF(stat);
    return NULL;
}

static PyObject *
DirEntry_is_dir(DirEntry *self, PyObject *args, PyObject *kwargs)
{
    int follow_symlinks = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$p:DirEntry.is_dir",
                                     follow_symlinks_keywords,
                                     &follow_symlinks)) {
        return NULL;
    }

    return DirEntry_test_mode(self, follow_symlinks, S_IFDIR);
}

static PyObject *
DirEntry_is_file(DirEntry *self, PyObject *args, PyObject *kwargs)
{
    int follow_symlinks = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$p:DirEntry.is_file",
                                     follow_symlinks_keywords,
                                     &follow_symlinks)) {
        return NULL;
    }

    return DirEntry_test_mode(self, follow_symlinks, S_IFREG);
}

static PyObject *
DirEntry_inode(DirEntry *self)
{
#ifdef MS_WINDOWS
    if (!self->got_file_index) {
        path_t path = PATH_T_INITIALIZE("DirEntry.inode", NULL, 0, 0);
        struct _Py_stat_struct stat;

        if (!path_converter(self->path, &path)) {
            return NULL;
        }

        if (win32_lstat_w(path.wide, &stat) != 0) {
            path_error(&path);
            path_cleanup(&path);
            return NULL;
        }
        path_cleanup(&path);
        self->win32_file_index = stat.st_ino;
        self->got_file_index = 1;
    }
    return PyLong_FromLongLong((PY_LONG_LONG)self->win32_file_index);
#else /* POSIX */
#ifdef HAVE_LARGEFILE_SUPPORT
    return PyLong_FromLongLong((PY_LONG_LONG)self->d_ino);
#else
    return PyLong_FromLong((long)self->d_ino);
#endif
#endif
}

static PyMemberDef DirEntry_members[] = {
    {"name", T_OBJECT_EX, offsetof(DirEntry, name), READONLY,
     "the entry's base filename, relative to scandir() \"path\" argument"},
    {"path", T_OBJECT_EX, offsetof(DirEntry, path), READONLY,
     "the entry's full path name; equivalent to os.path.join(scandir_path, entry.name)"},
    {NULL}
};

static PyMethodDef DirEntry_methods[] = {
    {"is_dir", (PyCFunction)DirEntry_is_dir, METH_VARARGS | METH_KEYWORDS,
     "return True if the entry is a directory; cached per entry"
    },
    {"is_file", (PyCFunction)DirEntry_is_file, METH_VARARGS | METH_KEYWORDS,
     "return True if the entry is a file; cached per entry"
    },
    {"is_symlink", (PyCFunction)DirEntry_is_symlink, METH_NOARGS,
     "return True if the entry is a symbolic link; cached per entry"
    },
    {"stat", (PyCFunction)DirEntry_stat, METH_VARARGS | METH_KEYWORDS,
     "return stat_result object for the entry; cached per entry"
    },
    {"inode", (PyCFunction)DirEntry_inode, METH_NOARGS,
     "return inode of the entry; cached per entry",
    },
    {NULL}
};

PyTypeObject DirEntryType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    MODNAME ".DirEntry",                    /* tp_name */
    sizeof(DirEntry),                       /* tp_basicsize */
    0,                                      /* tp_itemsize */
    /* methods */
    (destructor)DirEntry_dealloc,           /* tp_dealloc */
    0,                                      /* tp_print */
    0,                                      /* tp_getattr */
    0,                                      /* tp_setattr */
    0,                                      /* tp_compare */
    0,                                      /* tp_repr */
    0,                                      /* tp_as_number */
    0,                                      /* tp_as_sequence */
    0,                                      /* tp_as_mapping */
    0,                                      /* tp_hash */
    0,                                      /* tp_call */
    0,                                      /* tp_str */
    0,                                      /* tp_getattro */
    0,                                      /* tp_setattro */
    0,                                      /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                     /* tp_flags */
    0,                                      /* tp_doc */
    0,                                      /* tp_traverse */
    0,                                      /* tp_clear */
    0,                                      /* tp_richcompare */
    0,                                      /* tp_weaklistoffset */
    0,                                      /* tp_iter */
    0,                                      /* tp_iternext */
    DirEntry_methods,                       /* tp_methods */
    DirEntry_members,                       /* tp_members */
};

#ifdef MS_WINDOWS

static wchar_t *
join_path_filenameW(wchar_t *path_wide, wchar_t* filename)
{
    Py_ssize_t path_len;
    wchar_t *result;
    wchar_t ch;

    if (!path_wide) { /* Default arg: "." */
        path_wide = L".";
        path_len = 1;
    }
    else {
        path_len = wcslen(path_wide);
    }

    /* The +1's are for the path separator and the NUL */
    result = PyMem_Malloc((path_len + 1 + wcslen(filename) + 1) * sizeof(wchar_t));
    if (!result) {
        PyErr_NoMemory();
        return NULL;
    }
    wcscpy(result, path_wide);
    ch = result[path_len - 1];
    if (ch != SEP && ch != ALTSEP && ch != L':') {
        result[path_len++] = SEP;
    }
    wcscpy(result + path_len, filename);
    return result;
}

static PyObject *
DirEntry_new(path_t *path, WIN32_FIND_DATAW *dataW)
{
    DirEntry *entry;
    BY_HANDLE_FILE_INFORMATION file_info;
    ULONG reparse_tag;
    wchar_t *joined_path;

    entry = PyObject_New(DirEntry, &DirEntryType);
    if (!entry) {
        return NULL;
    }
    entry->name = NULL;
    entry->path = NULL;
    entry->stat = NULL;
    entry->lstat = NULL;
    entry->got_file_index = 0;

    entry->name = PyUnicode_FromWideChar(dataW->cFileName, wcslen(dataW->cFileName));
    if (!entry->name) {
        goto error;
    }

    joined_path = join_path_filenameW(path->wide, dataW->cFileName);
    if (!joined_path) {
        goto error;
    }
    entry->path = PyUnicode_FromWideChar(joined_path, wcslen(joined_path));
    PyMem_Free(joined_path);
    if (!entry->path) {
        goto error;
    }

    find_data_to_file_info_w(dataW, &file_info, &reparse_tag);
    _Py_attribute_data_to_stat(&file_info, reparse_tag, &entry->win32_lstat);

    return (PyObject *)entry;

error:
    Py_XDECREF(entry);
    return NULL;
}

static PyObject *
ScandirIterator_iternext(ScandirIterator *iterator)
{
    WIN32_FIND_DATAW *file_data = &iterator->file_data;
    BOOL success;

    if (iterator->handle == INVALID_HANDLE_VALUE) {
        /* Happens if the iterator is iterated twice */
        PyErr_SetNone(PyExc_StopIteration);
        return NULL;
    }

    while (1) {
        if (!iterator->first_time) {
            Py_BEGIN_ALLOW_THREADS
            success = FindNextFileW(iterator->handle, file_data);
            Py_END_ALLOW_THREADS
            if (!success) {
                if (GetLastError() != ERROR_NO_MORE_FILES) {
                    return path_error(&iterator->path);
                }
                /* No more files found in directory, stop iterating */
                break;
            }
        }

        /* Skip over . and .. */
        if (wcscmp(file_data->cFileName, L".") != 0 &&
                wcscmp(file_data->cFileName, L"..") != 0) {
            return DirEntry_new(&iterator->path, file_data);
        }

        /* Loop till we get a non-dot directory or finish iterating */
        iterator->first_time = 0;
    }

    Py_BEGIN_ALLOW_THREADS
    success = FindClose(iterator->handle);
    Py_END_ALLOW_THREADS
    if (!success) {
        return path_error(&iterator->path);
    }
    iterator->handle = INVALID_HANDLE_VALUE;

    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
}

#else /* POSIX */

static char *
join_path_filenameA(char *path_narrow, char* filename, Py_ssize_t filename_len)
{
    Py_ssize_t path_len;
    char *result;
    char ch;

    if (!path_narrow) { /* Default arg: "." */
        path_narrow = ".";
        path_len = 1;
    }
    else {
        path_len = strlen(path_narrow);
    }

    if (filename_len == -1) {
        filename_len = strlen(filename);
    }

    /* The +1's are for the path separator and the NUL */
    result = PyMem_Malloc(path_len + 1 + filename_len + 1);
    if (!result) {
        PyErr_NoMemory();
        return NULL;
    }
    strcpy(result, path_narrow);
    ch = result[path_len - 1];
    if (ch != '/') {
        result[path_len++] = '/';
    }
    strcpy(result + path_len, filename);
    return result;
}

static PyObject *
DirEntry_new(path_t *path, char *name, Py_ssize_t name_len,
             unsigned char d_type, ino_t d_ino)
{
    DirEntry *entry;
    char *joined_path;

    entry = PyObject_New(DirEntry, &DirEntryType);
    if (!entry) {
        return NULL;
    }
    entry->name = NULL;
    entry->path = NULL;
    entry->stat = NULL;
    entry->lstat = NULL;

    joined_path = join_path_filenameA(path->narrow, name, name_len);
    if (!joined_path) {
        goto error;
    }

    if (!path->narrow || !PyBytes_Check(path->object)) {
        entry->name = PyUnicode_DecodeFSDefaultAndSize(name, name_len);
        entry->path = PyUnicode_DecodeFSDefault(joined_path);
    }
    else {
        entry->name = PyBytes_FromStringAndSize(name, name_len);
        entry->path = PyBytes_FromString(joined_path);
    }
    PyMem_Free(joined_path);
    if (!entry->name || !entry->path) {
        goto error;
    }

    entry->d_type = d_type;
    entry->d_ino = d_ino;

    return (PyObject *)entry;

error:
    Py_XDECREF(entry);
    return NULL;
}

static PyObject *
ScandirIterator_iternext(ScandirIterator *iterator)
{
    struct dirent *direntp;
    Py_ssize_t name_len;
    int is_dot;
    int result;
    unsigned char d_type;

    if (!iterator->dirp) {
        /* Happens if the iterator is iterated twice */
        PyErr_SetNone(PyExc_StopIteration);
        return NULL;
    }

    while (1) {
        errno = 0;
        Py_BEGIN_ALLOW_THREADS
        direntp = readdir(iterator->dirp);
        Py_END_ALLOW_THREADS

        if (!direntp) {
            if (errno != 0) {
                return path_error(&iterator->path);
            }
            /* No more files found in directory, stop iterating */
            break;
        }

        /* Skip over . and .. */
        name_len = NAMLEN(direntp);
        is_dot = direntp->d_name[0] == '.' &&
                 (name_len == 1 || (direntp->d_name[1] == '.' && name_len == 2));
        if (!is_dot) {
#if defined(__GLIBC__) && !defined(_DIRENT_HAVE_D_TYPE)
            d_type = DT_UNKNOWN;  /* System doesn't support d_type */
#else
            d_type = direntp->d_type;
#endif
            return DirEntry_new(&iterator->path, direntp->d_name,
                                name_len, d_type, direntp->d_ino);
        }

        /* Loop till we get a non-dot directory or finish iterating */
    }

    Py_BEGIN_ALLOW_THREADS
    result = closedir(iterator->dirp);
    Py_END_ALLOW_THREADS
    if (result != 0) {
        return path_error(&iterator->path);
    }
    iterator->dirp = NULL;

    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
}

#endif

static void
ScandirIterator_dealloc(ScandirIterator *iterator)
{
    Py_XDECREF(iterator->path.object);
    path_cleanup(&iterator->path);
    Py_TYPE(iterator)->tp_free((PyObject *)iterator);
}

PyTypeObject ScandirIteratorType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    MODNAME ".ScandirIterator",             /* tp_name */
    sizeof(ScandirIterator),                /* tp_basicsize */
    0,                                      /* tp_itemsize */
    /* methods */
    (destructor)ScandirIterator_dealloc,    /* tp_dealloc */
    0,                                      /* tp_print */
    0,                                      /* tp_getattr */
    0,                                      /* tp_setattr */
    0,                                      /* tp_compare */
    0,                                      /* tp_repr */
    0,                                      /* tp_as_number */
    0,                                      /* tp_as_sequence */
    0,                                      /* tp_as_mapping */
    0,                                      /* tp_hash */
    0,                                      /* tp_call */
    0,                                      /* tp_str */
    0,                                      /* tp_getattro */
    0,                                      /* tp_setattro */
    0,                                      /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                     /* tp_flags */
    0,                                      /* tp_doc */
    0,                                      /* tp_traverse */
    0,                                      /* tp_clear */
    0,                                      /* tp_richcompare */
    0,                                      /* tp_weaklistoffset */
    PyObject_SelfIter,                      /* tp_iter */
    (iternextfunc)ScandirIterator_iternext, /* tp_iternext */
};

static PyObject *
posix_scandir(PyObject *self, PyObject *args, PyObject *kwargs)
{
    ScandirIterator *iterator;
    static char *keywords[] = {"path", NULL};
#ifdef MS_WINDOWS
    wchar_t *path_strW;
#endif

    iterator = PyObject_New(ScandirIterator, &ScandirIteratorType);
    if (!iterator) {
        return NULL;
    }
    memset(&iterator->path, 0, sizeof(path_t));
    iterator->path.function_name = "scandir";
    iterator->path.nullable = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O&:scandir", keywords,
                                     path_converter, &iterator->path)) {
        goto error;
    }

    /* path_converter doesn't keep path.object around, so do it
       manually for the lifetime of the iterator here (the refcount
       is decremented in ScandirIterator_dealloc)
    */
    Py_XINCREF(iterator->path.object);

#ifdef MS_WINDOWS
    if (iterator->path.narrow) {
        PyErr_SetString(PyExc_TypeError,
                        "os.scandir() doesn't support bytes path on Windows, use Unicode instead");
        goto error;
    }
    iterator->first_time = 1;

    path_strW = join_path_filenameW(iterator->path.wide, L"*.*");
    if (!path_strW) {
        goto error;
    }

    Py_BEGIN_ALLOW_THREADS
    iterator->handle = FindFirstFileW(path_strW, &iterator->file_data);
    Py_END_ALLOW_THREADS

    PyMem_Free(path_strW);  /* We're done with path_strW now */

    if (iterator->handle == INVALID_HANDLE_VALUE) {
        path_error(&iterator->path);
        goto error;
    }
#else /* POSIX */
    errno = 0;
    Py_BEGIN_ALLOW_THREADS
    iterator->dirp = opendir(iterator->path.narrow ? iterator->path.narrow : ".");
    Py_END_ALLOW_THREADS

    if (!iterator->dirp) {
        path_error(&iterator->path);
        goto error;
    }
#endif

    return (PyObject *)iterator;

error:
    Py_DECREF(iterator);
    return NULL;
}
