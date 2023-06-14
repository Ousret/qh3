#define PY_SSIZE_T_CLEAN

#include <Python.h>
#include <stdint.h>

#define MODULE_NAME "aioquic._buffer"

// https://foss.heptapod.net/pypy/pypy/-/issues/3770
#ifndef Py_None
#define Py_None (&_Py_NoneStruct)
#endif

static PyObject *BufferReadError;
static PyObject *BufferWriteError;

typedef struct {
    PyObject_HEAD
    uint8_t *base;
    uint8_t *end;
    uint8_t *pos;
} BufferObject;

static PyObject *BufferType;

#define CHECK_READ_BOUNDS(self, len) \
    if (len < 0 || self->pos + len > self->end) { \
        PyErr_SetString(BufferReadError, "Read out of bounds"); \
        return NULL; \
    }

#define CHECK_WRITE_BOUNDS(self, len) \
    if (self->pos + len > self->end) { \
        PyErr_SetString(BufferWriteError, "Write out of bounds"); \
        return NULL; \
    }

static int
Buffer_init(BufferObject *self, PyObject *args, PyObject *kwargs)
{
    const char *kwlist[] = {"capacity", "data", NULL};
    Py_ssize_t capacity = 0;
    const unsigned char *data = NULL;
    Py_ssize_t data_len = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|ny#", (char**)kwlist, &capacity, &data, &data_len))
        return -1;

    if (data != NULL) {
        self->base = malloc(data_len);
        self->end = self->base + data_len;
        memcpy(self->base, data, data_len);
    } else {
        self->base = malloc(capacity);
        self->end = self->base + capacity;
    }
    self->pos = self->base;
    return 0;
}

static void
Buffer_dealloc(BufferObject *self)
{
    free(self->base);
    PyTypeObject *tp = Py_TYPE(self);
    freefunc free = PyType_GetSlot(tp, Py_tp_free);
    free(self);
    Py_DECREF(tp);
}

static PyObject *
Buffer_data_slice(BufferObject *self, PyObject *args)
{
    Py_ssize_t start, stop;
    if (!PyArg_ParseTuple(args, "nn", &start, &stop))
        return NULL;

    if (start < 0 || self->base + start > self->end ||
        stop < 0 || self->base + stop > self->end ||
        stop < start) {
        PyErr_SetString(BufferReadError, "Read out of bounds");
        return NULL;
    }

    return PyBytes_FromStringAndSize((const char*)(self->base + start), (stop - start));
}

static PyObject *
Buffer_eof(BufferObject *self, PyObject *args)
{
    if (self->pos == self->end)
        Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

static PyObject *
Buffer_pull_bytes(BufferObject *self, PyObject *args)
{
    Py_ssize_t len;
    if (!PyArg_ParseTuple(args, "n", &len))
        return NULL;

    CHECK_READ_BOUNDS(self, len);

    PyObject *o = PyBytes_FromStringAndSize((const char*)self->pos, len);
    self->pos += len;
    return o;
}

static PyObject *
Buffer_pull_uint8(BufferObject *self, PyObject *args)
{
    CHECK_READ_BOUNDS(self, 1)

    return PyLong_FromUnsignedLong(
        (uint8_t)(*(self->pos++))
    );
}

static PyObject *
Buffer_pull_uint16(BufferObject *self, PyObject *args)
{
    CHECK_READ_BOUNDS(self, 2)

    uint16_t value = (uint16_t)(*(self->pos)) << 8 |
                     (uint16_t)(*(self->pos + 1));
    self->pos += 2;
    return PyLong_FromUnsignedLong(value);
}

static PyObject *
Buffer_pull_uint32(BufferObject *self, PyObject *args)
{
    CHECK_READ_BOUNDS(self, 4)

    uint32_t value = (uint32_t)(*(self->pos)) << 24 |
                     (uint32_t)(*(self->pos + 1)) << 16 |
                     (uint32_t)(*(self->pos + 2)) << 8 |
                     (uint32_t)(*(self->pos + 3));
    self->pos += 4;
    return PyLong_FromUnsignedLong(value);
}

static PyObject *
Buffer_pull_uint64(BufferObject *self, PyObject *args)
{
    CHECK_READ_BOUNDS(self, 8)

    uint64_t value = (uint64_t)(*(self->pos)) << 56 |
                     (uint64_t)(*(self->pos + 1)) << 48 |
                     (uint64_t)(*(self->pos + 2)) << 40 |
                     (uint64_t)(*(self->pos + 3)) << 32 |
                     (uint64_t)(*(self->pos + 4)) << 24 |
                     (uint64_t)(*(self->pos + 5)) << 16 |
                     (uint64_t)(*(self->pos + 6)) << 8 |
                     (uint64_t)(*(self->pos + 7));
    self->pos += 8;
    return PyLong_FromUnsignedLongLong(value);
}

static PyObject *
Buffer_pull_uint_var(BufferObject *self, PyObject *args)
{
    uint64_t value;
    CHECK_READ_BOUNDS(self, 1)
    switch (*(self->pos) >> 6) {
    case 0:
        value = *(self->pos++) & 0x3F;
        break;
    case 1:
        CHECK_READ_BOUNDS(self, 2)
        value = (uint16_t)(*(self->pos) & 0x3F) << 8 |
                (uint16_t)(*(self->pos + 1));
        self->pos += 2;
        break;
    case 2:
        CHECK_READ_BOUNDS(self, 4)
        value = (uint32_t)(*(self->pos) & 0x3F) << 24 |
                (uint32_t)(*(self->pos + 1)) << 16 |
                (uint32_t)(*(self->pos + 2)) << 8 |
                (uint32_t)(*(self->pos + 3));
        self->pos += 4;
        break;
    default:
        CHECK_READ_BOUNDS(self, 8)
        value = (uint64_t)(*(self->pos) & 0x3F) << 56 |
                (uint64_t)(*(self->pos + 1)) << 48 |
                (uint64_t)(*(self->pos + 2)) << 40 |
                (uint64_t)(*(self->pos + 3)) << 32 |
                (uint64_t)(*(self->pos + 4)) << 24 |
                (uint64_t)(*(self->pos + 5)) << 16 |
                (uint64_t)(*(self->pos + 6)) << 8 |
                (uint64_t)(*(self->pos + 7));
        self->pos += 8;
        break;
    }
    return PyLong_FromUnsignedLongLong(value);
}

static PyObject *
Buffer_push_bytes(BufferObject *self, PyObject *args)
{
    const unsigned char *data;
    Py_ssize_t data_len;
    if (!PyArg_ParseTuple(args, "y#", &data, &data_len))
        return NULL;

    CHECK_WRITE_BOUNDS(self, data_len)

    memcpy(self->pos, data, data_len);
    self->pos += data_len;
    Py_RETURN_NONE;
}

static PyObject *
Buffer_push_uint8(BufferObject *self, PyObject *args)
{
    uint8_t value;
    if (!PyArg_ParseTuple(args, "B", &value))
        return NULL;

    CHECK_WRITE_BOUNDS(self, 1)

    *(self->pos++) = value;
    Py_RETURN_NONE;
}

static PyObject *
Buffer_push_uint16(BufferObject *self, PyObject *args)
{
    uint16_t value;
    if (!PyArg_ParseTuple(args, "H", &value))
        return NULL;

    CHECK_WRITE_BOUNDS(self, 2)

    *(self->pos++) = (value >> 8);
    *(self->pos++) = value;
    Py_RETURN_NONE;
}

static PyObject *
Buffer_push_uint32(BufferObject *self, PyObject *args)
{
    uint32_t value;
    if (!PyArg_ParseTuple(args, "I", &value))
        return NULL;

    CHECK_WRITE_BOUNDS(self, 4)
    *(self->pos++) = (value >> 24);
    *(self->pos++) = (value >> 16);
    *(self->pos++) = (value >> 8);
    *(self->pos++) = value;
    Py_RETURN_NONE;
}

static PyObject *
Buffer_push_uint64(BufferObject *self, PyObject *args)
{
    uint64_t value;
    if (!PyArg_ParseTuple(args, "K", &value))
        return NULL;

    CHECK_WRITE_BOUNDS(self, 8)
    *(self->pos++) = (value >> 56);
    *(self->pos++) = (value >> 48);
    *(self->pos++) = (value >> 40);
    *(self->pos++) = (value >> 32);
    *(self->pos++) = (value >> 24);
    *(self->pos++) = (value >> 16);
    *(self->pos++) = (value >> 8);
    *(self->pos++) = value;
    Py_RETURN_NONE;
}

static PyObject *
Buffer_push_uint_var(BufferObject *self, PyObject *args)
{
    uint64_t value;
    if (!PyArg_ParseTuple(args, "K", &value))
        return NULL;

    if (value <= 0x3F) {
        CHECK_WRITE_BOUNDS(self, 1)
        *(self->pos++) = value;
        Py_RETURN_NONE;
    } else if (value <= 0x3FFF) {
        CHECK_WRITE_BOUNDS(self, 2)
        *(self->pos++) = (value >> 8) | 0x40;
        *(self->pos++) = value;
        Py_RETURN_NONE;
    } else if (value <= 0x3FFFFFFF) {
        CHECK_WRITE_BOUNDS(self, 4)
        *(self->pos++) = (value >> 24) | 0x80;
        *(self->pos++) = (value >> 16);
        *(self->pos++) = (value >> 8);
        *(self->pos++) = value;
        Py_RETURN_NONE;
    } else if (value <= 0x3FFFFFFFFFFFFFFF) {
        CHECK_WRITE_BOUNDS(self, 8)
        *(self->pos++) = (value >> 56) | 0xC0;
        *(self->pos++) = (value >> 48);
        *(self->pos++) = (value >> 40);
        *(self->pos++) = (value >> 32);
        *(self->pos++) = (value >> 24);
        *(self->pos++) = (value >> 16);
        *(self->pos++) = (value >> 8);
        *(self->pos++) = value;
        Py_RETURN_NONE;
    } else {
        PyErr_SetString(PyExc_ValueError, "Integer is too big for a variable-length integer");
        return NULL;
    }
}

static PyObject *
Buffer_seek(BufferObject *self, PyObject *args)
{
    Py_ssize_t pos;
    if (!PyArg_ParseTuple(args, "n", &pos))
        return NULL;

    if (pos < 0 || self->base + pos > self->end) {
        PyErr_SetString(BufferReadError, "Seek out of bounds");
        return NULL;
    }

    self->pos = self->base + pos;
    Py_RETURN_NONE;
}

static PyObject *
Buffer_tell(BufferObject *self, PyObject *args)
{
    return PyLong_FromSsize_t(self->pos - self->base);
}

static PyMethodDef Buffer_methods[] = {
    {"data_slice", (PyCFunction)Buffer_data_slice, METH_VARARGS, ""},
    {"eof", (PyCFunction)Buffer_eof, METH_VARARGS, ""},
    {"pull_bytes", (PyCFunction)Buffer_pull_bytes, METH_VARARGS, "Pull bytes."},
    {"pull_uint8", (PyCFunction)Buffer_pull_uint8, METH_VARARGS, "Pull an 8-bit unsigned integer."},
    {"pull_uint16", (PyCFunction)Buffer_pull_uint16, METH_VARARGS, "Pull a 16-bit unsigned integer."},
    {"pull_uint32", (PyCFunction)Buffer_pull_uint32, METH_VARARGS, "Pull a 32-bit unsigned integer."},
    {"pull_uint64", (PyCFunction)Buffer_pull_uint64, METH_VARARGS, "Pull a 64-bit unsigned integer."},
    {"pull_uint_var", (PyCFunction)Buffer_pull_uint_var, METH_VARARGS, "Pull a QUIC variable-length unsigned integer."},
    {"push_bytes", (PyCFunction)Buffer_push_bytes, METH_VARARGS, "Push bytes."},
    {"push_uint8", (PyCFunction)Buffer_push_uint8, METH_VARARGS, "Push an 8-bit unsigned integer."},
    {"push_uint16", (PyCFunction)Buffer_push_uint16, METH_VARARGS, "Push a 16-bit unsigned integer."},
    {"push_uint32", (PyCFunction)Buffer_push_uint32, METH_VARARGS, "Push a 32-bit unsigned integer."},
    {"push_uint64", (PyCFunction)Buffer_push_uint64, METH_VARARGS, "Push a 64-bit unsigned integer."},
    {"push_uint_var", (PyCFunction)Buffer_push_uint_var, METH_VARARGS, "Push a QUIC variable-length unsigned integer."},
    {"seek", (PyCFunction)Buffer_seek, METH_VARARGS, ""},
    {"tell", (PyCFunction)Buffer_tell, METH_VARARGS, ""},
    {NULL}
};

static PyObject*
Buffer_capacity_getter(BufferObject* self, void *closure) {
    return PyLong_FromSsize_t(self->end - self->base);
}

static PyObject*
Buffer_data_getter(BufferObject* self, void *closure) {
    return PyBytes_FromStringAndSize((const char*)self->base, self->pos - self->base);
}

static PyGetSetDef Buffer_getset[] = {
    {"capacity", (getter) Buffer_capacity_getter, NULL, "", NULL },
    {"data", (getter) Buffer_data_getter, NULL, "", NULL },
    {NULL}
};

static PyType_Slot BufferType_slots[] = {
    {Py_tp_dealloc, Buffer_dealloc},
    {Py_tp_methods, Buffer_methods},
    {Py_tp_doc, "Buffer objects"},
    {Py_tp_getset, Buffer_getset},
    {Py_tp_init, Buffer_init},
    {0, 0},
};

static PyType_Spec BufferType_spec = {
    MODULE_NAME ".Buffer",
    sizeof(BufferObject),
    0,
    Py_TPFLAGS_DEFAULT,
    BufferType_slots
};


static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    MODULE_NAME,                        /* m_name */
    "Serialization utilities.",         /* m_doc */
    -1,                                 /* m_size */
    NULL,                               /* m_methods */
    NULL,                               /* m_reload */
    NULL,                               /* m_traverse */
    NULL,                               /* m_clear */
    NULL,                               /* m_free */
};


PyMODINIT_FUNC
PyInit__buffer(void)
{
    PyObject* m;

    m = PyModule_Create(&moduledef);
    if (m == NULL)
        return NULL;

    BufferReadError = PyErr_NewException(MODULE_NAME ".BufferReadError", PyExc_ValueError, NULL);
    Py_INCREF(BufferReadError);
    PyModule_AddObject(m, "BufferReadError", BufferReadError);

    BufferWriteError = PyErr_NewException(MODULE_NAME ".BufferWriteError", PyExc_ValueError, NULL);
    Py_INCREF(BufferWriteError);
    PyModule_AddObject(m, "BufferWriteError", BufferWriteError);

    BufferType = PyType_FromSpec(&BufferType_spec);
    if (BufferType == NULL)
        return NULL;

    PyObject *o = PyType_FromSpec(&BufferType_spec);
    if (o == NULL)
        return NULL;

    PyModule_AddObject(m, "Buffer", o);

    return m;
}
