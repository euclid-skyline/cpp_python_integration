#include "value_interface.hpp"

// ------------------------------------------------------------
// Singleton instance pointer
// ------------------------------------------------------------
static PyObject *g_cpp_proxy_instance = nullptr;

// ------------------------------------------------------------
// C++ proxy object (empty struct, Python only uses the type)
// ------------------------------------------------------------
typedef struct
{
    PyObject_HEAD
} CppProxyObject;

// ------------------------------------------------------------
// __getattr__(self, name)
// Called when Python reads: cpp.health
// ------------------------------------------------------------
static PyObject *cppproxy_getattro(PyObject *, PyObject *attr)
{
    const char *name = PyUnicode_AsUTF8(attr);
    PyBoundValue *val = PyInterface::get_value(name);

    if (!val)
    {
        PyErr_Format(PyExc_AttributeError, "Unknown C++ variable '%s'", name);
        return nullptr;
    }

    return val->to_python();
}

// ------------------------------------------------------------
// __setattr__(self, name, value)
// Called when Python writes: cpp.health = 10
// ------------------------------------------------------------
static int cppproxy_setattro(PyObject *, PyObject *attr, PyObject *value)
{
    const char *name = PyUnicode_AsUTF8(attr);
    PyBoundValue *val = PyInterface::get_value(name);

    if (!val)
    {
        PyErr_Format(PyExc_AttributeError, "Unknown C++ variable '%s'", name);
        return -1;
    }

    if (!val->from_python(value))
    {
        PyErr_Format(PyExc_TypeError, "Type mismatch for '%s'", name);
        return -1;
    }

    return 0;
}

// ------------------------------------------------------------
// Python type definition
// ------------------------------------------------------------
static PyTypeObject CppProxyType = {
    PyVarObject_HEAD_INIT(NULL, 0) "cppbridge.CppProxy", // tp_name
    sizeof(CppProxyObject),                              // tp_basicsize
    0,                                                   // tp_itemsize
    0,                                                   // tp_dealloc
    0,                                                   // tp_vectorcall_offset
    0,                                                   // tp_getattr
    0,                                                   // tp_setattr
    0,                                                   // tp_as_async
    0,                                                   // tp_repr
    0,                                                   // tp_as_number
    0,                                                   // tp_as_sequence
    0,                                                   // tp_as_mapping
    0,                                                   // tp_hash
    0,                                                   // tp_call
    0,                                                   // tp_str
    cppproxy_getattro,                                   // tp_getattro
    cppproxy_setattro,                                   // tp_setattro
    0,                                                   // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                                  // tp_flags
    "C++ variable proxy",                                // tp_doc
};

// ------------------------------------------------------------
// Create a new instance of the proxy
// ------------------------------------------------------------
PyObject *create_cpp_proxy()
{
    // If already created → return the same instance
    if (g_cpp_proxy_instance)
    {
        Py_INCREF(g_cpp_proxy_instance);
        return g_cpp_proxy_instance;
    }

    // Initialize type
    if (PyType_Ready(&CppProxyType) < 0)
        return nullptr;

    // Create the proxy instance
    g_cpp_proxy_instance = reinterpret_cast<PyObject *>(
        PyObject_New(CppProxyObject, &CppProxyType));

    return g_cpp_proxy_instance;
}