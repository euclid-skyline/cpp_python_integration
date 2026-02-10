// A More Generalized Interface for Exposing C++ Variables to Python
#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <type_traits>
#include <Python.h>

enum class ValueType
{
    Int,
    Float,
    Bool,
    String
    // Extendable for more types like Vector, Array, etc.
};

// ---------------------------------------------------------
// Base class
// ---------------------------------------------------------
struct PyBoundValue
{
    std::string name;
    ValueType type;

    virtual ~PyBoundValue() = default;
    virtual PyObject *to_python() = 0;
    virtual bool from_python(PyObject *obj) = 0;
};

// ---------------------------------------------------------
// Concrete types
// ---------------------------------------------------------
struct PyBoundInt : PyBoundValue
{
    int *ptr;

    PyBoundInt(const std::string &n, int &v)
    {
        name = n;
        type = ValueType::Int;
        ptr = &v;
    }
    PyObject *to_python() override { return PyLong_FromLong(*ptr); }

    bool from_python(PyObject *obj) override
    {
        if (!PyLong_Check(obj))
            return false;

        long v = PyLong_AsLong(obj);
        *ptr = static_cast<int>(v);

        return true;
    }
};

struct PyBoundFloat : PyBoundValue
{
    float *ptr;
    PyBoundFloat(const std::string &n, float &v)
    {
        name = n;
        type = ValueType::Float;
        ptr = &v;
    }
    PyObject *to_python() override { return PyFloat_FromDouble(*ptr); }
    bool from_python(PyObject *obj) override
    {
        if (!PyFloat_Check(obj) && !PyLong_Check(obj))
            return false;
        *ptr = (float)PyFloat_AsDouble(obj);
        return true;
    }
};

struct PyBoundBool : PyBoundValue
{
    bool *ptr;
    PyBoundBool(const std::string &n, bool &v)
    {
        name = n;
        type = ValueType::Bool;
        ptr = &v;
    }
    PyObject *to_python() override { return PyBool_FromLong(*ptr ? 1 : 0); }
    bool from_python(PyObject *obj) override
    {
        if (!PyBool_Check(obj))
            return false;
        *ptr = (obj == Py_True);
        return true;
    }
};

struct PyBoundString : PyBoundValue
{
    std::string *ptr;
    PyBoundString(const std::string &n, std::string &v)
    {
        name = n;
        type = ValueType::String;
        ptr = &v;
    }
    PyObject *to_python() override
    {
        return PyUnicode_FromString(ptr->c_str());
    }
    bool from_python(PyObject *obj) override
    {
        if (!PyUnicode_Check(obj))
            return false;
        PyObject *utf8 = PyUnicode_AsUTF8String(obj);
        *ptr = PyBytes_AsString(utf8);
        Py_DECREF(utf8);
        return true;
    }
};

// ---------------------------------------------------------
// Interface class
// ---------------------------------------------------------
class PyInterface
{
public:
    // template <typename T>
    // static void bind(const std::string &name, T &variable);

    static PyBoundValue *get_value(const std::string &name);

    static PyObject *init_py_module();

    template <typename T>
    static void bind(const std::string &name, T &variable)
    {
        if constexpr (std::is_same_v<T, int>)
        {
            g_values[name] = std::make_unique<PyBoundInt>(name, variable);
        }
        else if constexpr (std::is_same_v<T, float>)
        {
            g_values[name] = std::make_unique<PyBoundFloat>(name, variable);
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            g_values[name] = std::make_unique<PyBoundBool>(name, variable);
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            g_values[name] = std::make_unique<PyBoundString>(name, variable);
        }
        else
        {
            static_assert(!sizeof(T), "Unsupported type for PyInterface::bind()");
        }
    }

public:
    static inline std::unordered_map<std::string, std::unique_ptr<PyBoundValue>> g_values;
};

// Proxy object struct for Python
PyObject* create_cpp_proxy();
