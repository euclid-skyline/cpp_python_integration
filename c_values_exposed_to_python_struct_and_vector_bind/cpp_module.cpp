#include "python_proxy.hpp"
#include "value_interface.hpp"

// Module __getattr__ to dynamically look up C++ variables
static PyObject *cpp_module_getattr(PyObject *module, PyObject *name)
{
    // Convert name to string
    const char *attr_name = PyUnicode_AsUTF8(name);
    if (!attr_name)
        return nullptr;

    // Look up in bound C++ values
    BoundValue *val = PyInterface::get_value_raw(attr_name);
    if (!val)
    {
        // Build list of available variables for better error message
        std::string available_vars;
        size_t count = PyInterface::g_values.size();
        size_t i = 0;
        for (const auto &pair : PyInterface::g_values)
        {
            available_vars += pair.first;
            if (++i < count)
                available_vars += ", ";
        }

        if (available_vars.empty())
        {
            PyErr_Format(PyExc_AttributeError,
                         "Unknown C++ variable '%s' - no variables are currently bound",
                         attr_name);
        }
        else
        {
            PyErr_Format(PyExc_AttributeError,
                         "Unknown C++ variable '%s' - available variables: %s",
                         attr_name, available_vars.c_str());
        }
        return nullptr;
    }

    // Convert based on type
    switch (val->type)
    {
    case ValueType::Struct:
    {
        auto *bs = static_cast<BoundStruct *>(val);
        // Create a wrapper that the proxy can own and delete safely
        BoundStruct *wrapper = new BoundStruct(bs->name, bs->instance(), bs->info());
        return StructProxy_New(wrapper);
    }

    case ValueType::Vector:
    {
        auto *bv = static_cast<BoundVector *>(val);
        // Create a wrapper that the proxy can own and delete safely
        BoundVector *wrapper = new BoundVector(bv->name, bv->raw_vector(), bv->info());
        return VectorProxy_New(wrapper);
    }

    default:
    {
        // Scalar types - use PyBoundValue interface
        PyBoundValue *pyval = dynamic_cast<PyBoundValue *>(val);
        if (!pyval)
        {
            PyErr_SetString(PyExc_RuntimeError, "Unsupported C++ variable type");
            return nullptr;
        }
        return pyval->to_python();
    }
    }
}

// Module __setattr__ to allow setting scalar values
static int cpp_module_setattr(PyObject *module, PyObject *name, PyObject *value)
{
    // Convert name to string
    const char *attr_name = PyUnicode_AsUTF8(name);
    if (!attr_name)
        return -1;

    // Look up in bound C++ values
    BoundValue *val = PyInterface::get_value_raw(attr_name);
    if (!val)
    {
        // Build list of available variables for better error message
        std::string available_vars;
        size_t count = PyInterface::g_values.size();
        size_t i = 0;
        for (const auto &pair : PyInterface::g_values)
        {
            available_vars += pair.first;
            if (++i < count)
                available_vars += ", ";
        }

        if (available_vars.empty())
        {
            PyErr_Format(PyExc_AttributeError,
                         "Unknown C++ variable '%s' - no variables are currently bound",
                         attr_name);
        }
        else
        {
            PyErr_Format(PyExc_AttributeError,
                         "Unknown C++ variable '%s' - available variables: %s",
                         attr_name, available_vars.c_str());
        }
        return -1;
    }

    // Only allow setting scalar types
    if (val->type == ValueType::Struct || val->type == ValueType::Vector)
    {
        PyErr_Format(PyExc_TypeError, "Cannot reassign struct or vector '%s'", attr_name);
        return -1;
    }

    // Set scalar value
    PyBoundValue *pyval = dynamic_cast<PyBoundValue *>(val);
    if (!pyval)
    {
        PyErr_SetString(PyExc_RuntimeError, "Unsupported C++ variable type");
        return -1;
    }

    if (!pyval->from_python(value))
    {
        PyErr_Format(PyExc_TypeError, "Type mismatch for '%s'", attr_name);
        return -1;
    }

    return 0;
}

static PyModuleDef cppmodule = {
    PyModuleDef_HEAD_INIT,
    "cpp",
    "C++ bridge module",
    -1,
    nullptr,
    nullptr, // m_reload
    nullptr, // m_traverse
    nullptr, // m_clear
    nullptr  // m_free
};

// Custom module type with __getattr__ and __setattr__
static PyTypeObject CppModuleType = {
    PyVarObject_HEAD_INIT(nullptr, 0) "cpp.module", // tp_name
    0,                                              // tp_basicsize (inherited)
    0,                                              // tp_itemsize
    nullptr,                                        // tp_dealloc
    0,                                              // tp_vectorcall_offset
    nullptr,                                        // tp_getattr
    nullptr,                                        // tp_setattr
    nullptr,                                        // tp_as_async
    nullptr,                                        // tp_repr
    nullptr,                                        // tp_as_number
    nullptr,                                        // tp_as_sequence
    nullptr,                                        // tp_as_mapping
    nullptr,                                        // tp_hash
    nullptr,                                        // tp_call
    nullptr,                                        // tp_str
    cpp_module_getattr,                             // tp_getattro
    cpp_module_setattr,                             // tp_setattro
    nullptr,                                        // tp_as_buffer
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,       // tp_flags
    "C++ extension module",                         // tp_doc
    nullptr,                                        // tp_traverse
    nullptr,                                        // tp_clear
    nullptr,                                        // tp_richcompare
    0,                                              // tp_weaklistoffset
    nullptr,                                        // tp_iter
    nullptr,                                        // tp_iternext
    nullptr,                                        // tp_methods
    nullptr,                                        // tp_members
    nullptr,                                        // tp_getset
    &PyModule_Type,                                 // tp_base (inherit from module)
    nullptr,                                        // tp_dict
    nullptr,                                        // tp_descr_get
    nullptr,                                        // tp_descr_set
    0,                                              // tp_dictoffset
    nullptr,                                        // tp_init
    nullptr,                                        // tp_alloc
    nullptr                                         // tp_new
};

extern "C" PyMODINIT_FUNC PyInit_cpp(void)
{
    // Initialize proxy types
    if (PyType_Ready(&CppProxyType) < 0)
        return nullptr;
    if (PyType_Ready(&StructProxyType) < 0)
        return nullptr;
    if (PyType_Ready(&VectorProxyType) < 0)
        return nullptr;

    // Initialize our custom module type
    if (PyType_Ready(&CppModuleType) < 0)
        return nullptr;

    // Create module using standard module creation
    PyObject *module = PyModule_Create(&cppmodule);
    if (!module)
        return nullptr;

    // Change the module's type to our custom type
    Py_SET_TYPE(module, &CppModuleType);

    return module;
}
