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
#else
#define INITERROR return
#define FROM_LONG PyInt_FromLong
#define FROM_STRING PyString_FromStringAndSize
#endif

#define PATTERN_LEN 1024
typedef struct {
	PyObject_HEAD
    wchar_t pattern[PATTERN_LEN];
    void *handle;
} FileIterator;

static PyObject *_iterfile(Py_UNICODE *);
static PyObject *iterfile (PyObject *, PyObject *);

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

    iterator = iterfile(self, Py_BuildValue("(u)", wnamebuf));
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
_iterfile (Py_UNICODE *pattern)
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
    wcscpy(iterator->pattern, pattern);
    iterator->handle = NULL;

    return (PyObject *)iterator;
}

static PyObject*
iterfile (PyObject *self, PyObject *args)
{
PyUnicodeObject *po;

    if (!PyArg_ParseTuple(args, "U", &po)) {
        return NULL;
    }
    return _iterfile(PyUnicode_AS_UNICODE(po));
}

static PyMethodDef scandir_methods[] = {
    {"scandir_helper", (PyCFunction)scandir_helper, METH_VARARGS, NULL},
    {"iterfile", (PyCFunction)iterfile, METH_VARARGS, NULL},
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
