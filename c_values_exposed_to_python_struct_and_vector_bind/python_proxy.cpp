#include "python_proxy.hpp"
#include "value_interface.hpp"
#include <vector>

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// ------------------------------------------------------------
// Helper: Calculate struct size from StructInfo
// Used by VectorProxy_append_new() to allocate struct instances
// ------------------------------------------------------------
static std::size_t calculate_struct_size(const StructInfo *sinfo)
{
    if (!sinfo || sinfo->fields.empty())
        return 0;

    const FieldInfo &last = sinfo->fields.back();
    std::size_t field_size = 0;

    switch (last.type)
    {
    case ValueType::Int:
        field_size = sizeof(int);
        break;
    case ValueType::Float:
        field_size = sizeof(float);
        break;
    case ValueType::Bool:
        field_size = sizeof(ByteBool);
        break;
    case ValueType::String:
        field_size = sizeof(std::string);
        break;
    case ValueType::Struct:
    {
        const StructInfo *nested = static_cast<const StructInfo *>(last.type_meta);
        field_size = nested ? calculate_struct_size(nested) : 0;
        break;
    }
    case ValueType::Vector:
        field_size = sizeof(std::vector<int>); // All vectors same size
        break;
    default:
        field_size = 0;
        break;
    }

    return last.offset + field_size;
}

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
    if (!name)
    {
        PyErr_SetString(PyExc_TypeError, "Attribute name must be a string");
        return nullptr;
    }

    // Use get_value_raw() to get any BoundValue (scalar, struct, or vector)
    BoundValue *val = PyInterface::get_value_raw(name);

    if (!val)
    {
        PyErr_Format(PyExc_AttributeError, "Unknown C++ variable '%s'", name);
        return nullptr;
    }

    // Dispatch based on actual type
    switch (val->type)
    {
    case ValueType::Struct:
    {
        auto *bs = static_cast<BoundStruct *>(val);
        // Create a wrapper that the proxy can own and delete safely
        // This prevents double-free: g_values owns original, proxy owns wrapper
        BoundStruct *wrapper = new BoundStruct(bs->name, bs->instance(), bs->info());
        return StructProxy_New(wrapper);
    }

    case ValueType::Vector:
    {
        auto *bv = static_cast<BoundVector *>(val);
        // Create a wrapper that the proxy can own and delete safely
        // This prevents double-free: g_values owns original, proxy owns wrapper
        BoundVector *wrapper = new BoundVector(bv->name, bv->raw_vector(), bv->info());
        return VectorProxy_New(wrapper);
    }

    default:
        // For scalar types, use PyBoundValue interface
        PyBoundValue *pyval = dynamic_cast<PyBoundValue *>(val);
        if (!pyval)
        {
            PyErr_Format(PyExc_RuntimeError, "Internal error: scalar type not PyBoundValue");
            return nullptr;
        }
        return pyval->to_python();
    }
}

// ------------------------------------------------------------
// __setattr__(self, name, value)
// Called when Python writes: cpp.health = 10
// ------------------------------------------------------------
static int cppproxy_setattro(PyObject *, PyObject *attr, PyObject *value)
{
    const char *name = PyUnicode_AsUTF8(attr);
    if (!name)
    {
        PyErr_SetString(PyExc_TypeError, "Attribute name must be a string");
        return -1;
    }

    BoundValue *val = PyInterface::get_value_raw(name);

    if (!val)
    {
        PyErr_Format(PyExc_AttributeError, "Unknown C++ variable '%s'", name);
        return -1;
    }

    // Structs and vectors cannot be reassigned (only modified via their proxy)
    if (val->type == ValueType::Struct || val->type == ValueType::Vector)
    {
        PyErr_Format(PyExc_TypeError, "Cannot reassign struct or vector '%s'", name);
        return -1;
    }

    // For scalar types
    PyBoundValue *pyval = dynamic_cast<PyBoundValue *>(val);
    if (!pyval)
    {
        PyErr_Format(PyExc_RuntimeError, "Internal error: scalar type not PyBoundValue");
        return -1;
    }

    if (!pyval->from_python(value))
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
    PyVarObject_HEAD_INIT(nullptr, 0) "cppbridge.CppProxy", // tp_name
    sizeof(CppProxyObject),                                 // tp_basicsize
    0,                                                      // tp_itemsize
    0,                                                      // tp_dealloc
    0,                                                      // tp_vectorcall_offset
    0,                                                      // tp_getattr
    0,                                                      // tp_setattr
    0,                                                      // tp_as_async
    0,                                                      // tp_repr
    0,                                                      // tp_as_number
    0,                                                      // tp_as_sequence
    0,                                                      // tp_as_mapping
    0,                                                      // tp_hash
    0,                                                      // tp_call
    0,                                                      // tp_str
    cppproxy_getattro,                                      // tp_getattro
    cppproxy_setattro,                                      // tp_setattro
    0,                                                      // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                                     // tp_flags
    "C++ variable proxy"                                    // tp_doc
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
// Destructor for StructProxy
// ------------------------------------------------------------
static void StructProxy_dealloc(PyObject *self)
{
    StructProxyObject *proxy = (StructProxyObject *)self;

    // Delete the BoundStruct wrapper
    delete proxy->bound;

    // Free the Python object
    PyObject_Del(self);
}

// ------------------------------------------------------------
// __getattr__
// Called when Python does:  cpp.player.health
// ------------------------------------------------------------
static PyObject *StructProxy_getattro(PyObject *self, PyObject *attr)
{
    StructProxyObject *proxy = (StructProxyObject *)self;

    const char *name = PyUnicode_AsUTF8(attr);
    if (!name)
    {
        PyErr_SetString(PyExc_TypeError, "Field name must be a string");
        return nullptr;
    }
    const FieldInfo *field = proxy->bound->get_field(name);

    if (!field)
    {
        PyErr_Format(PyExc_AttributeError, "Unknown field '%s'", name);
        return nullptr;
    }

    void *fieldPtr = proxy->bound->get_field_ptr(field);

    // Handle directly based on field type
    switch (field->type)
    {
    case ValueType::Int:
        return PyLong_FromLong(*static_cast<int *>(fieldPtr));

    case ValueType::Float:
        return PyFloat_FromDouble(*static_cast<float *>(fieldPtr));

    case ValueType::Bool:
    {
        ByteBool b = *static_cast<ByteBool *>(fieldPtr);
        return PyBool_FromLong((b != FALSE_BYTE) ? 1 : 0);
    }

    case ValueType::String:
        return PyUnicode_FromString(static_cast<std::string *>(fieldPtr)->c_str());

    case ValueType::Struct:
    {
        const StructInfo *sinfo = static_cast<const StructInfo *>(field->type_meta);
        BoundStruct *bstruct = new BoundStruct(field->name, fieldPtr, sinfo);
        return StructProxy_New(bstruct);
    }

    case ValueType::Vector:
    {
        const VectorInfo *vinfo = static_cast<const VectorInfo *>(field->type_meta);
        BoundVector *bvec = new BoundVector(field->name, fieldPtr, vinfo);
        return VectorProxy_New(bvec);
    }

    default:
        PyErr_SetString(PyExc_RuntimeError, "Unsupported field type");
        return nullptr;
    }
}

// ------------------------------------------------------------
// __setattr__
// Called when Python does:  cpp.player.health = 10
// ------------------------------------------------------------
static int StructProxy_setattro(PyObject *self, PyObject *attr, PyObject *value)
{
    StructProxyObject *proxy = (StructProxyObject *)self;

    const char *name = PyUnicode_AsUTF8(attr);
    if (!name)
    {
        PyErr_SetString(PyExc_TypeError, "Field name must be a string");
        return -1;
    }
    const FieldInfo *field = proxy->bound->get_field(name);

    if (!field)
    {
        PyErr_Format(PyExc_AttributeError, "Unknown field '%s'", name);
        return -1;
    }

    void *fieldPtr = proxy->bound->get_field_ptr(field);

    // Handle assignment based on field type
    switch (field->type)
    {
    case ValueType::Int:
        if (!PyLong_Check(value))
        {
            PyErr_SetString(PyExc_TypeError, "Expected int");
            return -1;
        }
        *static_cast<int *>(fieldPtr) = (int)PyLong_AsLong(value);
        return 0;

    case ValueType::Float:
        if (!PyFloat_Check(value) && !PyLong_Check(value))
        {
            PyErr_SetString(PyExc_TypeError, "Expected float");
            return -1;
        }
        *static_cast<float *>(fieldPtr) = (float)PyFloat_AsDouble(value);
        return 0;

    case ValueType::Bool:
    {
        int truth = PyObject_IsTrue(value);
        if (truth < 0)
        {
            PyErr_SetString(PyExc_TypeError, "Expected bool");
            return -1;
        }
        *static_cast<ByteBool *>(fieldPtr) = (truth != 0) ? TRUE_BYTE : FALSE_BYTE;
        return 0;
    }

    case ValueType::String:
        if (!PyUnicode_Check(value))
        {
            PyErr_SetString(PyExc_TypeError, "Expected string");
            return -1;
        }
        {
            const char *str = PyUnicode_AsUTF8(value);
            if (!str)
                return -1;
            *static_cast<std::string *>(fieldPtr) = str; // std::string copies the content
        }
        return 0;

    case ValueType::Struct:
    case ValueType::Vector:
        PyErr_SetString(PyExc_TypeError, "Cannot reassign struct or vector field");
        return -1;

    default:
        PyErr_SetString(PyExc_RuntimeError, "Unsupported field type");
        return -1;
    }
}

// ------------------------------------------------------------
// __len__
// Returns the number of fields in the struct
// Called when Python does: len(cpp.player)
// ------------------------------------------------------------
static Py_ssize_t StructProxy_len(PyObject *self)
{
    StructProxyObject *proxy = (StructProxyObject *)self;

    if (!proxy->bound || !proxy->bound->info())
        return 0;

    return static_cast<Py_ssize_t>(proxy->bound->info()->fields.size());
}

// ------------------------------------------------------------
// Sequence methods for StructProxy
// Enables len() to work on struct proxies
// ------------------------------------------------------------
static PySequenceMethods StructProxy_sequence_methods = {
    StructProxy_len, // sq_length
    0,               // sq_concat
    0,               // sq_repeat
    0,               // sq_item
    0,               // sq_slice
    0,               // sq_ass_item
    0,               // sq_ass_slice
    0,               // sq_contains
    0,               // sq_inplace_concat
    0,               // sq_inplace_repeat
};

// ------------------------------------------------------------
// StructProxy Python type definition
// ------------------------------------------------------------
PyTypeObject StructProxyType = {
    PyVarObject_HEAD_INIT(nullptr, 0) "cpp.StructProxy", // tp_name
    sizeof(StructProxyObject),                           // tp_basicsize
    0,                                                   // tp_itemsize
    StructProxy_dealloc,                                 // tp_dealloc
    0,                                                   // tp_vectorcall_offset
    0,                                                   // tp_getattr
    0,                                                   // tp_setattr
    0,                                                   // tp_as_async
    0,                                                   // tp_repr
    0,                                                   // tp_as_number
    &StructProxy_sequence_methods,                       // tp_as_sequence (NEW!)
    0,                                                   // tp_as_mapping
    0,                                                   // tp_hash
    0,                                                   // tp_call
    0,                                                   // tp_str
    StructProxy_getattro,                                // tp_getattro
    StructProxy_setattro,                                // tp_setattro
    0,                                                   // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                                  // tp_flags
    "Proxy for C++ struct"                               // tp_doc
};

// ------------------------------------------------------------
// Create a new StructProxy instance
// ------------------------------------------------------------
PyObject *StructProxy_New(BoundStruct *bound)
{
    StructProxyObject *obj =
        PyObject_New(StructProxyObject, &StructProxyType);

    if (!obj)
    {
        PyErr_NoMemory();
        return nullptr;
    }

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
// Destructor for VectorProxy
// ------------------------------------------------------------
static void VectorProxy_dealloc(PyObject *self)
{
    VectorProxyObject *proxy = (VectorProxyObject *)self;

    // Delete the BoundVector wrapper
    delete proxy->bound;

    // Free the Python object
    PyObject_Del(self);
}

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

    Py_ssize_t size = static_cast<Py_ssize_t>(proxy->bound->size());

    // Support negative indexing
    if (index < 0)
        index += size;

    if (index < 0 || index >= size)
    {
        PyErr_SetString(PyExc_IndexError, "Vector index out of range");
        return nullptr;
    }

    void *elemPtr = proxy->bound->element_ptr(index);
    const VectorInfo *info = proxy->bound->info();

    // Handle directly based on element type
    switch (info->element_type)
    {
    case ValueType::Int:
        return PyLong_FromLong(*static_cast<int *>(elemPtr));

    case ValueType::Float:
        return PyFloat_FromDouble(*static_cast<float *>(elemPtr));

    case ValueType::Bool:
    {
        ByteBool b = *static_cast<ByteBool *>(elemPtr);
        return PyBool_FromLong((b != FALSE_BYTE) ? 1 : 0);
    }

    case ValueType::String:
        return PyUnicode_FromString(static_cast<std::string *>(elemPtr)->c_str());

    case ValueType::Struct:
    {
        const StructInfo *sinfo = static_cast<const StructInfo *>(info->element_meta);
        // Use parent + index constructor instead of raw pointer (Issue 26 fix)
        BoundStruct *bstruct = new BoundStruct(
            proxy->bound->name,
            proxy->bound,                    // Parent vector
            static_cast<std::size_t>(index), // Element index
            sinfo);
        return StructProxy_New(bstruct);
    }

    case ValueType::Vector:
    {
        const VectorInfo *vinfo = static_cast<const VectorInfo *>(info->element_meta);
        // Use parent + index constructor instead of raw pointer (Issue 26 fix)
        BoundVector *bvec = new BoundVector(
            proxy->bound->name,
            proxy->bound,                    // Parent vector
            static_cast<std::size_t>(index), // Element index
            vinfo);
        return VectorProxy_New(bvec);
    }

    default:
        PyErr_SetString(PyExc_RuntimeError, "Unsupported element type");
        return nullptr;
    }
}

// ------------------------------------------------------------
// __setitem__
// ------------------------------------------------------------
static int VectorProxy_setitem(PyObject *self, Py_ssize_t index, PyObject *value)
{
    VectorProxyObject *proxy = (VectorProxyObject *)self;

    Py_ssize_t size = static_cast<Py_ssize_t>(proxy->bound->size());

    // Support negative indexing
    if (index < 0)
        index += size;

    if (index < 0 || index >= size)
    {
        PyErr_SetString(PyExc_IndexError, "Vector index out of range");
        return -1;
    }

    void *elemPtr = proxy->bound->element_ptr(index);
    const VectorInfo *info = proxy->bound->info();

    // Handle assignment based on element type
    switch (info->element_type)
    {
    case ValueType::Int:
        if (!PyLong_Check(value))
        {
            PyErr_SetString(PyExc_TypeError, "Expected int");
            return -1;
        }
        *static_cast<int *>(elemPtr) = (int)PyLong_AsLong(value);
        return 0;

    case ValueType::Float:
        if (!PyFloat_Check(value) && !PyLong_Check(value))
        {
            PyErr_SetString(PyExc_TypeError, "Expected float");
            return -1;
        }
        *static_cast<float *>(elemPtr) = (float)PyFloat_AsDouble(value);
        return 0;

    case ValueType::Bool:
    {
        int truth = PyObject_IsTrue(value);
        if (truth < 0)
        {
            PyErr_SetString(PyExc_TypeError, "Expected bool");
            return -1;
        }
        *static_cast<ByteBool *>(elemPtr) = (truth != 0) ? TRUE_BYTE : FALSE_BYTE;
        return 0;
    }

    case ValueType::String:
        if (!PyUnicode_Check(value))
        {
            PyErr_SetString(PyExc_TypeError, "Expected string");
            return -1;
        }
        {
            const char *str = PyUnicode_AsUTF8(value);
            if (!str)
                return -1;
            *static_cast<std::string *>(elemPtr) = str; // std::string copies the content
        }
        return 0;

    case ValueType::Struct:
    case ValueType::Vector:
        PyErr_SetString(PyExc_TypeError, "Cannot reassign struct or vector element");
        return -1;

    default:
        PyErr_SetString(PyExc_RuntimeError, "Unsupported element type");
        return -1;
    }
}

// ------------------------------------------------------------
// append_new() - for struct vectors, create a default instance
// ------------------------------------------------------------
static PyObject *VectorProxy_append_new(PyObject *self, PyObject *args)
{
    (void)args;
    auto *proxy = reinterpret_cast<VectorProxyObject *>(self);
    BoundVector *vec = proxy->bound;
    const VectorInfo *info = vec->info();

    // Only works for struct element types
    if (info->element_type != ValueType::Struct)
    {
        PyErr_SetString(PyExc_TypeError, "append_new() only works for vectors of structs");
        return nullptr;
    }

    const StructInfo *sinfo = static_cast<const StructInfo *>(info->element_meta);

    // Calculate struct size using helper function
    std::size_t struct_size = calculate_struct_size(sinfo);

    // Allocate zero-initialized memory for the struct
    void *new_instance = ::operator new(struct_size);
    std::memset(new_instance, 0, struct_size);

    // Initialize string fields properly
    for (const auto &field : sinfo->fields)
    {
        if (field.type == ValueType::String)
        {
            void *fieldPtr = reinterpret_cast<char *>(new_instance) + field.offset;
            new (fieldPtr) std::string();
        }
    }

    // Append to vector
    vec->append_from_cpp(new_instance);

    // Get the last element (the one we just added)
    std::size_t last_idx = vec->size() - 1;

    // Clean up temporary allocation
    // Destroy string fields before freeing
    for (const auto &field : sinfo->fields)
    {
        if (field.type == ValueType::String)
        {
            void *fieldPtr = reinterpret_cast<char *>(new_instance) + field.offset;
            reinterpret_cast<std::string *>(fieldPtr)->~basic_string();
        }
    }
    ::operator delete(new_instance);

    // Return a proxy to the newly added element (using parent + index for Issue 26 fix)
    BoundStruct *bstruct = new BoundStruct(vec->name, vec, last_idx, sinfo);
    return StructProxy_New(bstruct);
}

// ------------------------------------------------------------
// append_new_vector() - for vector-of-vector, create a new empty inner vector
// Supports inner vectors of any type: int, float, bool, string, struct, vector
// ------------------------------------------------------------
static PyObject *VectorProxy_append_new_vector(PyObject *self, PyObject *args)
{
    (void)args;
    auto *proxy = reinterpret_cast<VectorProxyObject *>(self);
    BoundVector *vec = proxy->bound;
    const VectorInfo *info = vec->info();

    // Only works for vector element types
    if (info->element_type != ValueType::Vector)
    {
        PyErr_SetString(PyExc_TypeError, "append_new_vector() only works for vectors of vectors");
        return nullptr;
    }

    const VectorInfo *inner_info = static_cast<const VectorInfo *>(info->element_meta);

    // Create a new empty inner vector based on the inner element type
    // Use a generic approach that works for all types via void* and append functions

    // Get the append function from inner_info
    if (!inner_info->append_fn)
    {
        PyErr_SetString(PyExc_RuntimeError, "Inner vector has no append function");
        return nullptr;
    }

    if (inner_info->create_empty_vec_fn && inner_info->destroy_vec_fn)
    {
        void *temp_vec = inner_info->create_empty_vec_fn();
        if (!temp_vec)
        {
            PyErr_SetString(PyExc_RuntimeError, "Failed to create inner vector");
            return nullptr;
        }
        vec->append_from_cpp(temp_vec);
        inner_info->destroy_vec_fn(temp_vec);
    }
    else
    {
        switch (inner_info->element_type)
        {
        case ValueType::Int:
        {
            std::vector<int> new_inner_vec;
            vec->append_from_cpp(&new_inner_vec);
            break;
        }

        case ValueType::Float:
        {
            std::vector<float> new_inner_vec;
            vec->append_from_cpp(&new_inner_vec);
            break;
        }

        case ValueType::Bool:
        {
            std::vector<ByteBool> new_inner_vec;
            vec->append_from_cpp(&new_inner_vec);
            break;
        }

        case ValueType::String:
        {
            std::vector<std::string> new_inner_vec;
            vec->append_from_cpp(&new_inner_vec);
            break;
        }

        default:
            PyErr_SetString(PyExc_TypeError, "Unsupported inner vector element type");
            return nullptr;
        }
    }

    // Get the last element (the one we just added)
    std::size_t last_idx = vec->size() - 1;

    // Return a proxy to the newly added inner vector (using parent + index for Issue 26 fix)
    BoundVector *bvec = new BoundVector(vec->name, vec, last_idx, inner_info);
    return VectorProxy_New(bvec);
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
        if (!utf8)
        {
            return nullptr;
        }
        const char *s = PyBytes_AsString(utf8);
        if (!s)
        {
            Py_DECREF(utf8);
            return nullptr;
        }
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
        vec->append_from_cpp(inner_raw); // FIXED: pass pointer directly, not address
        break;
    }

    default:
        PyErr_SetString(PyExc_TypeError, "Unsupported vector element type");
        return nullptr;
    }

    Py_RETURN_NONE;
}

// ------------------------------------------------------------
// ============================================================================
// SECTION 3.5 — VectorProxy Iterator
// ============================================================================

// Iterator object for VectorProxy
typedef struct
{
    PyObject_HEAD PyObject *vector; // Reference to the VectorProxy object
    std::size_t index;              // Current iteration index
} VectorIteratorObject;

// Iterator destructor
static void VectorIterator_dealloc(PyObject *self)
{
    VectorIteratorObject *it = (VectorIteratorObject *)self;
    Py_XDECREF(it->vector);
    PyObject_Del(self);
}

// __iter__ on iterator returns itself
static PyObject *VectorIterator_iter(PyObject *self)
{
    Py_INCREF(self);
    return self;
}

// __next__ implementation
static PyObject *VectorIterator_next(PyObject *self)
{
    VectorIteratorObject *it = (VectorIteratorObject *)self;
    VectorProxyObject *proxy = (VectorProxyObject *)it->vector;

    // Check if we've reached the end
    if (it->index >= proxy->bound->size())
    {
        PyErr_SetNone(PyExc_StopIteration);
        return nullptr;
    }

    // Get the element at current index
    PyObject *item = VectorProxy_getitem((PyObject *)proxy, (Py_ssize_t)it->index);

    if (item)
        it->index++; // Increment only on success

    return item;
}

// VectorIterator type definition
PyTypeObject VectorIteratorType = {
    PyVarObject_HEAD_INIT(nullptr, 0) "cpp.VectorIterator", // tp_name
    sizeof(VectorIteratorObject),                           // tp_basicsize
    0,                                                      // tp_itemsize
    VectorIterator_dealloc,                                 // tp_dealloc
    0,                                                      // tp_vectorcall_offset
    0,                                                      // tp_getattr
    0,                                                      // tp_setattr
    0,                                                      // tp_as_async
    0,                                                      // tp_repr
    0,                                                      // tp_as_number
    0,                                                      // tp_as_sequence
    0,                                                      // tp_as_mapping
    0,                                                      // tp_hash
    0,                                                      // tp_call
    0,                                                      // tp_str
    0,                                                      // tp_getattro
    0,                                                      // tp_setattro
    0,                                                      // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                                     // tp_flags
    "Iterator for C++ vector",                              // tp_doc
    0,                                                      // tp_traverse
    0,                                                      // tp_clear
    0,                                                      // tp_richcompare
    0,                                                      // tp_weaklistoffset
    VectorIterator_iter,                                    // tp_iter
    VectorIterator_next,                                    // tp_iternext
    0,                                                      // tp_methods
};

// VectorProxy __iter__ implementation
static PyObject *VectorProxy_iter(PyObject *self)
{
    VectorIteratorObject *it = PyObject_New(VectorIteratorObject, &VectorIteratorType);
    if (!it)
        return nullptr;

    Py_INCREF(self); // Hold reference to vector
    it->vector = self;
    it->index = 0;

    return (PyObject *)it;
}

// ============================================================================
// SECTION 3.4 — VectorProxy Methods
// ============================================================================

// VectorProxy methods table
// ------------------------------------------------------------
static PyMethodDef VectorProxy_methods[] = {
    {"append", VectorProxy_append, METH_O, "Append an element"},
    {"append_new", VectorProxy_append_new, METH_NOARGS, "Append a new default struct instance and return it"},
    {"append_new_vector", VectorProxy_append_new_vector, METH_NOARGS, "Append a new empty vector and return it"},
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
    PyVarObject_HEAD_INIT(nullptr, 0) "cpp.VectorProxy", // tp_name
    sizeof(VectorProxyObject),                           // tp_basicsize
    0,                                                   // tp_itemsize
    VectorProxy_dealloc,                                 // tp_dealloc
    0,                                                   // tp_vectorcall_offset
    0,                                                   // tp_getattr
    0,                                                   // tp_setattr
    0,                                                   // tp_as_async
    0,                                                   // tp_repr
    0,                                                   // tp_as_number
    &VectorProxy_seq,                                    // tp_as_sequence
    0,                                                   // tp_as_mapping
    0,                                                   // tp_hash
    0,                                                   // tp_call
    0,                                                   // tp_str
    0,                                                   // tp_getattro
    0,                                                   // tp_setattro
    0,                                                   // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                                  // tp_flags
    "Proxy for C++ vector",                              // tp_doc
    0,                                                   // tp_traverse
    0,                                                   // tp_clear
    0,                                                   // tp_richcompare
    0,                                                   // tp_weaklistoffset
    VectorProxy_iter,                                    // tp_iter (NEW!)
    0,                                                   // tp_iternext
    VectorProxy_methods,                                 // tp_methods
};
// ------------------------------------------------------------
// Create a new VectorProxy instance
// ------------------------------------------------------------
PyObject *VectorProxy_New(BoundVector *bound)
{
    VectorProxyObject *obj =
        PyObject_New(VectorProxyObject, &VectorProxyType);

    if (!obj)
    {
        PyErr_NoMemory();
        return nullptr;
    }

    obj->bound = bound;
    return (PyObject *)obj;
}
