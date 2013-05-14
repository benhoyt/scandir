#include <python.h>
#include <datetime.h>
#include <windows.h>
#include <tchar.h>
#include <stdio.h>

static PyTypeObject FileDataType;

static PyObject *
FileDataType_from_find_data(WIN32_FIND_DATAW *data)
{
PY_LONG_LONG size;
PyObject *v = PyStructSequence_New(&FileDataType);

    /*
    Produce a structseq object whose fields map to the data in the WIN32_FIND_DATAW
    */
    if (v == NULL)
        return NULL;

    size = (PY_LONG_LONG)data->nFileSizeHigh << 32 |
           (PY_LONG_LONG)data->nFileSizeLow;

    PyStructSequence_SET_ITEM(v, 0, PyUnicode_FromWideChar(data->cFileName, -1));
    PyStructSequence_SET_ITEM(v, 1, PyLong_FromLongLong(size));
    PyStructSequence_SET_ITEM(v, 2, PyBool_FromLong(data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));

    if (PyErr_Occurred()) {
        Py_DECREF(v);
        return NULL;
    }

    return v;
}

static PyStructSequence_Field file_data_fields[] = {
    {"name",         "file name"},
    {"size",         "how big is the file? (bytes)"},
    {"is_directory", "is the file a directory?"},
    {0}
};

static PyStructSequence_Desc file_data_desc = {
    "file_data",    /* name */
    NULL,           /* doc */
    file_data_fields,
    3
};

#define PATTERN_LEN 1024
typedef struct {
	PyObject_HEAD
	HANDLE hFind;
    wchar_t pattern[PATTERN_LEN];
	WIN32_FIND_DATAW data;
} FileIterator;

static void
ffi_dealloc(FileIterator *iterator)
{
	if (iterator->hFind != INVALID_HANDLE_VALUE)
		FindClose(iterator->hFind);
	PyObject_Del(iterator);
}

static PyObject *
ffi_iternext(PyObject *iterator)
{
PyObject *file_data;
BOOL is_empty;

	FileIterator *fi = (FileIterator *)iterator;
    is_empty = FALSE;
    memset(&fi->data, 0, sizeof(fi->data));

    /*
    Put data into the iterator's data buffer, using the state of the
    hFind handle to determine whether this is the first iteration or
    a successive one.

    If the API indicates that there are no (or no more) files, raise
    a StopIteration exception.
    */
    if (fi->hFind == NULL) {
        Py_BEGIN_ALLOW_THREADS
        fi->hFind = FindFirstFileW(fi->pattern, &fi->data);
        Py_END_ALLOW_THREADS

        if (fi->hFind == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) {
                return PyErr_SetFromWindowsErr(GetLastError());
            }
            is_empty = TRUE;
        }
    }
	else {
		BOOL ok;
		Py_BEGIN_ALLOW_THREADS
		ok = FindNextFileW(fi->hFind, &fi->data);
		Py_END_ALLOW_THREADS

        if (!ok) {
			if (GetLastError() != ERROR_NO_MORE_FILES) {
			    return PyErr_SetFromWindowsErr(GetLastError());
			}
            is_empty = TRUE;
		}
	}

    if (is_empty) {
        PyErr_SetNone(PyExc_StopIteration);
        return NULL;
    }

    file_data = FileDataType_from_find_data(&fi->data);
    if (!file_data) {
        return PyErr_SetFromWindowsErr(GetLastError());
    }
    else {
        return Py_BuildValue("O", file_data);
    }
}

PyTypeObject FileIterator_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
	"FileIterator",				        /* tp_name */
	sizeof(FileIterator),			    /* tp_basicsize */
	0,					                /* tp_itemsize */
	/* methods */
	(destructor)ffi_dealloc, 		    /* tp_dealloc */
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
	(iternextfunc)ffi_iternext,		    /* tp_iternext */
	0,					                /* tp_methods */
	0,					                /* tp_members */
	0,					                /* tp_getset */
	0,					                /* tp_base */
	0,					                /* tp_dict */
	0,					                /* tp_descr_get */
	0,					                /* tp_descr_set */
};

static PyObject*
iterfile (PyObject *self, PyObject *args)
{
PyUnicodeObject *po;
FileIterator *iterator;

    if (!PyArg_ParseTuple(args, "U", &po)) {
        return NULL;
    }

    /*
    Create and return a FileIterator object.

    Initialise it with the pattern to iterate over, an empty data block, and an empty handle,
    the latter indicating to the iternext implementation that iteration has not yet started.
    */
    iterator = PyObject_New(FileIterator, &FileIterator_Type);
    if (iterator == NULL) {
        return NULL;
    }
    wcscpy(iterator->pattern, PyUnicode_AS_UNICODE(po));
    iterator->hFind = NULL;

    return (PyObject *)iterator;
}

static PyMethodDef _iterfile_methods[] = {
    {"iterfile", iterfile, METH_VARARGS, "Get File Iterator"},
    {NULL, NULL, 0, NULL}
};
static struct PyModuleDef _iterfile_module = {
    PyModuleDef_HEAD_INIT,
    "_iterfile",
    "This module offers lots of useful file utils2",
    -1,
    _iterfile_methods
};

PyMODINIT_FUNC
PyInit__iterfile(void)
{
PyObject *module;
PyDateTime_IMPORT;

    FileIterator_Type.tp_new = PyType_GenericNew;
    if (PyType_Ready(&FileIterator_Type) < 0) {
        return NULL;
    }

    module = PyModule_Create(&_iterfile_module);
    if (module == NULL) {
        return NULL;
    }

    file_data_desc.name = "iterfile.file_data";
    PyStructSequence_InitType(&FileDataType, &file_data_desc);

    return module;
}
