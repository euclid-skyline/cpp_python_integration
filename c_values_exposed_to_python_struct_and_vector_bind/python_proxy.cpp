#include "python_proxy.hpp"
#include "value_interface.hpp"

// ============================================================================
// SECTION 1 — RootProxy (from value_interface_proxy.cpp)
// ============================================================================

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

    // [C++20 FIX] use PyInterface::get_value wrapper (added for compatibility)
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

    // [C++20 FIX] use PyInterface::get_value wrapper
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
PyTypeObject CppProxyType = {
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
    "C++ variable proxy"                                 // tp_doc
};

// ------------------------------------------------------------
// Create a new instance of the proxy
// ------------------------------------------------------------
PyObject *create_cpp_proxy()
{
    if (g_cpp_proxy_instance)
    {
        Py_INCREF(g_cpp_proxy_instance);
        return g_cpp_proxy_instance;
    }

    // [C++20 FIX] Type readiness is now centralized in module init,
    // but we keep this for backward compatibility.
    if (PyType_Ready(&CppProxyType) < 0)
        return nullptr;

    g_cpp_proxy_instance =
        reinterpret_cast<PyObject *>(PyObject_New(CppProxyObject, &CppProxyType));

    return g_cpp_proxy_instance;
}

// ============================================================================
// SECTION 2 — StructProxy (from aggregate_interface_proxy.cpp)
// ============================================================================

// Python object layout for StructProxy
typedef struct
{
    PyObject_HEAD BoundStruct *bound;
} StructProxyObject;

// ------------------------------------------------------------
// __getattr__
// Called when Python does:  cpp.player.health
// ------------------------------------------------------------
static PyObject *StructProxy_getattro(PyObject *self, PyObject *attr)
{
    StructProxyObject *proxy = (StructProxyObject *)self;

    const char *name = PyUnicode_AsUTF8(attr);
    const FieldInfo *field = proxy->bound->get_field(name);

    if (!field)
    {
        PyErr_Format(PyExc_AttributeError, "Unknown field '%s'", name);
        return nullptr;
    }

    void *fieldPtr = proxy->bound->get_field_ptr(field);

    // [C++20 FIX] wrap_field added to PyInterface
    PyBoundValue *val = PyInterface::wrap_field(field, fieldPtr);

    return val->to_python();
}

// ------------------------------------------------------------
// __setattr__
// Called when Python does:  cpp.player.health = 10
// ------------------------------------------------------------
static int StructProxy_setattro(PyObject *self, PyObject *attr, PyObject *value)
{
    StructProxyObject *proxy = (StructProxyObject *)self;

    const char *name = PyUnicode_AsUTF8(attr);
    const FieldInfo *field = proxy->bound->get_field(name);

    if (!field)
    {
        PyErr_Format(PyExc_AttributeError, "Unknown field '%s'", name);
        return -1;
    }

    void *fieldPtr = proxy->bound->get_field_ptr(field);

    // [C++20 FIX] wrap_field added to PyInterface
    PyBoundValue *val = PyInterface::wrap_field(field, fieldPtr);

    return val->from_python(value) ? 0 : -1;
}

// ------------------------------------------------------------
// StructProxy Python type definition
// ------------------------------------------------------------
PyTypeObject StructProxyType = {
    PyVarObject_HEAD_INIT(NULL, 0) "cpp.StructProxy", // tp_name
    sizeof(StructProxyObject),                        // tp_basicsize
    0,                                                // tp_itemsize
    0,                                                // tp_dealloc
    0,                                                // tp_vectorcall_offset
    0,                                                // tp_getattr
    0,                                                // tp_setattr
    0,                                                // tp_as_async
    0,                                                // tp_repr
    0,                                                // tp_as_number
    0,                                                // tp_as_sequence
    0,                                                // tp_as_mapping
    0,                                                // tp_hash
    0,                                                // tp_call
    0,                                                // tp_str
    StructProxy_getattro,                             // tp_getattro
    StructProxy_setattro,                             // tp_setattro
    0,                                                // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                               // tp_flags
    "Proxy for C++ struct"                            // tp_doc
};

// ------------------------------------------------------------
// Create a new StructProxy instance
// ------------------------------------------------------------
PyObject *StructProxy_New(BoundStruct *bound)
{
    StructProxyObject *obj =
        PyObject_New(StructProxyObject, &StructProxyType);

    obj->bound = bound;
    return (PyObject *)obj;
}

// ============================================================================
// SECTION 3 — VectorProxy (from aggregate_interface_proxy.cpp)
// ============================================================================

typedef struct
{
    PyObject_HEAD BoundVector *bound;
} VectorProxyObject;

// ------------------------------------------------------------
// __len__
// ------------------------------------------------------------
static Py_ssize_t VectorProxy_len(PyObject *self)
{
    VectorProxyObject *proxy = (VectorProxyObject *)self;
    return proxy->bound->size();
}

// ------------------------------------------------------------
// __getitem__
// ------------------------------------------------------------
static PyObject *VectorProxy_getitem(PyObject *self, Py_ssize_t index)
{
    VectorProxyObject *proxy = (VectorProxyObject *)self;

    if (index < 0 || static_cast<std::size_t>(index) >= proxy->bound->size())
    {
        PyErr_SetString(PyExc_IndexError, "Vector index out of range");
        return nullptr;
    }

    void *elemPtr = proxy->bound->element_ptr(index);

    // [C++20 FIX] wrap_vector_element added to PyInterface
    PyBoundValue *val =
        PyInterface::wrap_vector_element(proxy->bound, elemPtr);

    return val->to_python();
}

// ------------------------------------------------------------
// __setitem__
// ------------------------------------------------------------
static int VectorProxy_setitem(PyObject *self, Py_ssize_t index, PyObject *value)
{
    VectorProxyObject *proxy = (VectorProxyObject *)self;

    if (index < 0 || static_cast<std::size_t>(index) >= proxy->bound->size())
    {
        PyErr_SetString(PyExc_IndexError, "Vector index out of range");
        return -1;
    }

    void *elemPtr = proxy->bound->element_ptr(index);

    // [C++20 FIX] wrap_vector_element added to PyInterface
    PyBoundValue *val =
        PyInterface::wrap_vector_element(proxy->bound, elemPtr);

    return val->from_python(value) ? 0 : -1;
}

// ------------------------------------------------------------
// append()
// ------------------------------------------------------------
static PyObject *VectorProxy_append(PyObject *self, PyObject *value)
{
    auto *proxy = reinterpret_cast<VectorProxyObject *>(self);
    BoundVector *vec = proxy->bound;
    const VectorInfo *info = vec->info();

    switch (info->element_type)
    {
    // ------------------------------------------------------------
    // Scalar types
    // ------------------------------------------------------------
    case ValueType::Int:
    {
        if (!PyLong_Check(value))
        {
            PyErr_SetString(PyExc_TypeError, "Expected int");
            return nullptr;
        }
        int v = (int)PyLong_AsLong(value);
        vec->append_from_cpp(&v);
        break;
    }

    case ValueType::Float:
    {
        if (!PyFloat_Check(value))
        {
            PyErr_SetString(PyExc_TypeError, "Expected float");
            return nullptr;
        }
        float v = (float)PyFloat_AsDouble(value);
        vec->append_from_cpp(&v);
        break;
    }

    case ValueType::Bool:
    {
        int truth = PyObject_IsTrue(value);
        if (truth < 0)
        {
            PyErr_SetString(PyExc_TypeError, "Expected bool");
            return nullptr;
        }
        ByteBool v = (truth != 0) ? TRUE_BYTE : FALSE_BYTE;
        vec->append_from_cpp(&v);
        break;
    }

    case ValueType::String:
    {
        if (!PyUnicode_Check(value))
        {
            PyErr_SetString(PyExc_TypeError, "Expected string");
            return nullptr;
        }
        PyObject *utf8 = PyUnicode_AsUTF8String(value);
        const char *s = PyBytes_AsString(utf8);
        std::string v = s;
        Py_DECREF(utf8);
        vec->append_from_cpp(&v);
        break;
    }

    // ------------------------------------------------------------
    // Struct type
    // ------------------------------------------------------------
    case ValueType::Struct:
    {
        if (!PyObject_TypeCheck(value, &StructProxyType))
        {
            PyErr_SetString(PyExc_TypeError, "Expected StructProxy");
            return nullptr;
        }
        auto *sp = reinterpret_cast<StructProxyObject *>(value);
        BoundStruct *bs = sp->bound;
        vec->append_from_cpp(bs->instance());
        break;
    }

    // ------------------------------------------------------------
    // Vector type
    // ------------------------------------------------------------
    case ValueType::Vector:
    {
        if (!PyObject_TypeCheck(value, &VectorProxyType))
        {
            PyErr_SetString(PyExc_TypeError, "Expected VectorProxy");
            return nullptr;
        }
        auto *vp = reinterpret_cast<VectorProxyObject *>(value);
        BoundVector *inner = vp->bound;
        void *inner_raw = inner->raw_vector();
        vec->append_from_cpp(&inner_raw); // store pointer to inner vector
        break;
    }

    default:
        PyErr_SetString(PyExc_TypeError, "Unsupported vector element type");
        return nullptr;
    }

    Py_RETURN_NONE;
}

// ------------------------------------------------------------
// VectorProxy methods table
// ------------------------------------------------------------
static PyMethodDef VectorProxy_methods[] = {
    {"append", VectorProxy_append, METH_O, "Append an element"},
    {NULL, NULL, 0, NULL} // Sentinel to mark the end of the array
};

// ------------------------------------------------------------
// Sequence protocol table
// ------------------------------------------------------------
static PySequenceMethods VectorProxy_seq = {
    VectorProxy_len,     // sq_length
    0,                   // sq_concat
    0,                   // sq_repeat
    VectorProxy_getitem, // sq_item
    0,                   // was_sq_slice
    VectorProxy_setitem, // sq_ass_item
    0,                   // was_sq_ass_slice
    0,                   // sq_contains
    0,                   // sq_inplace_concat
    0                    // sq_inplace_repeat
};

// ------------------------------------------------------------
// VectorProxy Python type definition
// ------------------------------------------------------------
PyTypeObject VectorProxyType = {
    PyVarObject_HEAD_INIT(NULL, 0) "cpp.VectorProxy", // tp_name
    sizeof(VectorProxyObject),                        // tp_basicsize
    0,                                                // tp_itemsize
    0,                                                // tp_dealloc
    0,                                                // tp_vectorcall_offset
    0,                                                // tp_getattr
    0,                                                // tp_setattr
    0,                                                // tp_as_async
    0,                                                // tp_repr
    0,                                                // tp_as_number
    &VectorProxy_seq,                                 // tp_as_sequence
    0,                                                // tp_as_mapping
    0,                                                // tp_hash
    0,                                                // tp_call
    0,                                                // tp_str
    0,                                                // tp_getattro
    0,                                                // tp_setattro
    0,                                                // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                               // tp_flags
    "Proxy for C++ vector",                           // tp_doc
    0,                                                // tp_traverse
    0,                                                // tp_clear
    0,                                                // tp_richcompare
    0,                                                // tp_weaklistoffset
    0,                                                // tp_iter
    0,                                                // tp_iternext
    VectorProxy_methods,                              // tp_methods
};
// ------------------------------------------------------------
// Create a new VectorProxy instance
// ------------------------------------------------------------
PyObject *VectorProxy_New(BoundVector *bound)
{
    VectorProxyObject *obj =
        PyObject_New(VectorProxyObject, &VectorProxyType);

    obj->bound = bound;
    return (PyObject *)obj;
}
