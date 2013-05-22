// scandir C speedups
//
// There's a fair bit of PY_MAJOR_VERSION boilerplate to support both Python 2
// and Python 3 -- the structure of this is taken from here:
// http://docs.python.org/3.3/howto/cporting.html

#include <Python.h>
#include <structseq.h>

#ifdef MS_WINDOWS
#include <windows.h>
#endif

#if PY_MAJOR_VERSION >= 3
#define INITERROR return NULL
#define FROM_LONG PyLong_FromLong
#define FROM_STRING PyUnicode_FromStringAndSize
#define UNICODE_LENGTH PyUnicode_GET_LENGTH
#else
#define INITERROR return
#define FROM_LONG PyInt_FromLong
#define FROM_STRING PyString_FromStringAndSize
#define UNICODE_LENGTH PyUnicode_GET_SIZE
#endif

#ifdef Py_CLEANUP_SUPPORTED
#define PATH_CONVERTER_RESULT (Py_CLEANUP_SUPPORTED)
#else
#define PATH_CONVERTER_RESULT (1)
#endif
/*
START OF CODE ADAPTED FROM POSIXMODULE.C
*/
#ifdef AT_FDCWD
/*
 * Why the (int) cast?  Solaris 10 defines AT_FDCWD as 0xffd19553 (-3041965);
 * without the int cast, the value gets interpreted as uint (4291925331),
 * which doesn't play nicely with all the initializer lines in this file that
 * look like this:
 *      int dir_fd = DEFAULT_DIR_FD;
 */
#define DEFAULT_DIR_FD (int)AT_FDCWD
#else
#define DEFAULT_DIR_FD (-100)
#endif

#ifdef MS_WINDOWS
static int
win32_warn_bytes_api()
{
    return PyErr_WarnEx(PyExc_DeprecationWarning,
        "The Windows bytes API has been deprecated, "
        "use Unicode filenames instead",
        1);
}
#endif

static int
_fd_converter(PyObject *o, int *p, const char *allowed)
{
    int overflow;
    long long_value = PyLong_AsLongAndOverflow(o, &overflow);
    if (PyFloat_Check(o) ||
        (long_value == -1 && !overflow && PyErr_Occurred())) {
        PyErr_Clear();
        PyErr_Format(PyExc_TypeError,
                        "argument should be %s, not %.200s",
                        allowed, Py_TYPE(o)->tp_name);
        return 0;
    }
    if (overflow > 0 || long_value > INT_MAX) {
        PyErr_SetString(PyExc_OverflowError,
                        "signed integer is greater than maximum");
        return 0;
    }
    if (overflow < 0 || long_value < INT_MIN) {
        PyErr_SetString(PyExc_OverflowError,
                        "signed integer is less than minimum");
        return 0;
    }
    *p = (int)long_value;
    return 1;
}

static int
dir_fd_converter(PyObject *o, void *p)
{
    if (o == Py_None) {
        *(int *)p = DEFAULT_DIR_FD;
        return 1;
    }
    return _fd_converter(o, (int *)p, "integer");
}
/*
 * A PyArg_ParseTuple "converter" function
 * that handles filesystem paths in the manner
 * preferred by the os module.
 *
 * path_converter accepts (Unicode) strings and their
 * subclasses, and bytes and their subclasses.  What
 * it does with the argument depends on the platform:
 *
 *   * On Windows, if we get a (Unicode) string we
 *     extract the wchar_t * and return it; if we get
 *     bytes we extract the char * and return that.
 *
 *   * On all other platforms, strings are encoded
 *     to bytes using PyUnicode_FSConverter, then we
 *     extract the char * from the bytes object and
 *     return that.
 *
 * path_converter also optionally accepts signed
 * integers (representing open file descriptors) instead
 * of path strings.
 *
 * Input fields:
 *   path.nullable
 *     If nonzero, the path is permitted to be None.
 *   path.allow_fd
 *     If nonzero, the path is permitted to be a file handle
 *     (a signed int) instead of a string.
 *   path.function_name
 *     If non-NULL, path_converter will use that as the name
 *     of the function in error messages.
 *     (If path.argument_name is NULL it omits the function name.)
 *   path.argument_name
 *     If non-NULL, path_converter will use that as the name
 *     of the parameter in error messages.
 *     (If path.argument_name is NULL it uses "path".)
 *
 * Output fields:
 *   path.wide
 *     Points to the path if it was expressed as Unicode
 *     and was not encoded.  (Only used on Windows.)
 *   path.narrow
 *     Points to the path if it was expressed as bytes,
 *     or it was Unicode and was encoded to bytes.
 *   path.fd
 *     Contains a file descriptor if path.accept_fd was true
 *     and the caller provided a signed integer instead of any
 *     sort of string.
 *
 *     WARNING: if your "path" parameter is optional, and is
 *     unspecified, path_converter will never get called.
 *     So if you set allow_fd, you *MUST* initialize path.fd = -1
 *     yourself!
 *   path.length
 *     The length of the path in characters, if specified as
 *     a string.
 *   path.object
 *     The original object passed in.
 *   path.cleanup
 *     For internal use only.  May point to a temporary object.
 *     (Pay no attention to the man behind the curtain.)
 *
 *   At most one of path.wide or path.narrow will be non-NULL.
 *   If path was None and path.nullable was set,
 *     or if path was an integer and path.allow_fd was set,
 *     both path.wide and path.narrow will be NULL
 *     and path.length will be 0.
 *
 *   path_converter takes care to not write to the path_t
 *   unless it's successful.  However it must reset the
 *   "cleanup" field each time it's called.
 *
 * Use as follows:
 *      path_t path;
 *      memset(&path, 0, sizeof(path));
 *      PyArg_ParseTuple(args, "O&", path_converter, &path);
 *      // ... use values from path ...
 *      path_cleanup(&path);
 *
 * (Note that if PyArg_Parse fails you don't need to call
 * path_cleanup().  However it is safe to do so.)
 */
typedef struct {
    const char *function_name;
    const char *argument_name;
    int nullable;
    int allow_fd;
    wchar_t *wide;
    char *narrow;
    int fd;
    Py_ssize_t length;
    PyObject *object;
    PyObject *cleanup;
} path_t;

static void
path_cleanup(path_t *path) {
    if (path->cleanup) {
        Py_DECREF(path->cleanup);
        path->cleanup = NULL;
    }
}

static int
path_converter(PyObject *o, void *p) {
    path_t *path = (path_t *)p;
    PyObject *unicode, *bytes;
    Py_ssize_t length;
    char *narrow;

#define FORMAT_EXCEPTION(exc, fmt) \
    PyErr_Format(exc, "%s%s" fmt, \
        path->function_name ? path->function_name : "", \
        path->function_name ? ": "                : "", \
        path->argument_name ? path->argument_name : "path")

    /* Py_CLEANUP_SUPPORTED support */
    if (o == NULL) {
        path_cleanup(path);
        return 1;
    }

    /* ensure it's always safe to call path_cleanup() */
    path->cleanup = NULL;

    if (o == Py_None) {
        if (!path->nullable) {
            FORMAT_EXCEPTION(PyExc_TypeError,
                             "can't specify None for %s argument");
            return 0;
        }
        path->wide = NULL;
        path->narrow = NULL;
        path->length = 0;
        path->object = o;
        path->fd = -1;
        return 1;
    }


    unicode = PyUnicode_FromObject(o);
    if (unicode) {
#ifdef MS_WINDOWS
        wchar_t *wide;
        length = PyUnicode_GET_SIZE(unicode);
        if (length > 32767) {
            FORMAT_EXCEPTION(PyExc_ValueError, "%s too long for Windows");
            Py_DECREF(unicode);
            return 0;
        }

        wide = PyUnicode_AsUnicode(unicode);
        if (!wide) {
            Py_DECREF(unicode);
            return 0;
        }

        path->wide = wide;
        path->narrow = NULL;
        path->length = length;
        path->object = o;
        path->fd = -1;
        path->cleanup = unicode;
        return PATH_CONVERTER_RESULT;
#else
        int converted = PyUnicode_FSConverter(unicode, &bytes);
        Py_DECREF(unicode);
        if (!converted)
            bytes = NULL;
#endif
    }
    else {
        PyErr_Clear();
#if PY_MAJOR_VERSION >= 3
        if (PyObject_CheckBuffer(o))
            bytes = PyBytes_FromObject(o);
        else
            bytes = NULL;
#else
        if (PyString_Check(o)) {
            bytes = o;
            Py_INCREF(bytes);
        }
        else
            bytes = NULL;
#endif
        if (!bytes) {
            PyErr_Clear();
            if (path->allow_fd) {
                int fd;
                int result = _fd_converter(o, &fd,
                        "string, bytes or integer");
                if (result) {
                    path->wide = NULL;
                    path->narrow = NULL;
                    path->length = 0;
                    path->object = o;
                    path->fd = fd;
                    return result;
                }
            }
        }
    }

    if (!bytes) {
        if (!PyErr_Occurred())
            FORMAT_EXCEPTION(PyExc_TypeError, "illegal type for %s parameter");
        return 0;
    }

#ifdef MS_WINDOWS
    if (win32_warn_bytes_api()) {
        Py_DECREF(bytes);
        return 0;
    }
#endif

#if PY_MAJOR_VERSION >= 3
    length = PyBytes_GET_SIZE(bytes);
#else
    length = PyString_GET_SIZE(bytes);
#endif
#ifdef MS_WINDOWS
    if (length > MAX_PATH) {
        FORMAT_EXCEPTION(PyExc_ValueError, "%s too long for Windows");
        Py_DECREF(bytes);
        return 0;
    }
#endif

#if PY_MAJOR_VERSION >= 3
    narrow = PyBytes_AS_STRING(bytes);
#else
    narrow = PyString_AS_STRING(bytes);
#endif
    if (length != strlen(narrow)) {
        FORMAT_EXCEPTION(PyExc_ValueError, "embedded NUL character in %s");
        Py_DECREF(bytes);
        return 0;
    }

    path->wide = NULL;
    path->narrow = narrow;
    path->length = length;
    path->object = o;
    path->fd = -1;
    path->cleanup = bytes;
    return PATH_CONVERTER_RESULT;
}

/*
END OF CODE ADAPTED FROM POSIXMODULE.C
*/

#define PATTERN_LEN 1024
typedef struct {
	PyObject_HEAD
    wchar_t pattern[PATTERN_LEN];
    void *handle;
} FileIterator;

static PyObject *_iterfile(path_t *);
static PyObject *iterfile (PyObject *, PyObject *, PyObject *);

#ifdef MS_WINDOWS

static PyObject *
win32_error_unicode(char* function, Py_UNICODE* filename)
{
    errno = GetLastError();
    if (filename)
        return PyErr_SetFromWindowsErrWithUnicodeFilename(errno, filename);
    else
        return PyErr_SetFromWindowsErr(errno);
}

/* Below, we *know* that ugo+r is 0444 */
#if _S_IREAD != 0400
#error Unsupported C library
#endif
static int
attributes_to_mode(DWORD attr)
{
    int m = 0;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        m |= _S_IFDIR | 0111; /* IFEXEC for user,group,other */
    else
        m |= _S_IFREG;
    if (attr & FILE_ATTRIBUTE_READONLY)
        m |= 0444;
    else
        m |= 0666;
    return m;
}

double
filetime_to_time(FILETIME *filetime)
{
    const double SECONDS_BETWEEN_EPOCHS = 11644473600.0;

    unsigned long long total = (unsigned long long)filetime->dwHighDateTime << 32 |
                               (unsigned long long)filetime->dwLowDateTime;
    return (double)total / 10000000.0 - SECONDS_BETWEEN_EPOCHS;
}

static PyTypeObject StatResultType;

static PyObject *
find_data_to_statresult(WIN32_FIND_DATAW *data)
{
    PY_LONG_LONG size;
    PyObject *v = PyStructSequence_New(&StatResultType);
    if (v == NULL)
        return NULL;

    size = (PY_LONG_LONG)data->nFileSizeHigh << 32 |
           (PY_LONG_LONG)data->nFileSizeLow;

    PyStructSequence_SET_ITEM(v, 0, FROM_LONG(attributes_to_mode(data->dwFileAttributes)));
    PyStructSequence_SET_ITEM(v, 1, FROM_LONG(0));
    PyStructSequence_SET_ITEM(v, 2, FROM_LONG(0));
    PyStructSequence_SET_ITEM(v, 3, FROM_LONG(0));
    PyStructSequence_SET_ITEM(v, 4, FROM_LONG(0));
    PyStructSequence_SET_ITEM(v, 5, FROM_LONG(0));
    PyStructSequence_SET_ITEM(v, 6, PyLong_FromLongLong((PY_LONG_LONG)size));
    PyStructSequence_SET_ITEM(v, 7, PyFloat_FromDouble(filetime_to_time(&data->ftLastAccessTime)));
    PyStructSequence_SET_ITEM(v, 8, PyFloat_FromDouble(filetime_to_time(&data->ftLastWriteTime)));
    PyStructSequence_SET_ITEM(v, 9, PyFloat_FromDouble(filetime_to_time(&data->ftCreationTime)));

    if (PyErr_Occurred()) {
        Py_DECREF(v);
        return NULL;
    }

    return v;
}

static PyStructSequence_Field stat_result_fields[] = {
    {"st_mode",    "protection bits"},
    {"st_ino",     "inode"},
    {"st_dev",     "device"},
    {"st_nlink",   "number of hard links"},
    {"st_uid",     "user ID of owner"},
    {"st_gid",     "group ID of owner"},
    {"st_size",    "total size, in bytes"},
    {"st_atime",   "time of last access"},
    {"st_mtime",   "time of last modification"},
    {"st_ctime",   "time of last change"},
    {0}
};

static PyStructSequence_Desc stat_result_desc = {
    "stat_result", /* name */
    NULL, /* doc */
    stat_result_fields,
    10
};

static void
fi_dealloc(FileIterator *iterator)
{
HANDLE handle;

    if (iterator->handle != NULL) {
        handle = *((HANDLE *)iterator->handle);
        if (handle != INVALID_HANDLE_VALUE) {
            Py_BEGIN_ALLOW_THREADS
            FindClose(handle);
            Py_END_ALLOW_THREADS
        }
        free(iterator->handle);
    }
	PyObject_Del(iterator);
}

static PyObject *
fi_iternext(PyObject *iterator)
{
PyObject *file_data;
BOOL is_finished;
WIN32_FIND_DATAW data;
HANDLE *p_handle;

	FileIterator *fi = (FileIterator *)iterator;
    memset(&data, 0, sizeof(data));

    /*
    Put data into the iterator's data buffer, using the state of the
    hFind handle to determine whether this is the first iteration or
    a successive one.

    If the API indicates that there are no (or no more) files, raise
    a StopIteration exception.
    */
    is_finished = FALSE;
    if (fi->handle == NULL) {
        p_handle = malloc(sizeof(HANDLE));
        Py_BEGIN_ALLOW_THREADS
        *p_handle = FindFirstFileW(fi->pattern, &data);
        Py_END_ALLOW_THREADS

        if (*p_handle == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND) {
                return PyErr_SetFromWindowsErr(GetLastError());
            }
            is_finished = TRUE;
        }
        fi->handle = (void *)p_handle;
    }
	else {
		BOOL ok;
        p_handle = (HANDLE *)fi->handle;
		Py_BEGIN_ALLOW_THREADS
		ok = FindNextFileW(*p_handle, &data);
		Py_END_ALLOW_THREADS

        if (!ok) {
			if (GetLastError() != ERROR_NO_MORE_FILES) {
			    return PyErr_SetFromWindowsErr(GetLastError());
			}
            is_finished = TRUE;
		}
	}

    if (is_finished) {
        PyErr_SetNone(PyExc_StopIteration);
        return NULL;
    }

    file_data = find_data_to_statresult(&data);
    if (!file_data) {
        return PyErr_SetFromWindowsErr(GetLastError());
    }
    else {
        return Py_BuildValue("u#O",
                            data.cFileName, wcslen(data.cFileName),
                            file_data);
    }
}

static PyObject *
scandir_helper(PyObject *self, PyObject *args)
{
    Py_UNICODE *wnamebuf;
    Py_ssize_t len;
    PyObject *po;
    PyObject *iterator;

    if (!PyArg_ParseTuple(args, "U:scandir_helper", &po))
        return NULL;

    /* Overallocate for \\*.*\0 */
    len = PyUnicode_GET_SIZE(po);
    wnamebuf = malloc((len + 5) * sizeof(wchar_t));
    if (!wnamebuf) {
        PyErr_NoMemory();
        return NULL;
    }
    wcscpy(wnamebuf, PyUnicode_AS_UNICODE(po));
    if (len > 0) {
        Py_UNICODE wch = wnamebuf[len-1];
        if (wch != L'/' && wch != L'\\' && wch != L':')
            wnamebuf[len++] = L'\\';
        wcscpy(wnamebuf + len, L"*.*");
    }

    iterator = iterfile(self, Py_BuildValue("(u)", wnamebuf), NULL);
    if (iterator == NULL) {
        free(wnamebuf);
        return NULL;
    }

    return iterator;
}

#else  // Linux / OS X

#include <dirent.h>
#define NAMLEN(dirent) strlen((dirent)->d_name)

static PyObject *
posix_error_with_allocated_filename(char* name)
{
    PyObject *rc = PyErr_SetFromErrnoWithFilename(PyExc_OSError, name);
    PyMem_Free(name);
    return rc;
}

static PyObject *
scandir_helper(PyObject *self, PyObject *args)
{
    char *name = NULL;
    PyObject *d, *v, *name_ino_type;
    DIR *dirp;
    struct dirent *ep;
    int arg_is_unicode = 1;

    errno = 0;
    if (!PyArg_ParseTuple(args, "U:scandir_helper", &v)) {
        arg_is_unicode = 0;
        PyErr_Clear();
    }
    if (!PyArg_ParseTuple(args, "et:scandir_helper", Py_FileSystemDefaultEncoding, &name))
        return NULL;

    Py_BEGIN_ALLOW_THREADS
    dirp = opendir(name);
    Py_END_ALLOW_THREADS
    if (dirp == NULL) {
        return posix_error_with_allocated_filename(name);
    }
    if ((d = PyList_New(0)) == NULL) {
        Py_BEGIN_ALLOW_THREADS
        closedir(dirp);
        Py_END_ALLOW_THREADS
        PyMem_Free(name);
        return NULL;
    }
    for (;;) {
        errno = 0;
        Py_BEGIN_ALLOW_THREADS
        ep = readdir(dirp);
        Py_END_ALLOW_THREADS
        if (ep == NULL) {
            if (errno == 0) {
                break;
            } else {
                Py_BEGIN_ALLOW_THREADS
                closedir(dirp);
                Py_END_ALLOW_THREADS
                Py_DECREF(d);
                return posix_error_with_allocated_filename(name);
            }
        }
        if (ep->d_name[0] == '.' &&
            (NAMLEN(ep) == 1 ||
             (ep->d_name[1] == '.' && NAMLEN(ep) == 2)))
            continue;
        v = FROM_STRING(ep->d_name, NAMLEN(ep));
        if (v == NULL) {
            Py_DECREF(d);
            d = NULL;
            break;
        }
        if (arg_is_unicode) {
            PyObject *w;

            w = PyUnicode_FromEncodedObject(v,
                                            Py_FileSystemDefaultEncoding,
                                            "strict");
            if (w != NULL) {
                Py_DECREF(v);
                v = w;
            }
            else {
                /* fall back to the original byte string, as
                   discussed in patch #683592 */
                PyErr_Clear();
            }
        }
        name_ino_type = PyTuple_Pack(3, v, FROM_LONG(ep->d_ino), FROM_LONG(ep->d_type));
        if (name_ino_type == NULL) {
            Py_DECREF(v);
            Py_DECREF(d);
            d = NULL;
            break;
        }
        if (PyList_Append(d, name_ino_type) != 0) {
            Py_DECREF(v);
            Py_DECREF(d);
            Py_DECREF(name_ino_type);
            d = NULL;
            break;
        }
        Py_DECREF(v);
    }
    Py_BEGIN_ALLOW_THREADS
    closedir(dirp);
    Py_END_ALLOW_THREADS
    PyMem_Free(name);

    return d;
}

static void
fi_dealloc(FileIterator *iterator)
{
DIR *p_dir;

    if (iterator->handle != NULL) {
        p_dir = (DIR *)iterator->handle;
        Py_BEGIN_ALLOW_THREADS
        closedir(p_dir);
        Py_END_ALLOW_THREADS
    }
	PyObject_Del(iterator);
}

static PyObject *
fi_iternext(PyObject *iterator)
{
/*
FileIterator *fi = (FileIterator *)iterator;
DIR *p_dir;
BOOL is_finished = FALSE;

    is_finished = FALSE;
    if (fi->handle == NULL) {
        Py_BEGIN_ALLOW_THREADS
        p_dir = opendir(name);
        Py_END_ALLOW_THREADS
        if (p_dir == NULL) {
            return posix_error_with_allocated_filename(name);
        }
        fi->handle = p_dir;
    }

    errno = 0;
    Py_BEGIN_ALLOW_THREADS
    ep = readdir(p_dir);
    Py_END_ALLOW_THREADS
    if (ep == NULL) {
        if (errno == 0) {
            is_finished = TRUE;
        } else {
            Py_BEGIN_ALLOW_THREADS
            closedir(dirp);
            Py_END_ALLOW_THREADS
            return posix_error_with_allocated_filename(name);
        }
    }
    v = FROM_STRING(ep->d_name, NAMLEN(ep));
    if (v == NULL) {
        return NULL
    }
    if (arg_is_unicode) {
        PyObject *w;

        w = PyUnicode_FromEncodedObject(v,
                                        Py_FileSystemDefaultEncoding,
                                        "strict");
        if (w != NULL) {
            Py_DECREF(v);
            v = w;
        }
        else {
            PyErr_Clear();
        }
    }
    Py_DECREF(v);

    Py_BEGIN_ALLOW_THREADS
    closedir(p_dir);
    Py_END_ALLOW_THREADS

    if (is_finished) {
        PyErr_SetNone(PyExc_StopIteration);
        return NULL;
    }

    return Py_BuildValue("u#kk", v, ep->d_ino, ep->d_type);
*/
    return Py_None;
}
#endif

PyTypeObject FileIterator_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
	"FileIterator",				        /* tp_name */
	sizeof(FileIterator),			    /* tp_basicsize */
	0,					                /* tp_itemsize */
	/* methods */
	(destructor)fi_dealloc, 		    /* tp_dealloc */
	0,					                /* tp_print */
	0,					                /* tp_getattr */
	0,					                /* tp_setattr */
	0,					                /* tp_compare */
	0,					                /* tp_repr */
	0,					                /* tp_as_number */
	0,					                /* tp_as_sequence */
	0,					                /* tp_as_mapping */
	0,					                /* tp_hash */
	0,					                /* tp_call */
	0,					                /* tp_str */
	PyObject_GenericGetAttr,		    /* tp_getattro */
	0,					                /* tp_setattro */
	0,					                /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,                 /* tp_flags */
 	0,					                /* tp_doc */
 	0,					                /* tp_traverse */
 	0,					                /* tp_clear */
	0,					                /* tp_richcompare */
	0,					                /* tp_weaklistoffset */
	PyObject_SelfIter,	                /* tp_iter */
	(iternextfunc)fi_iternext,		    /* tp_iternext */
	0,					                /* tp_methods */
	0,					                /* tp_members */
	0,					                /* tp_getset */
	0,					                /* tp_base */
	0,					                /* tp_dict */
	0,					                /* tp_descr_get */
	0,					                /* tp_descr_set */
};

static PyObject*
_iterfile (path_t *path)
{
FileIterator *iterator;

    /*
    Create and return a FileIterator object.

    Initialise it with the pattern to iterate over and an empty handle, the latter indicating
    to the iternext implementation that iteration has not yet started.
    */
    iterator = PyObject_New(FileIterator, &FileIterator_Type);
    if (iterator == NULL) {
        return NULL;
    }
    wcscpy(iterator->pattern, path->wide);
    iterator->handle = NULL;

    return (PyObject *)iterator;
}

static PyObject*
iterfile (PyObject *self, PyObject *args, PyObject *kwargs)
{
    path_t path;
    static char *keywords[] = {"path", NULL};
    PyObject *return_value;

    memset(&path, 0, sizeof(path));
    path.function_name = "iterfile";
#ifdef HAVE_FDOPENDIR
    path.allow_fd = 1;
    path.fd = -1;
#endif

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O&:iterfile", keywords,
                                     path_converter, &path)) {
        return NULL;
    }

#if defined(MS_WINDOWS) && !defined(HAVE_OPENDIR)
    return_value = _iterfile(&path);
#else
    return_value = _posix_listdir(&path);
#endif
    path_cleanup(&path);
    return return_value;
}

static PyMethodDef scandir_methods[] = {
    {"scandir_helper", (PyCFunction)scandir_helper, METH_VARARGS, NULL},
    {"iterfile", (PyCFunction)iterfile, METH_VARARGS | METH_KEYWORDS, NULL},
    {NULL, NULL},
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,
        "_scandir",
        NULL,
        0,
        scandir_methods,
        NULL,
        NULL,
        NULL,
        NULL,
};
#endif

#if PY_MAJOR_VERSION >= 3
PyObject *
PyInit__scandir(void)
{
    PyObject *module = PyModule_Create(&moduledef);
#else
void
init_scandir(void)
{
    PyObject *module = Py_InitModule("_scandir", scandir_methods);
#endif
    if (module == NULL) {
        INITERROR;
    }

#ifdef MS_WINDOWS
    stat_result_desc.name = "scandir.stat_result";
    PyStructSequence_InitType(&StatResultType, &stat_result_desc);
#endif

#if PY_MAJOR_VERSION >= 3
    return module;
#endif
}
