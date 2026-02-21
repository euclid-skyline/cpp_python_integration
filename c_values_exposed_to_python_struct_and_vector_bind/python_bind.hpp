#pragma once

#include <Python.h>      // Python C‑API
#include <unordered_map> // std::unordered_map

#include "reflection_value.hpp" // BoundValue, ValueType

struct PyBoundValue : public BoundValue
{
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
    ByteBool *ptr;
    PyBoundBool(const std::string &n, ByteBool &v)
    {
        name = n;
        type = ValueType::Bool;
        ptr = &v;
    }
    PyObject *to_python() override { return PyBool_FromLong((*ptr != FALSE_BYTE) ? 1 : 0); }

    bool from_python(PyObject *obj) override
    {
        if (!PyBool_Check(obj))
            return false;
        *ptr = (obj == Py_True) ? TRUE_BYTE : FALSE_BYTE;
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
        const char *str = PyUnicode_AsUTF8(obj);
        if (!str)
            return false;
        *ptr = str; // std::string copies the content
        return true;
    }
};
