#pragma once

#include <Python.h>

#include "reflection_struct.hpp" // BoundStruct, StructInfo
#include "reflection_vector.hpp" // BoundVector, VectorInfo

// ------------------------------------------------------------
// Forward declarations for proxy types
// ------------------------------------------------------------

// Root proxy (from value_interface_proxy.cpp)
extern PyTypeObject CppProxyType;
PyObject *create_cpp_proxy();

// StructProxy (from aggregate_interface_proxy.cpp)
extern PyTypeObject StructProxyType;
PyObject *StructProxy_New(BoundStruct *bound, PyObject *parent = nullptr);

// VectorProxy (from aggregate_interface_proxy.cpp)
extern PyTypeObject VectorProxyType;
PyObject *VectorProxy_New(BoundVector *bound, PyObject *parent = nullptr);

// VectorIterator (for iteration protocol)
extern PyTypeObject VectorIteratorType;
