# Error and Exception Handling in C++/Python Integration

**Document Version:** 1.0  
**Date:** March 4, 2026  
**Related Issue:** Issue 50 - Missing Exception Safety Across Python C API Boundary

---

## Table of Contents

1. [Overview](#overview)
2. [Current State Analysis](#current-state-analysis)
3. [Error Flow Directions](#error-flow-directions)
4. [Available Strategies](#available-strategies)
5. [Implementation Guide](#implementation-guide)
6. [Best Practices](#best-practices)
7. [Testing Strategies](#testing-strategies)

---

## Overview

When integrating Python scripts into C++ applications, you have **four critical error scenarios** to handle:

```
┌─────────────────────────────────────────────────────────┐
│ Scenario 1: C++ Exception → Python (Issue 50)           │
│   C++ code throws → Must not cross C API boundary       │
├─────────────────────────────────────────────────────────┤
│ Scenario 2: Python Exception → C++                      │
│   Python code raises → C++ must detect and handle       │
├─────────────────────────────────────────────────────────┤
│ Scenario 3: C++ Error State → Python                    │
│   C++ returns false/nullptr → Set Python exception      │
├─────────────────────────────────────────────────────────┤
│ Scenario 4: Python Error State → C++                    │
│   Python returns nullptr → Check PyErr_Occurred()       │
└─────────────────────────────────────────────────────────┘
```

---

## Current State Analysis

### What Your Codebase Does Well

✅ **Python-to-C++ Error Detection** (Scenario 4)
```cpp
// main.cpp - Already correctly handling Python exceptions
PyObject *result = PyObject_CallObject(updateFunc, nullptr);
if (result) {
    Py_DECREF(result);
} else {
    if (PyErr_ExceptionMatches(PyExc_KeyboardInterrupt)) {
        PyErr_Clear();  // Graceful handling
    }
    PyErr_Print();  // Display to user
}
```

✅ **Manual Error Reporting to Python** (Scenario 3)
```cpp
// python_proxy.cpp - Sets Python exceptions appropriately
if (!proxy || !proxy->bound) {
    PyErr_SetString(PyExc_RuntimeError, 
        "Internal error: StructProxy has null BoundStruct");
    return nullptr;
}
```

### Critical Gap (Issue 50)

❌ **C++-to-Python Exception Boundary** (Scenario 1)
```cpp
// reflection_builder.hpp - CURRENT PROBLEM
template <typename T>
bool generic_vec_append(void *vec_ptr, void *value_ptr)
{
    if (!vec_ptr || !value_ptr)
        return false;
    static_cast<std::vector<T> *>(vec_ptr)->push_back(*value_ptr);  
    // ⚠️ CAN THROW: std::bad_alloc, copy constructor exceptions
    // These will CRASH Python interpreter!
    return true;
}
```

---

## Error Flow Directions

### Direction 1: C++ → Python (Issue 50 Fix Required)

**Problem:** C++ exceptions must **never** escape into Python C API functions.

**Python C API Contract:**
- All functions returning `PyObject*` must return `nullptr` on error AND set Python exception
- All functions returning `int` must return `-1` on error AND set Python exception
- C++ exceptions violate this contract and cause undefined behavior

**Your Affected Code:**
| Function | Location | Can Throw |
|----------|----------|-----------|
| `generic_vec_append<T>()` | reflection_builder.hpp:44 | `std::bad_alloc`, copy constructor exceptions |
| `generic_struct_construct<T>()` | reflection_builder.hpp:66 | Constructor exceptions |
| `generic_struct_destruct<T>()` | reflection_builder.hpp:72 | Destructor exceptions (rare but possible) |
| `std::vector::push_back()` | Multiple locations | Memory allocation, element construction |

---

### Direction 2: Python → C++

**Current Status:** ✅ Already handled correctly in main.cpp

**Available Options for Detection:**

#### Option A: Check Return Value (Current Approach)
```cpp
PyObject *result = PyObject_CallObject(func, args);
if (!result) {
    // Python exception occurred
    if (PyErr_ExceptionMatches(PyExc_SpecificException)) {
        // Handle specific exception
        PyErr_Clear();  // Clear if handling
    } else {
        PyErr_Print();  // Or just print
    }
    return;
}
// Success path
Py_DECREF(result);
```

**Pros:**
- Simple and efficient
- Standard Python C API pattern
- No overhead when no exception

**Cons:**
- Must check every Python API call
- Easy to forget to check

#### Option B: Exception Indicator Check
```cpp
if (PyErr_Occurred()) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch(&ptype, &pvalue, &ptraceback);
    
    // Extract exception details
    PyObject *str = PyObject_Str(pvalue);
    const char *msg = PyUnicode_AsUTF8(str);
    
    std::cerr << "Python exception: " << msg << std::endl;
    
    Py_XDECREF(str);
    Py_XDECREF(ptype);
    Py_XDECREF(pvalue);
    Py_XDECREF(ptraceback);
}
```

**Pros:**
- Can check exception details
- Can re-raise or transform exception

**Cons:**
- More verbose
- Must manually manage exception references

#### Option C: RAII Exception Guard
```cpp
class PythonExceptionGuard {
public:
    ~PythonExceptionGuard() {
        if (PyErr_Occurred()) {
            PyErr_Print();
            std::terminate();  // Or other recovery
        }
    }
};

void call_python() {
    PythonExceptionGuard guard;
    PyObject *result = PyObject_CallObject(func, args);
    // Guard ensures exception is handled even if we forget
}
```

**Pros:**
- Automatic exception detection
- Can't forget to check
- Useful for complex call patterns

**Cons:**
- Adds overhead
- May hide logic errors

---

## Available Strategies

### Strategy 1: Catch-and-Convert (Recommended for Issue 50)

Wrap all C++ operations that can throw and convert to Python exceptions:

```cpp
template <typename T>
bool generic_vec_append(void *vec_ptr, void *value_ptr)
{
    if (!vec_ptr || !value_ptr)
        return false;
    
    try {
        static_cast<std::vector<T> *>(vec_ptr)->push_back(*static_cast<T *>(value_ptr));
        return true;
    } catch (const std::bad_alloc&) {
        PyErr_SetString(PyExc_MemoryError, "Failed to append: out of memory");
        return false;
    } catch (const std::exception& e) {
        PyErr_Format(PyExc_RuntimeError, "Failed to append: %s", e.what());
        return false;
    } catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to append: unknown C++ exception");
        return false;
    }
}
```

**Exception Type Mapping:**
| C++ Exception | Python Exception | Use Case |
|---------------|------------------|----------|
| `std::bad_alloc` | `PyExc_MemoryError` | Memory allocation failures |
| `std::invalid_argument` | `PyExc_ValueError` | Invalid input values |
| `std::out_of_range` | `PyExc_IndexError` | Index out of bounds |
| `std::runtime_error` | `PyExc_RuntimeError` | General runtime errors |
| `std::logic_error` | `PyExc_RuntimeError` | Logic errors |
| `catch (...)` | `PyExc_RuntimeError` | Unknown exceptions |

---

### Strategy 2: noexcept Boundaries

Mark C API functions as `noexcept` and terminate on exception:

```cpp
// For functions that absolutely must not throw
template <typename T>
void generic_struct_destruct(void *ptr) noexcept
{
    try {
        static_cast<T *>(ptr)->~T();
    } catch (...) {
        // Destructors should never throw
        // Log error but don't propagate
        std::terminate();  // Or just suppress
    }
}
```

**When to Use:**
- Destructors (must never throw)
- Critical cleanup paths
- Functions where exception = unrecoverable error

---

### Strategy 3: Error Code Returns

Return error codes instead of throwing:

```cpp
enum class AppendResult {
    Success,
    NullPointer,
    AllocationFailure,
    CopyFailure
};

template <typename T>
AppendResult generic_vec_append_safe(void *vec_ptr, void *value_ptr)
{
    if (!vec_ptr || !value_ptr)
        return AppendResult::NullPointer;
    
    try {
        static_cast<std::vector<T> *>(vec_ptr)->push_back(*value_ptr);
        return AppendResult::Success;
    } catch (const std::bad_alloc&) {
        return AppendResult::AllocationFailure;
    } catch (...) {
        return AppendResult::CopyFailure;
    }
}
```

**Pros:**
- Explicit error handling
- No exception overhead
- Clear control flow

**Cons:**
- Verbose at call sites
- Easy to ignore error codes

---

### Strategy 4: Callback Error Handlers

Use function pointers or lambdas for error reporting:

```cpp
using ErrorCallback = std::function<void(const char* error)>;

template <typename T>
bool generic_vec_append_with_callback(
    void *vec_ptr, 
    void *value_ptr,
    ErrorCallback on_error)
{
    if (!vec_ptr || !value_ptr) {
        on_error("Null pointer provided");
        return false;
    }
    
    try {
        static_cast<std::vector<T> *>(vec_ptr)->push_back(*value_ptr);
        return true;
    } catch (const std::exception& e) {
        on_error(e.what());
        return false;
    }
}

// Usage from Python C API
extern "C" PyObject* some_api_function() {
    bool success = generic_vec_append_with_callback(
        vec, val,
        [](const char* err) { 
            PyErr_SetString(PyExc_RuntimeError, err); 
        }
    );
    if (!success) return nullptr;
    Py_RETURN_NONE;
}
```

---

## Implementation Guide

### Phase 1: Fix Critical Paths (Issue 50)

**1. Update reflection_builder.hpp**

```cpp
#pragma once
#include <vector>
#include <cstddef>
#include <Python.h>  // ADD: For PyErr_SetString
#include "reflection_vector.hpp"

// Update generic_vec_append
template <typename T>
bool generic_vec_append(void *vec_ptr, void *value_ptr)
{
    if (!vec_ptr || !value_ptr)
        return false;
    
    try {
        static_cast<std::vector<T> *>(vec_ptr)->push_back(*static_cast<T *>(value_ptr));
        return true;
    } catch (const std::bad_alloc&) {
        PyErr_SetString(PyExc_MemoryError, "Failed to append: out of memory");
        return false;
    } catch (const std::exception& e) {
        PyErr_Format(PyExc_RuntimeError, "Failed to append: %s", e.what());
        return false;
    } catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to append: unknown C++ exception");
        return false;
    }
}

// Update generic_struct_construct
template <typename T>
void generic_struct_construct(void *ptr)
{
    try {
        new (ptr) T();
    } catch (const std::exception& e) {
        // Note: Can't set Python error here since this is called
        // from contexts that may not have GIL. Caller must validate.
        // Consider returning bool instead.
    } catch (...) {
        // Silent failure - caller must validate
    }
}

// Update generic_struct_destruct  
template <typename T>
void generic_struct_destruct(void *ptr) noexcept
{
    try {
        static_cast<T *>(ptr)->~T();
    } catch (...) {
        // Destructors must not throw - suppress all exceptions
        // Log if possible, but do not propagate
    }
}
```

**2. Update VectorProxy_append_new (python_proxy.cpp)**

```cpp
static PyObject *VectorProxy_append_new(PyObject *self, PyObject *args)
{
    // ... existing validation ...
    
    // Allocate with exception safety
    void *new_instance = nullptr;
    try {
        new_instance = ::operator new(sinfo->size);
    } catch (const std::bad_alloc&) {
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate struct instance");
        return nullptr;
    }

    bool constructed = false;
    if (sinfo->construct_fn)
    {
        sinfo->construct_fn(new_instance);
        // Note: construct_fn may fail silently - consider validation
        constructed = true;
    }
    else
    {
        std::memset(new_instance, 0, sinfo->size);
    }

    // append_from_cpp wraps generic_vec_append which now handles exceptions
    bool append_ok = vec->append_from_cpp(new_instance);

    // Cleanup
    if (constructed && sinfo->destruct_fn)
    {
        sinfo->destruct_fn(new_instance);
    }
    ::operator delete(new_instance);

    if (!append_ok)
    {
        // Python exception already set by generic_vec_append
        return nullptr;
    }

    // Return proxy to new element
    std::size_t last_idx = vec->size() - 1;
    BoundStruct *bstruct = new BoundStruct(vec->name, vec, last_idx, sinfo);
    return StructProxy_New(bstruct, self);
}
```

---

### Phase 2: Enhance Python Exception Handling

**Option 1: Add Exception Context Helper**

```cpp
// Add to python_proxy.hpp or new error_handling.hpp
class PythonErrorContext {
public:
    static void LogAndClear() {
        if (PyErr_Occurred()) {
            PyErr_Print();
            PyErr_Clear();
        }
    }
    
    static std::string GetExceptionString() {
        if (!PyErr_Occurred()) return "";
        
        PyObject *ptype, *pvalue, *ptraceback;
        PyErr_Fetch(&ptype, &pvalue, &ptraceback);
        
        std::string result;
        if (pvalue) {
            PyObject *str = PyObject_Str(pvalue);
            if (str) {
                const char *msg = PyUnicode_AsUTF8(str);
                if (msg) result = msg;
                Py_DECREF(str);
            }
        }
        
        PyErr_Restore(ptype, pvalue, ptraceback);
        return result;
    }
    
    static void CheckAndThrow() {
        if (PyErr_Occurred()) {
            std::string msg = GetExceptionString();
            PyErr_Clear();
            throw std::runtime_error("Python exception: " + msg);
        }
    }
};
```

**Usage:**
```cpp
// In main.cpp
PyObject *result = PyObject_CallObject(updateFunc, nullptr);
if (!result) {
    std::string error = PythonErrorContext::GetExceptionString();
    std::cerr << "Python error: " << error << std::endl;
    PythonErrorContext::LogAndClear();
    // Handle or continue
}
```

---

### Phase 3: Add Logging and Diagnostics

**Create Error Logging System:**

```cpp
// error_logger.hpp
#pragma once
#include <fstream>
#include <chrono>
#include <mutex>

class ErrorLogger {
private:
    static std::ofstream log_file;
    static std::mutex log_mutex;
    
public:
    static void Init(const std::string& filename) {
        log_file.open(filename, std::ios::app);
    }
    
    static void LogCppException(const char* location, const std::exception& e) {
        std::lock_guard<std::mutex> lock(log_mutex);
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        log_file << "[" << std::ctime(&time) 
                 << "] C++ Exception in " << location 
                 << ": " << e.what() << std::endl;
    }
    
    static void LogPythonException(const char* location) {
        std::lock_guard<std::mutex> lock(log_mutex);
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        log_file << "[" << std::ctime(&time) 
                 << "] Python Exception in " << location;
        
        if (PyErr_Occurred()) {
            PyObject *ptype, *pvalue, *ptraceback;
            PyErr_Fetch(&ptype, &pvalue, &ptraceback);
            if (pvalue) {
                PyObject *str = PyObject_Str(pvalue);
                if (str) {
                    const char *msg = PyUnicode_AsUTF8(str);
                    log_file << ": " << msg;
                    Py_DECREF(str);
                }
            }
            PyErr_Restore(ptype, pvalue, ptraceback);
        }
        log_file << std::endl;
    }
};
```

---

## Best Practices

### DO ✅

1. **Always wrap operations that can throw in try-catch at Python boundary**
   ```cpp
   extern "C" PyObject* my_api_function() {
       try {
           // C++ code that might throw
       } catch (const std::exception& e) {
           PyErr_SetString(PyExc_RuntimeError, e.what());
           return nullptr;
       }
   }
   ```

2. **Check every Python API call that can fail**
   ```cpp
   PyObject *result = PyObject_CallObject(func, args);
   if (!result) {
       // Handle error
       return nullptr;
   }
   ```

3. **Map C++ exceptions to appropriate Python exception types**
   - `std::bad_alloc` → `PyExc_MemoryError`
   - `std::invalid_argument` → `PyExc_ValueError`
   - `std::out_of_range` → `PyExc_IndexError`

4. **Always set Python exception before returning nullptr/-1**
   ```cpp
   if (error_condition) {
       PyErr_SetString(PyExc_RuntimeError, "Description");
       return nullptr;  // or -1 for int functions
   }
   ```

5. **Clean up resources in exception paths**
   ```cpp
   PyObject *obj = create_object();
   if (!process(obj)) {
       Py_DECREF(obj);  // Don't leak!
       return nullptr;
   }
   ```

### DON'T ❌

1. **Never let C++ exceptions cross into Python C API**
   ```cpp
   // WRONG - can crash Python
   extern "C" PyObject* my_api_function() {
       std::vector<int> v;
       v.push_back(42);  // Could throw std::bad_alloc
       Py_RETURN_NONE;
   }
   ```

2. **Never ignore Python API return values**
   ```cpp
   // WRONG - ignores potential errors
   PyObject *result = PyObject_CallObject(func, args);
   Py_XDECREF(result);  // Might be nullptr!
   ```

3. **Never return nullptr without setting exception**
   ```cpp
   // WRONG - violates Python C API contract
   if (error) {
       return nullptr;  // Missing PyErr_SetString!
   }
   ```

4. **Never clear exceptions without handling them**
   ```cpp
   // WRONG - loses error information
   if (PyErr_Occurred()) {
       PyErr_Clear();  // Just silently fail?
   }
   ```

5. **Never throw from destructors or noexcept functions**
   ```cpp
   // WRONG - will call std::terminate
   ~MyClass() noexcept {
       throw std::runtime_error("error");  // CRASH!
   }
   ```

---

## Testing Strategies

### Test 1: Out-of-Memory Simulation

```cpp
// Test that memory failures are handled gracefully
void test_oom_handling() {
    // Attempt to allocate huge vector
    PyObject *result = PyRun_SimpleString(
        "import cpp\n"
        "for i in range(1000000):\n"
        "    cpp.enemies.append_new()\n"  // Will eventually fail
    );
    
    // Should see MemoryError, not crash
}
```

### Test 2: Python Exception Propagation

```python
# controller.py - Test Python exceptions reaching C++
def update_values():
    raise ValueError("Test exception from Python")
    
# C++ should catch this and handle gracefully
```

### Test 3: Copy Constructor Failures

```cpp
struct ThrowingStruct {
    ThrowingStruct() = default;
    ThrowingStruct(const ThrowingStruct&) {
        static int count = 0;
        if (++count > 3) {
            throw std::runtime_error("Copy failed");
        }
    }
};

// Test that vector operations handle this
```

### Test 4: Nested Exception Scenarios

```python
# Test exception in nested Python → C++ → Python call
def callback():
    raise RuntimeError("Nested error")

# C++ calls Python which calls C++ which calls this
```

---

## Summary Recommendations

### For Your Project (Priority Order):

1. **🔴 CRITICAL - Fix Issue 50**
   - Add try-catch blocks to `generic_vec_append<T>()`
   - Add try-catch blocks to `generic_struct_construct<T>()`
   - Make `generic_struct_destruct<T>()` noexcept
   - **Estimated Time:** 2-3 hours
   - **Risk if Not Fixed:** Python interpreter crashes on allocation failures

2. **🟠 HIGH - Enhance Error Logging**
   - Implement `ErrorLogger` class
   - Log all caught exceptions
   - Add diagnostic information to aid debugging
   - **Estimated Time:** 1-2 hours
   - **Benefit:** Much easier to diagnose production issues

3. **🟡 MEDIUM - Add Exception Helper Utilities**
   - Implement `PythonErrorContext` class
   - Standardize exception handling patterns
   - **Estimated Time:** 1 hour
   - **Benefit:** Cleaner, more maintainable code

4. **🟢 NICE-TO-HAVE - Comprehensive Testing**
   - Add unit tests for exception scenarios
   - Stress test with allocation failures
   - Test all exception propagation paths
   - **Estimated Time:** 3-4 hours
   - **Benefit:** Confidence in error handling

---

## References

- [Python C API Exception Handling](https://docs.python.org/3/c-api/exceptions.html)
- [Python C API Return Values](https://docs.python.org/3/c-api/intro.html#return-values)
- [C++ Exception Safety](https://en.cppreference.com/w/cpp/language/exceptions)
- ISO C++ Standard: [except.spec] - Exception Specifications

---

**Document Status:** Complete  
**Next Review:** After Issue 50 implementation  
**Owner:** Development Team
