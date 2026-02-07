#include <Python.h>
#include <unordered_map>
#include <string>

// Define a Bindable Variable for Integer
struct PyBoundInt
{
    std::string name;
    int *ptr; // pointer to the real C++ variable
};

// An Interface Registry to Hold All Exposed Variables
class PyInterface
{
public:
    static void bind_int(const std::string &name, int &variable)
    {
        c_ints[name] = {name, &variable};
    }

    static int *get_int(const std::string &name)
    {
        auto it = c_ints.find(name);
        return (it != c_ints.end()) ? it->second.ptr : nullptr;
    }

    // Python module initializer
    static PyObject *init_py_module();

private:
    static inline std::unordered_map<std::string, PyBoundInt> c_ints;
};

// Expose C++ Accessors to Python
static PyObject *py_cpp_get(PyObject *, PyObject *args)
{
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name))
        return nullptr;

    int *ptr = PyInterface::get_int(name);
    if (!ptr)
    {
        PyErr_SetString(PyExc_KeyError, "Unknown C++ variable");
        return nullptr;
    }

    return PyLong_FromLong(*ptr);
}

static PyObject *py_cpp_set(PyObject *, PyObject *args)
{
    const char *name;
    int value;

    if (!PyArg_ParseTuple(args, "si", &name, &value))
        return nullptr;

    int *ptr = PyInterface::get_int(name);
    if (!ptr)
    {
        PyErr_SetString(PyExc_KeyError, "Unknown C++ variable");
        return nullptr;
    }

    *ptr = value;
    Py_RETURN_NONE;
}

// Register Accessor Functions as a Python Module
static PyMethodDef CppMethods[] = {
    {"cpp_get", py_cpp_get, METH_VARARGS, "Get C++ variable"},
    {"cpp_set", py_cpp_set, METH_VARARGS, "Set C++ variable"},
    {nullptr, nullptr, 0, nullptr}};

static PyModuleDef CppModule = {
    PyModuleDef_HEAD_INIT,
    "cppbridge",         // m_name
    "C++/Python bridge", // m_doc
    -1,                  // m_size
    CppMethods,          // m_methods
    nullptr,             // m_slots
    nullptr,             // m_traverse
    nullptr,             // m_clear
    nullptr              // m_free
};

PyObject *PyInterface::init_py_module()
{
    return PyModule_Create(&CppModule);
}
