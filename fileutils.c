#include <python.h>
#include <datetime.h>
#include <windows.h>
#include <tchar.h>
#include <stdio.h>

typedef struct {
	PyObject_HEAD
	HANDLE hFind;
	WIN32_FIND_DATAW buffer;
	BOOL seen_first;
	BOOL empty;
} FindFileIterator;

static void
ffi_dealloc(FindFileIterator *it)
{
	if (it->hFind != INVALID_HANDLE_VALUE)
		FindClose(it->hFind);
	PyObject_Del(it);
}

static PyObject *
ffi_iternext(PyObject *iterator)
{
PyObject *filename;

	FindFileIterator *ffi = (FindFileIterator *)iterator;
	if (ffi->empty) {
		PyErr_SetNone(PyExc_StopIteration);
		return NULL;
	}
	if (!ffi->seen_first)
		ffi->seen_first = TRUE;
	else {
		BOOL ok;
		Py_BEGIN_ALLOW_THREADS
		memset (&ffi->buffer, 0, sizeof (ffi->buffer));
		ok = FindNextFileW (ffi->hFind, &ffi->buffer);
		Py_END_ALLOW_THREADS
		if (!ok) {
			if (GetLastError()==ERROR_NO_MORE_FILES) {
				PyErr_SetNone(PyExc_StopIteration);
				return NULL;
			}
			return PyErr_SetFromWindowsErr (GetLastError());
		}
	}
    filename = PyUnicode_FromWideChar (ffi->buffer.cFileName, -1);
    if (!filename)
        return PyErr_SetFromWindowsErr (GetLastError());
    else
        return Py_BuildValue ("O", filename);
}

PyTypeObject FindFileIterator_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
	"FindFileIterator",				/* tp_name */
	sizeof(FindFileIterator),			/* tp_basicsize */
	0,					/* tp_itemsize */
	/* methods */
	(destructor)ffi_dealloc, 		/* tp_dealloc */
	0,					/* tp_print */
	0,					/* tp_getattr */
	0,					/* tp_setattr */
	0,					/* tp_compare */
	0,					/* tp_repr */
	0,					/* tp_as_number */
	0,					/* tp_as_sequence */
	0,					/* tp_as_mapping */
	0,					/* tp_hash */
	0,					/* tp_call */
	0,					/* tp_str */
	PyObject_GenericGetAttr,		/* tp_getattro */
	0,					/* tp_setattro */
	0,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT, /* tp_flags */
 	0,					/* tp_doc */
 	0,					/* tp_traverse */
 	0,					/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	PyObject_SelfIter,	/* tp_iter */
	(iternextfunc)ffi_iternext,		/* tp_iternext */
	0,					/* tp_methods */
	0,					/* tp_members */
	0,					/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
};

static PyObject*
fileutils_file_iterator (PyObject *self, PyObject *args)
{
PyUnicodeObject *po;
FindFileIterator *it;

    if (!PyArg_ParseTuple (args, "U", &po))
        return NULL;

    it = PyObject_New (FindFileIterator, &FindFileIterator_Type);
    if (it == NULL) {
        return NULL;
    }
    it->seen_first = FALSE;
    it->empty = FALSE;
    it->hFind = INVALID_HANDLE_VALUE;
    memset(&it->buffer, 0, sizeof(it->buffer));

    Py_BEGIN_ALLOW_THREADS
    it->hFind = FindFirstFileW (PyUnicode_AS_UNICODE (po), &it->buffer);
    Py_END_ALLOW_THREADS

    if (it->hFind == INVALID_HANDLE_VALUE) {
        if (GetLastError () != ERROR_FILE_NOT_FOUND) {
            Py_DECREF (it);
            return PyErr_SetFromWindowsErr (GetLastError());
        }
        it->empty = TRUE;
    }
    return (PyObject *)it;
}

static PyMethodDef fileutils_methods[] = {
    {"get_file_iterator", fileutils_file_iterator, METH_VARARGS, "Get File Iterator"},
    {NULL, NULL, 0, NULL}
};
static struct PyModuleDef fileutils_module = {
    PyModuleDef_HEAD_INIT,
    "fileutils",
    "This module offers lots of useful file utils2",
    -1,
    fileutils_methods
};

PyMODINIT_FUNC
PyInit_fileutils (void)
{
PyObject *module;
PyDateTime_IMPORT;

    FindFileIterator_Type.tp_new = PyType_GenericNew;
    if (PyType_Ready(&FindFileIterator_Type) < 0)
        return NULL;

    module = PyModule_Create (&fileutils_module);
    if (module == NULL)
        return NULL;

    return module;
}
