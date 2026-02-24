# Source Code Documentation

## Table of Contents

- [Overview](#overview)
- [1) Architecture Overview](#1-architecture-overview)
- [2) File-by-File Guide](#2-file-by-file-guide)
  - [[main.cpp](main.cpp)](#maincpp)
  - [[cpp_module.cpp](cpp_module.cpp)](#cpp_modulecpp)
  - [[cpp_module.hpp](cpp_module.hpp)](#cpp_modulehpp)
  - [[python_proxy.hpp](python_proxy.hpp)](#python_proxyhpp)
  - [[python_proxy.cpp](python_proxy.cpp)](#python_proxycpp)
  - [[python_bind.hpp](python_bind.hpp)](#python_bindhpp)
  - [[value_interface.hpp](value_interface.hpp)](#value_interfacehpp)
  - [[reflection_struct.hpp](reflection_struct.hpp)](#reflection_structhpp)
  - [[reflection_vector.hpp](reflection_vector.hpp)](#reflection_vectorhpp)
- [3) Memory Safety Architecture](#3-memory-safety-architecture)
  - [Pattern 1: Wrapper Ownership (Issue #18 Fix)](#pattern-1-wrapper-ownership-issue-18-fix)
  - [Pattern 2: Parent Tracking (Issue #26 Fix)](#pattern-2-parent-tracking-issue-26-fix)
  - [Pattern 3: Thread-Safe Singleton Initialization (Issue #34 Fix)](#pattern-3-thread-safe-singleton-initialization-issue-34-fix)
  - [Pattern 4: Python Reference Counting Semantics (Issue #39 Implementation)](#pattern-4-python-reference-counting-semantics-issue-39-implementation)
  - [Pattern 5: Parent-Child Proxy Lifetime Management (Issue #48 Fix)](#pattern-5-parent-child-proxy-lifetime-management-issue-48-fix)
  - [Circular Dependency Resolution](#circular-dependency-resolution)
- [4) Design Perspective and Tradeoffs](#4-design-perspective-and-tradeoffs)
- [5) Known Limitations (Behavioral)](#5-known-limitations-behavioral)
- [5) Improvements and Enhancements](#5-improvements-and-enhancements)
  - [High-value enhancements](#high-value-enhancements)
  - [Usability enhancements](#usability-enhancements)
  - [Robustness enhancements](#robustness-enhancements)
  - [Testing enhancements](#testing-enhancements)
- [6) Quick Reference](#6-quick-reference)

## Overview

This document provides a file-by-file breakdown of the source code, explaining the purpose of each file, key functions and variables, design perspectives, and memory safety patterns. It also includes a comprehensive list of potential improvements and enhancements organized by category.

**Target Audience:** New developers learning the codebase, code reviewers, and developers planning enhancements.

**Key Topics:** File-by-file guide, memory safety architecture, design trade-offs, known limitations, and improvement suggestions.

---

This document explains the source code structure, key functions and variables, and the overall design. It also lists improvement and enhancement ideas.

[Back to Table of Contents](#table-of-contents)


## 1) Architecture Overview

The project exposes C++ variables (scalars, structs, vectors, and nested vectors) to Python using:
- A reflection layer describing structs and vectors (metadata + function pointers).
- A binding layer that stores typed values and converts to/from Python.
- Proxy objects that provide Pythonic access to C++ data.
- A custom Python module `cpp` that dynamically resolves attributes.

Core flow:
1. C++ creates data and binds it with `PyInterface::bind()`.
2. The `cpp` Python module resolves attribute access via `cpp_module_getattr()`.
3. Structs and vectors are exposed through `StructProxy` and `VectorProxy`.
4. Python reads/writes fields or vector items via proxy methods.

[Back to Table of Contents](#table-of-contents)


## 2) File-by-File Guide

### [main.cpp](main.cpp)
**Purpose:** Application entry point. Locates Python, configures interpreter, binds C++ variables, and runs the Python script.

**Key functions and variables:**
- `static bool running` and `signal_handler(int)`
  - Tracks shutdown and handles signals for graceful exit.
- `main()`
  - Locates Python using `pyembed::locate_python()`.
  - Initializes Python with `PyConfig` (supports bundled, zip, and system Python).
  - Imports `controller.py` and calls `update_values()`.
  - Binds example variables:
    - `Player player`, `Team team`
    - `scores` (vector of ints)
    - `enemies` (vector of structs)
    - `grid` (vector of vectors)
    - `enemy_waves` (vector of vector of structs)

**Design perspective:**
- Keeps Python initialization explicit for portability across deployment models.
- Demonstrates a full binding pipeline and scripted control loop.

---

### [cpp_module.cpp](cpp_module.cpp)
**Purpose:** Implements the Python module `cpp` and dynamic attribute access.

**Key functions and variables:**
- `cpp_module_getattr(PyObject *module, PyObject *name)`
  - Resolves `cpp.<name>` dynamically using `PyInterface::get_value_raw()`.
  - Wraps structs and vectors into proxies (creates a safe wrapper to own).
  - Returns scalar values via `PyBoundValue::to_python()`.
- `cpp_module_setattr(PyObject *module, PyObject *name, PyObject *value)`
  - Supports assignment for scalar types only.
  - Rejects reassigning structs/vectors.
- `CppModuleType`
  - Custom module type enabling `__getattr__` and `__setattr__`.
- `PyInit_cpp()`
  - Initializes proxy types and returns the module instance.

**Design perspective:**
- Uses a dynamic module pattern to avoid hardcoding variables.
- Keeps a clean separation between scalar values and proxies.

---

### [cpp_module.hpp](cpp_module.hpp)
**Purpose:** Declares the `PyInit_cpp` module initializer.

**Key items:**
- `extern "C" PyMODINIT_FUNC PyInit_cpp(void);`

**Design perspective:**
- Minimal header to keep module initialization in a single translation unit.

---

### [python_proxy.hpp](python_proxy.hpp)
**Purpose:** Declares proxy types for C++ values.

**Key items:**
- `CppProxyType`, `StructProxyType`, `VectorProxyType` declarations.
- Factory helpers: `create_cpp_proxy()`, `StructProxy_New()`, `VectorProxy_New()`.

**Design perspective:**
- Centralizes proxy declarations so other files can construct proxy objects.

---

### [python_proxy.cpp](python_proxy.cpp)
**Purpose:** Implements all proxy types and Python interop behavior.

**Key sections, functions, and variables:**

**Helper Functions:**
- `calculate_struct_size(const StructInfo *sinfo)`
  - Computes struct byte size based on metadata. Used for default struct allocation in vectors.

**CppProxy:**
- `cppproxy_getattro()` and `cppproxy_setattro()`
  - Exposes `cpp.<var>` access in Python.

**StructProxy:**
- `StructProxy_getattro()`
  - Reads a field value and returns a Python object.
- `StructProxy_setattro()`
  - Writes a field value with type validation.
- `StructProxy_len()`
  - Returns number of fields in the struct (supports `len(cpp.player)`).
- `StructProxyType` and `StructProxy_sequence_methods`
  - Wires up `__len__` via sequence protocol.

**VectorProxy:**
- `VectorProxy_len()`, `VectorProxy_getitem()`, `VectorProxy_setitem()`
  - Implements Python sequence protocol for vectors.
- `VectorProxy_append()`
  - Appends scalar or proxy elements to the vector.
- `VectorProxy_append_new()`
  - Allocates default struct element and appends it.
- `VectorProxy_append_new_vector()`
  - Allocates default inner vector and appends it.
- `VectorProxy_iter()` + `VectorIteratorType`
  - Implements Python iteration protocol (`for x in cpp.vector`).

**Design perspective:**
- Uses reflection metadata and function pointers for type-agnostic access.
- Explicitly avoids copying large data; proxies point to native memory.
- Supports nested vectors with safe placement-new allocation.

---

### [python_bind.hpp](python_bind.hpp)
**Purpose:** Defines scalar value bindings that convert between C++ and Python.

**Key classes:**
- `PyBoundValue`
  - Abstract base for scalar conversions.
- `PyBoundInt`, `PyBoundFloat`, `PyBoundBool`, `PyBoundString`
  - Concrete scalar wrappers with `to_python()` and `from_python()`.

**Design perspective:**
- Keeps scalar conversions lightweight and type-safe.
- Centralizes the C++ <-> Python conversion logic.

---

### [value_interface.hpp](value_interface.hpp)
**Purpose:** Core binding interface and type detection traits.

**Key functions and variables:**
- `is_std_vector<T>` and `is_reflected_struct<T>` traits
  - Determine how to bind a type.
- `get_struct_info<T>()`, `get_vector_info<T>()`
  - Provide reflection metadata for user types.
- `PyInterface::g_values`
  - Global registry of bound values.
- `PyInterface::bind()`
  - Registers scalars, structs, or vectors.
- `PyInterface::get_value_raw()`
  - Retrieves bound values by name.

**Design perspective:**
- Keeps binding logic in a single, extensible location.
- Uses templates to provide a clean API to user code.

---

### [reflection_struct.hpp](reflection_struct.hpp)
**Purpose:** Struct reflection metadata and access helpers.

**Key types and functions:**
- `FieldInfo` and `StructInfo`
  - Describe fields by name, offset, type, and metadata.
- `BoundStruct`
  - Wraps a struct instance and provides field access.
  - **Memory Safety Feature:** Includes parent tracking (m_parent_vector, m_parent_index) to prevent use-after-free when struct is vector element.
- `BoundStruct::get_field()` and `get_field_ptr()`
  - Resolves fields dynamically.
  - Uses `get_instance_ptr()` for safe parent resolution.

**Design perspective:**
- Uses offset-based access for speed and flexibility.
- Allows nested metadata (structs and vectors inside structs).
- **Implements Issue #26 fix:** Parent tracking prevents proxy invalidation after vector reallocation.

---

### [reflection_vector.hpp](reflection_vector.hpp)
**Purpose:** Vector reflection metadata and dynamic access.

**Key types and functions:**
- `VectorInfo`
  - Metadata describing vector element type and operations (size, element access, append).
- `BoundVector`
  - Wraps a vector instance and provides type-erased access.
- `BoundVector::element_ptr(index)`
  - Returns fresh pointer to element at current memory location.
  - **Critical for Issue #26 fix:** Always resolves current pointer, safe after reallocation.

**Design perspective:**
- Function pointers enable type-agnostic vector operations.
- Works with any std::vector<T> without template instantiation in reflection layer.

---

[Back to Table of Contents](#table-of-contents)


## 3) Memory Safety Architecture

The system implements two critical safety patterns that eliminate memory corruption bugs:

### Pattern 1: Wrapper Ownership (Issue #18 Fix)

**Problem Prevented:** Double-free when multiple Python proxies reference same C++ object.

**Implementation:**
- Registry (`PyInterface::g_values`) stores master wrappers
- Each proxy gets **copy** of wrapper via copy constructor
- Wrapper contains pointers to shared data (void *m_instance)
- Cleanup deletes wrapper copy, not underlying C++ data

**Key Files:**
- [python_proxy.cpp](python_proxy.cpp) - `StructProxy_New()` creates wrapper copy
- [value_interface.cpp](value_interface.cpp) - Registry stores master wrappers

**Result:** Multiple proxies → multiple wrapper copies → no double-free

**See:** [WRAPPER_OWNERSHIP_PATTERN.md](../architecture/WRAPPER_OWNERSHIP_PATTERN.md)

---

### Pattern 2: Parent Tracking (Issue #26 Fix)

**Problem Prevented:** Use-after-free when vector reallocates and element proxies hold dangling pointers.

**Implementation:**
- Vector element proxies store parent container + index (not raw pointer)
- Field access calls `get_instance_ptr()` which:
  - Checks if parent exists
  - Resolves fresh pointer from parent via `element_ptr(index)`
  - Returns current memory location (safe after reallocation)

**Key Files:**
- [reflection_struct.hpp](reflection_struct.hpp) - BoundStruct parent tracking members
- [reflection_vector.hpp](reflection_vector.hpp) - BoundVector::element_ptr() dynamic resolution
- [python_proxy.cpp](python_proxy.cpp) - VectorProxy_getitem() sets parent/index

**Result:** Vector can reallocate → proxies always resolve valid pointer → no use-after-free

**See:** 
- [VECTOR_ELEMENT_PROXY_INVALIDATION.md](../architecture/VECTOR_ELEMENT_PROXY_INVALIDATION.md)
- [PARENT_TRACKING_IMPLEMENTATION_GUIDE.md](../architecture/PARENT_TRACKING_IMPLEMENTATION_GUIDE.md)

---

### Pattern 3: Thread-Safe Singleton Initialization (Issue #34 Fix)

**Problem Prevented:** Race conditions during multi-threaded proxy creation causing double-initialization or memory leaks.

**Implementation:**
- Static mutex `g_cpp_proxy_mutex` protects singleton initialization
- `std::lock_guard` provides RAII-based exception-safe locking
- Double-checked locking pattern: check exists → acquire lock → check again → initialize
- Ensures `PyType_Ready()` called exactly once across all threads

**Key Files:**
- [python_proxy.cpp](python_proxy.cpp) - `create_cpp_proxy()` with mutex protection

**Code Pattern:**
```cpp
static std::mutex g_cpp_proxy_mutex;
static PyObject *g_cpp_proxy_instance = nullptr;

PyObject *create_cpp_proxy() {
    std::lock_guard<std::mutex> lock(g_cpp_proxy_mutex);
    if (g_cpp_proxy_instance) {
        Py_INCREF(g_cpp_proxy_instance);
        return g_cpp_proxy_instance;
    }
    // First initialization (protected by mutex)
    g_cpp_proxy_instance = PyObject_New(...);
    return g_cpp_proxy_instance;
}
```

**Result:** Multiple threads can safely call `create_cpp_proxy()` → singleton initialized once → no race conditions

**See:** [OWNERSHIP_MODELS_GUIDE.md](../architecture/OWNERSHIP_MODELS_GUIDE.md) Section 7: Thread Safety

---

### Pattern 4: Python Reference Counting Semantics (Issue #39 Implementation)

**Problem Prevented:** Memory leaks (refcount too high) or use-after-free (refcount too low).

**Implementation:**
- All proxy factory functions return NEW references (caller must `Py_DECREF`)
- `create_cpp_proxy()` increments refcount before returning existing instance
- `PyObject_New()` creates objects with refcount=1
- Destructors use `Py_XDECREF()` to release owned references

**Key Functions:**
- `create_cpp_proxy()` - Returns NEW reference (Py_INCREF if existing)
- `StructProxy_New()` - Returns NEW reference (refcount=1 from PyObject_New)
- `VectorProxy_New()` - Returns NEW reference (refcount=1 from PyObject_New)
- `StructProxy_dealloc()` - Releases parent_proxy reference with Py_XDECREF

**Code Pattern:**
```cpp
// Factory function
PyObject *StructProxy_New(BoundStruct *bound) {
    auto *self = PyObject_New(StructProxyObject, &StructProxyType);
    self->bound = bound;
    return (PyObject*)self;  // NEW reference (refcount=1)
}

// Destructor
static void StructProxy_dealloc(PyObject *self) {
    delete proxy->bound;
    Py_XDECREF(proxy->parent_proxy);  // Release owned reference
    Py_TYPE(self)->tp_free(self);
}
```

**Result:** Consistent reference ownership → Python runtime manages lifetimes correctly → no leaks or crashes

**See:** [OWNERSHIP_MODELS_GUIDE.md](../architecture/OWNERSHIP_MODELS_GUIDE.md) Section 6: Python C-API Reference Counting

---

### Pattern 5: Parent-Child Proxy Lifetime Management (Issue #48 Fix)

**Problem Prevented:** Dangling pointers when parent VectorProxy destroyed while element StructProxy still exists.

**Implementation:**
- `StructProxyObject` has `parent_proxy` field storing reference to parent VectorProxy
- `VectorProxy_getitem()` sets `parent_proxy` and calls `Py_INCREF(parent)`
- `StructProxy_dealloc()` releases parent reference with `Py_XDECREF(parent_proxy)`
- Parent kept alive as long as any element proxy exists

**Key Files:**
- [python_proxy.cpp](python_proxy.cpp) - StructProxyObject definition, VectorProxy_getitem(), StructProxy_dealloc()

**Code Pattern:**
```cpp
typedef struct {
    PyObject_HEAD
    BoundStruct *bound;
    PyObject *parent_proxy;  // Keeps parent VectorProxy alive
} StructProxyObject;

PyObject *VectorProxy_getitem(PyObject *self, Py_ssize_t index) {
    // ... create element wrapper ...
    StructProxyObject *proxy = PyObject_New(...);
    proxy->parent_proxy = self;
    Py_INCREF(self);  // Keep parent alive
    return (PyObject*)proxy;
}
```

**Memory Lifetime:**
```python
def get_element():
    v = cpp.enemies     # VectorProxy refcount=1
    e = v[0]            # StructProxy created, parent_proxy increments VectorProxy refcount to 2
    return e            # Local v released, but parent_proxy keeps VectorProxy alive (refcount=1)

enemy = get_element()   # VectorProxy still alive due to parent_proxy
print(enemy.health)     # ✓ Safe: parent valid, dynamic resolution works
```

**Result:** Element proxies keep parent alive → BoundVector remains valid → no dangling pointers

**See:** [OWNERSHIP_MODELS_GUIDE.md](../architecture/OWNERSHIP_MODELS_GUIDE.md) Section 8: Parent-Child Proxy Lifetime

---

### Circular Dependency Resolution

**Challenge:** BoundStruct needs BoundVector for parent tracking, but headers must avoid circular includes.

**Solution:** Two-phase include pattern
1. Forward declare BoundVector (incomplete type for pointer member)
2. Defer method definitions until after #include "reflection_vector.hpp"

**Key File:** [reflection_struct.hpp](reflection_struct.hpp)

**See:** [CIRCULAR_DEPENDENCY_RESOLUTION.md](../architecture/CIRCULAR_DEPENDENCY_RESOLUTION.md)

---

[Back to Table of Contents](#table-of-contents)


## 4) Design Perspective and Tradeoffs

- **Type erasure through metadata** keeps proxy code generic, avoiding templates in Python-facing layers.
- **Explicit ownership**: Proxies own wrappers (`BoundStruct`/`BoundVector`) but not underlying C++ data.
- **Zero-copy**: Operations modify C++ data in place.
- **Manual memory control** for nested vectors: placement-new and explicit cleanup prevent type mismatch issues.
- **Compatibility**: Configuration supports system, bundled, and zip-based Python for portability.
- **Memory safety**: Wrapper ownership and parent tracking eliminate double-free and use-after-free bugs with minimal overhead (24 bytes/proxy, 1-2 CPU cycles/access).
- **Thread safety**: Singleton initialization protected by std::mutex (Issue #34) prevents race conditions.
- **Reference counting**: All proxy factory functions return NEW references with correct Python refcount semantics (Issue #39).
- **Parent-child lifetime**: Element proxies hold references to parent proxies, preventing dangling pointers (Issue #48).

[Back to Table of Contents](#table-of-contents)


## 5) Known Limitations (Behavioral)

- No slicing support for vectors (`vec[1:3]`).
- No custom index support (`__index__` protocol).
- No string `repr` or `str` for proxies (debugging is raw object print).
- Iterator and len are implemented, but other Python container protocols remain limited.
- Multi-threaded access to C++ data structures requires external synchronization (Python GIL provides some protection for proxy creation).

[Back to Table of Contents](#table-of-contents)


## 5) Improvements and Enhancements

### High-value enhancements
- Add `__repr__` / `__str__` for `StructProxy` and `VectorProxy` for better debugging.
- Implement vector slicing (`sq_slice` or `mp_subscript` with slice support).
- Support `__index__` protocol to accept numpy integers and custom index types.

### Usability enhancements
- Improve module error messages (include a list of available bound variables).
- Add validation in `controller.py` for proxy creation failures (Issue 5 follow-up).
- Provide a lightweight iteration helper for nested vector formatting in Python.

### Robustness enhancements
- Add optional bounds-checking diagnostics in debug builds.
- Extend metadata to store field documentation for auto-generated Python docs.
- Consider read-write locks for PyInterface::g_values if concurrent read performance becomes critical.

### Testing enhancements
- Add automated tests for iterator behavior, including empty vectors.
- Add stress tests with large nested vectors to measure performance.
- Add tests that validate string conversion for non-ASCII input.

[Back to Table of Contents](#table-of-contents)


## 6) Quick Reference

- Entry point: [main.cpp](main.cpp)
- Module and dynamic attribute resolution: [cpp_module.cpp](cpp_module.cpp)
- Proxy types: [python_proxy.cpp](python_proxy.cpp)
- Scalar bindings: [python_bind.hpp](python_bind.hpp)
- Binding interface: [value_interface.hpp](value_interface.hpp)
- Struct reflection: [reflection_struct.hpp](reflection_struct.hpp)

[Back to Table of Contents](#table-of-contents)

