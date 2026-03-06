# Issue 50 Fix Proposal: Exception Safety at Python C API Boundary

**Date:** March 7, 2026  
**Scope:** Minimal fix to prevent C++ exceptions from crashing Python interpreter  
**Approach:** Add try-catch blocks at proxy layer only (no full error architecture)

---

## Overview

After modifying `reflection_builder.hpp` and `reflection_vector.hpp` to throw exceptions instead of returning error codes, we must catch these exceptions at the **Python C API boundary** (proxy layer) and convert them to Python exceptions.

**Key Principle:** The reflection layer throws C++ exceptions naturally. The proxy layer catches and converts them to Python errors.

---

## Exception Types from Reflection Layer

| Reflection Function | Can Throw |
|---------------------|-----------|
| `generic_vec_append()` | `std::invalid_argument` (null pointers)<br>`std::bad_alloc` (memory failure)<br>Element copy constructor exceptions |
| `generic_vec_element_ptr()` | `std::invalid_argument` (null pointer)<br>`std::out_of_range` (index bounds) |
| `generic_struct_construct()` | `std::invalid_argument` (null pointer)<br>Constructor exceptions |
| `append_from_cpp()` | `std::invalid_argument` (null append_fn)<br>Propagates exceptions from `generic_vec_append` |

---

## Proposed Changes to python_proxy.cpp

### Change 1: VectorProxy_append()

**Location:** Lines 1058-1148  
**Operations that can throw:** `vec->append_from_cpp(&v)` (multiple times)

**Wrap the entire switch statement:**

```cpp
static PyObject *VectorProxy_append(PyObject *self, PyObject *value)
{
    auto *proxy = reinterpret_cast<VectorProxyObject *>(self);
    if (!proxy || !proxy->bound)
    {
        PyErr_SetString(PyExc_RuntimeError, "Internal error: VectorProxy has null BoundVector");
        return nullptr;
    }
    BoundVector *vec = proxy->bound;
    const VectorInfo *info = vec->info();
    if (!info)
    {
        PyErr_SetString(PyExc_RuntimeError, "VectorInfo is null");
        return nullptr;
    }

    // ========== ADD TRY BLOCK HERE ==========
    try {
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
            vec->append_from_cpp(&v);  // CAN THROW
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
            vec->append_from_cpp(&v);  // CAN THROW
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
            vec->append_from_cpp(&v);  // CAN THROW
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
            vec->append_from_cpp(&v);  // CAN THROW
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
            if (!sp->bound)
            {
                PyErr_SetString(PyExc_RuntimeError, "StructProxy has null BoundStruct");
                return nullptr;
            }
            BoundStruct *bs = sp->bound;
            void *struct_instance = bs->instance();
            if (!struct_instance)
            {
                PyErr_SetString(PyExc_RuntimeError, "Failed to resolve struct instance");
                return nullptr;
            }
            vec->append_from_cpp(struct_instance);  // CAN THROW
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
            if (!vp->bound)
            {
                PyErr_SetString(PyExc_RuntimeError, "VectorProxy has null BoundVector");
                return nullptr;
            }
            BoundVector *inner = vp->bound;
            void *inner_raw = inner->raw_vector();
            if (!inner_raw)
            {
                PyErr_SetString(PyExc_RuntimeError, "Failed to resolve vector pointer");
                return nullptr;
            }
            vec->append_from_cpp(inner_raw);  // CAN THROW
            break;
        }

        default:
            PyErr_SetString(PyExc_TypeError, "Unsupported vector element type");
            return nullptr;
        }
    }
    // ========== ADD CATCH BLOCKS HERE ==========
    catch (const std::bad_alloc&)
    {
        PyErr_SetString(PyExc_MemoryError, "Failed to append: out of memory");
        return nullptr;
    }
    catch (const std::invalid_argument& e)
    {
        PyErr_Format(PyExc_ValueError, "Failed to append: %s", e.what());
        return nullptr;
    }
    catch (const std::out_of_range& e)
    {
        PyErr_Format(PyExc_IndexError, "Failed to append: %s", e.what());
        return nullptr;
    }
    catch (const std::exception& e)
    {
        PyErr_Format(PyExc_RuntimeError, "Failed to append: %s", e.what());
        return nullptr;
    }
    catch (...)
    {
        PyErr_SetString(PyExc_RuntimeError, "Failed to append: unknown C++ exception");
        return nullptr;
    }

    Py_RETURN_NONE;
}
```

---

### Change 2: VectorProxy_append_new()

**Location:** Lines 891-960  
**Operations that can throw:** `sinfo->construct_fn()`, `vec->append_from_cpp()`

**Wrap the construction and append operations:**

```cpp
static PyObject *VectorProxy_append_new(PyObject *self, PyObject *args)
{
    (void)args;
    auto *proxy = reinterpret_cast<VectorProxyObject *>(self);
    if (!proxy || !proxy->bound)
    {
        PyErr_SetString(PyExc_RuntimeError, "Internal error: VectorProxy has null BoundVector");
        return nullptr;
    }
    BoundVector *vec = proxy->bound;
    const VectorInfo *info = vec->info();
    if (!info)
    {
        PyErr_SetString(PyExc_RuntimeError, "VectorInfo is null");
        return nullptr;
    }

    // Only works for struct element types
    if (info->element_type != ValueType::Struct)
    {
        PyErr_SetString(PyExc_TypeError, "append_new() only works for vectors of structs");
        return nullptr;
    }

    const StructInfo *sinfo = static_cast<const StructInfo *>(info->element_meta);
    if (!sinfo || sinfo->size == 0)
    {
        PyErr_SetString(PyExc_RuntimeError, "Cannot append struct with invalid metadata size");
        return nullptr;
    }

    // Allocate raw storage for one struct instance.
    void *new_instance = nullptr;
    
    // ========== ADD TRY BLOCK HERE ==========
    try {
        new_instance = ::operator new(sinfo->size);  // CAN THROW std::bad_alloc

        bool constructed = false;
        if (sinfo->construct_fn)
        {
            sinfo->construct_fn(new_instance);  // CAN THROW
            constructed = true;
        }
        else
        {
            std::memset(new_instance, 0, sinfo->size);
        }

        // append_from_cpp performs copy into the destination vector.
        vec->append_from_cpp(new_instance);  // CAN THROW

        // Destroy temporary object before releasing raw storage.
        if (constructed && sinfo->destruct_fn)
        {
            sinfo->destruct_fn(new_instance);  // NOEXCEPT
        }
        ::operator delete(new_instance);

        // Get the last element (the one we just added)
        std::size_t last_idx = vec->size() - 1;

        // Return a proxy to the newly added element
        BoundStruct *bstruct = new BoundStruct(vec->name, vec, last_idx, sinfo);
        return StructProxy_New(bstruct, self);
    }
    // ========== ADD CATCH BLOCKS HERE ==========
    catch (const std::bad_alloc&)
    {
        if (new_instance) {
            ::operator delete(new_instance);
        }
        PyErr_SetString(PyExc_MemoryError, "Failed to append new struct: out of memory");
        return nullptr;
    }
    catch (const std::invalid_argument& e)
    {
        if (new_instance) {
            ::operator delete(new_instance);
        }
        PyErr_Format(PyExc_ValueError, "Failed to append new struct: %s", e.what());
        return nullptr;
    }
    catch (const std::exception& e)
    {
        if (new_instance) {
            ::operator delete(new_instance);
        }
        PyErr_Format(PyExc_RuntimeError, "Failed to append new struct: %s", e.what());
        return nullptr;
    }
    catch (...)
    {
        if (new_instance) {
            ::operator delete(new_instance);
        }
        PyErr_SetString(PyExc_RuntimeError, "Failed to append new struct: unknown C++ exception");
        return nullptr;
    }
}
```

---

### Change 3: VectorProxy_append_new_vector()

**Location:** Lines 962-1056  
**Operations that can throw:** `vec->append_from_cpp()`

**Wrap all append_from_cpp calls:**

```cpp
static PyObject *VectorProxy_append_new_vector(PyObject *self, PyObject *args)
{
    (void)args;
    auto *proxy = reinterpret_cast<VectorProxyObject *>(self);
    if (!proxy || !proxy->bound)
    {
        PyErr_SetString(PyExc_RuntimeError, "Internal error: VectorProxy has null BoundVector");
        return nullptr;
    }
    BoundVector *vec = proxy->bound;
    const VectorInfo *info = vec->info();
    if (!info)
    {
        PyErr_SetString(PyExc_RuntimeError, "VectorInfo is null");
        return nullptr;
    }

    // Only works for nested vector element types
    if (info->element_type != ValueType::Vector)
    {
        PyErr_SetString(PyExc_TypeError, "append_new_vector() only works for vectors of vectors");
        return nullptr;
    }

    const VectorInfo *inner_info = static_cast<const VectorInfo *>(info->element_meta);
    if (!inner_info)
    {
        PyErr_SetString(PyExc_RuntimeError, "Cannot append vector with invalid inner metadata");
        return nullptr;
    }

    // ========== ADD TRY BLOCK HERE ==========
    try {
        // Handle inner vector of struct type (use create_empty_vec_fn)
        if (inner_info->element_type == ValueType::Struct)
        {
            void *temp_vec = inner_info->create_empty_vec_fn();
            if (!temp_vec)
            {
                PyErr_SetString(PyExc_RuntimeError, "Failed to create inner vector");
                return nullptr;
            }
            vec->append_from_cpp(temp_vec);  // CAN THROW
            inner_info->destroy_vec_fn(temp_vec);
        }
        else
        {
            switch (inner_info->element_type)
            {
            case ValueType::Int:
            {
                std::vector<int> new_inner_vec;
                vec->append_from_cpp(&new_inner_vec);  // CAN THROW
                break;
            }

            case ValueType::Float:
            {
                std::vector<float> new_inner_vec;
                vec->append_from_cpp(&new_inner_vec);  // CAN THROW
                break;
            }

            case ValueType::Bool:
            {
                std::vector<ByteBool> new_inner_vec;
                vec->append_from_cpp(&new_inner_vec);  // CAN THROW
                break;
            }

            case ValueType::String:
            {
                std::vector<std::string> new_inner_vec;
                vec->append_from_cpp(&new_inner_vec);  // CAN THROW
                break;
            }

            default:
                PyErr_SetString(PyExc_TypeError, "Unsupported inner vector element type");
                return nullptr;
            }
        }

        // Get the last element (the one we just added)
        std::size_t last_idx = vec->size() - 1;

        // Return a proxy to the newly added inner vector
        BoundVector *bvec = new BoundVector(vec->name, vec, last_idx, inner_info);
        return VectorProxy_New(bvec, self);
    }
    // ========== ADD CATCH BLOCKS HERE ==========
    catch (const std::bad_alloc&)
    {
        PyErr_SetString(PyExc_MemoryError, "Failed to append new vector: out of memory");
        return nullptr;
    }
    catch (const std::invalid_argument& e)
    {
        PyErr_Format(PyExc_ValueError, "Failed to append new vector: %s", e.what());
        return nullptr;
    }
    catch (const std::exception& e)
    {
        PyErr_Format(PyExc_RuntimeError, "Failed to append new vector: %s", e.what());
        return nullptr;
    }
    catch (...)
    {
        PyErr_SetString(PyExc_RuntimeError, "Failed to append new vector: unknown C++ exception");
        return nullptr;
    }
}
```

---

### Change 4: VectorProxy_getitem()

**Location:** Lines 675-750  
**Operations that can throw:** `proxy->bound->element_ptr(index)`

**Wrap the element_ptr call:**

```cpp
static PyObject *VectorProxy_getitem(PyObject *self, Py_ssize_t index)
{
    VectorProxyObject *proxy = (VectorProxyObject *)self;

    if (!proxy || !proxy->bound)
    {
        PyErr_SetString(PyExc_RuntimeError, "Internal error: VectorProxy has null BoundVector");
        return nullptr;
    }

    std::size_t vectorSize = proxy->bound->size();
    if (vectorSize > static_cast<std::size_t>(PY_SSIZE_T_MAX))
    {
        PyErr_SetString(PyExc_OverflowError, "Vector is too large for Python indexing");
        return nullptr;
    }
    Py_ssize_t size = static_cast<Py_ssize_t>(vectorSize);

    // Support negative indexing
    if (index < 0)
        index += size;

    if (index < 0 || index >= size)
    {
        PyErr_SetString(PyExc_IndexError, "Vector index out of range");
        return nullptr;
    }

    // ========== ADD TRY BLOCK HERE ==========
    void *elemPtr = nullptr;
    try {
        elemPtr = proxy->bound->element_ptr(index);  // CAN THROW std::out_of_range
    }
    catch (const std::out_of_range& e)
    {
        PyErr_Format(PyExc_IndexError, "Vector index out of range: %s", e.what());
        return nullptr;
    }
    catch (const std::invalid_argument& e)
    {
        PyErr_Format(PyExc_ValueError, "Failed to get element: %s", e.what());
        return nullptr;
    }
    catch (const std::exception& e)
    {
        PyErr_Format(PyExc_RuntimeError, "Failed to get element: %s", e.what());
        return nullptr;
    }
    catch (...)
    {
        PyErr_SetString(PyExc_RuntimeError, "Failed to get element: unknown C++ exception");
        return nullptr;
    }

    if (!elemPtr)
    {
        PyErr_SetString(PyExc_RuntimeError, "Failed to get element pointer");
        return nullptr;
    }
    
    // ... rest of function continues unchanged ...
```

---

### Change 5: StructProxy_getattro() (Defensive)

**Location:** Lines 308-400  
**Operations that can throw:** Accessing struct fields (less likely, but defensive)

**Wrap the field access:**

```cpp
static PyObject *StructProxy_getattro(PyObject *self, PyObject *attr)
{
    StructProxyObject *proxy = (StructProxyObject *)self;

    if (!proxy || !proxy->bound)
    {
        PyErr_SetString(PyExc_RuntimeError, "Internal error: StructProxy has null BoundStruct");
        return nullptr;
    }

    const char *name = PyUnicode_AsUTF8(attr);
    if (!name)
    {
        PyErr_SetString(PyExc_TypeError, "Field name must be a string");
        return nullptr;
    }
    
    // ========== ADD TRY BLOCK HERE (DEFENSIVE) ==========
    try {
        const FieldInfo *field = proxy->bound->get_field(name);

        if (!field)
        {
            PyErr_Format(PyExc_AttributeError, "Unknown field '%s'", name);
            return nullptr;
        }

        void *fieldPtr = proxy->bound->get_field_ptr(field);
        if (!fieldPtr)
        {
            PyErr_SetString(PyExc_RuntimeError, "Failed to resolve field pointer");
            return nullptr;
        }

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
            if (!sinfo)
            {
                PyErr_Format(PyExc_RuntimeError,
                             "Internal error: Struct field '%s' has null metadata. "
                             "Check FIELD() registration for this struct field.",
                             field->name.c_str());
                return nullptr;
            }
            BoundStruct *bstruct = new BoundStruct(field->name, proxy->bound, field->offset, sinfo);
            PyObject *result = StructProxy_New(bstruct, self);
            if (!result)
            {
                delete bstruct;
            }
            return result;
        }

        case ValueType::Vector:
        {
            const VectorInfo *vinfo = static_cast<const VectorInfo *>(field->type_meta);
            if (!vinfo)
            {
                PyErr_Format(PyExc_RuntimeError,
                             "Internal error: Vector field '%s' has null metadata. "
                             "Check FIELD() registration for this vector field.",
                             field->name.c_str());
                return nullptr;
            }
            BoundVector *bvec = new BoundVector(field->name, proxy->bound, field->offset, vinfo);
            PyObject *result = VectorProxy_New(bvec, self);
            if (!result)
            {
                delete bvec;
            }
            return result;
        }

        default:
            PyErr_Format(PyExc_TypeError, "Unsupported field type for '%s'", field->name.c_str());
            return nullptr;
        }
    }
    // ========== ADD CATCH BLOCKS HERE ==========
    catch (const std::exception& e)
    {
        PyErr_Format(PyExc_RuntimeError, "Failed to get attribute '%s': %s", name, e.what());
        return nullptr;
    }
    catch (...)
    {
        PyErr_Format(PyExc_RuntimeError, "Failed to get attribute '%s': unknown C++ exception", name);
        return nullptr;
    }
}
```

---

## Exception Mapping Table

| C++ Exception | Python Exception | Use Case |
|---------------|------------------|----------|
| `std::bad_alloc` | `PyExc_MemoryError` | Memory allocation failure |
| `std::invalid_argument` | `PyExc_ValueError` | Null pointer, invalid function pointer |
| `std::out_of_range` | `PyExc_IndexError` | Array/vector bounds violation |
| `std::exception` (other) | `PyExc_RuntimeError` | Generic C++ exception |
| `...` (unknown) | `PyExc_RuntimeError` | Non-standard exception |

---

## Testing Checklist

After implementing these changes:

- [ ] Test appending to full vector (memory allocation failure simulation)
- [ ] Test appending with copy constructor that throws
- [ ] Test accessing out-of-bounds indices
- [ ] Test with null/invalid metadata
- [ ] Test nested vector/struct operations
- [ ] Verify Python gets correct exception types
- [ ] Verify no memory leaks in exception paths
- [ ] Verify Python traceback shows meaningful errors

---

## Summary

**Files Modified:** 1 file
- `python_proxy.cpp` - Add try-catch blocks to 5 functions

**Lines Changed:** Approximately 150 lines (adding exception handling)

**Approach:** Minimal defensive fix at boundary layer only
- No new files created
- No error handler infrastructure
- Simple exception-to-Python-error translation
- Prevents interpreter crashes
- Provides meaningful error messages to Python scripts

**Next Steps:** After approval, implement changes with multi_replace_string_in_file
