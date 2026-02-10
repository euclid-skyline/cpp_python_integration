#include "value_interface.hpp"

// Explicit template instantiation
// template void PyInterface::bind<int>(const std::string &, int &);
// template void PyInterface::bind<float>(const std::string &, float &);
// template void PyInterface::bind<bool>(const std::string &, bool &);
// template void PyInterface::bind<std::string>(const std::string &, std::string &);

// ---------------------------------------------------------
PyBoundValue *PyInterface::get_value(const std::string &name)
{
    auto it = g_values.find(name);
    return (it != g_values.end()) ? it->second.get() : nullptr;
}

// ---------------------------------------------------------
// C++ Accessor functions for Python
// ---------------------------------------------------------
static PyObject *py_cpp_get(PyObject *, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name))
        return nullptr;

    PyBoundValue *val = PyInterface::get_value(name);
    if (!val)
    {
        PyErr_SetString(PyExc_KeyError, "Unknown C++ variable");
        return nullptr;
    }

    return val->to_python();
}

static PyObject *py_cpp_set(PyObject *, PyObject *args)
{
    const char *name;
    PyObject *obj;

    if (!PyArg_ParseTuple(args, "sO", &name, &obj))
        return nullptr;

    PyBoundValue *val = PyInterface::get_value(name);
    if (!val)
    {
        PyErr_SetString(PyExc_KeyError, "Unknown C++ variable");
        return nullptr;
    }

    if (!val->from_python(obj))
    {
        PyErr_SetString(PyExc_TypeError, "Type mismatch");
        return nullptr;
    }

    Py_RETURN_NONE;
}

// Python Auto-discovery all bound variables and their types/values in Python dictionary
static PyObject *py_cpp_list(PyObject *, PyObject *)
{
    PyObject *dict = PyDict_New();

    for (auto &[name, val] : PyInterface::g_values)
    {
        PyObject *info = PyDict_New();

        // type name
        const char *typeName =
            val->type == ValueType::Int ? "int" : val->type == ValueType::Float ? "float"
                                              : val->type == ValueType::Bool    ? "bool"
                                              : val->type == ValueType::String  ? "string"
                                                                                : "unknown";

        PyDict_SetItemString(info, "type", PyUnicode_FromString(typeName));

        // current value
        PyDict_SetItemString(info, "value", val->to_python());

        // add to main dict
        PyDict_SetItemString(dict, name.c_str(), info);
        Py_DECREF(info);
    }

    return dict;
}

// ---------------------------------------------------------
// Module definition
// ---------------------------------------------------------
static PyMethodDef CppMethods[] = {
    {"cpp_get", py_cpp_get, METH_VARARGS, "Get C++ variable"},
    {"cpp_set", py_cpp_set, METH_VARARGS, "Set C++ variable"},
    {"cpp_list", py_cpp_list, METH_NOARGS, "List all C++ variables"},
    {nullptr, nullptr, 0, nullptr} // <-- sentinel terminator for a Python C‑API method table
};

static PyModuleDef CppModule = {
    PyModuleDef_HEAD_INIT,
    "cppbridge",
    "C++/Python bridge",
    -1,
    CppMethods,
    nullptr,
    nullptr,
    nullptr,
    nullptr};

PyObject *PyInterface::init_py_module()
{
    PyObject *module = PyModule_Create(&CppModule);
    PyObject *proxy = create_cpp_proxy();
    
    PyModule_AddObject(module, "cpp", proxy);

    return module;
}