# Source Code Documentation

This document explains the source code structure, key functions and variables, and the overall design. It also lists improvement and enhancement ideas.

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
- `BoundStruct::get_field()` and `get_field_ptr()`
  - Resolves fields dynamically.

**Design perspective:**
- Uses offset-based access for speed and flexibility.
- Allows nested metadata (structs and vectors inside structs).

---

## 3) Design Perspective and Tradeoffs

- **Type erasure through metadata** keeps proxy code generic, avoiding templates in Python-facing layers.
- **Explicit ownership**: Proxies own wrappers (`BoundStruct`/`BoundVector`) but not underlying C++ data.
- **Zero-copy**: Operations modify C++ data in place.
- **Manual memory control** for nested vectors: placement-new and explicit cleanup prevent type mismatch issues.
- **Compatibility**: Configuration supports system, bundled, and zip-based Python for portability.

## 4) Known Limitations (Behavioral)

- No slicing support for vectors (`vec[1:3]`).
- No custom index support (`__index__` protocol).
- No string `repr` or `str` for proxies (debugging is raw object print).
- Iterator and len are implemented, but other Python container protocols remain limited.
- Thread safety is not guaranteed; access should be single-threaded unless synchronized.

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
- Add thread-safety guards around `PyInterface::g_values` access.
- Add optional bounds-checking diagnostics in debug builds.
- Extend metadata to store field documentation for auto-generated Python docs.

### Testing enhancements
- Add automated tests for iterator behavior, including empty vectors.
- Add stress tests with large nested vectors to measure performance.
- Add tests that validate string conversion for non-ASCII input.

## 6) Quick Reference

- Entry point: [main.cpp](main.cpp)
- Module and dynamic attribute resolution: [cpp_module.cpp](cpp_module.cpp)
- Proxy types: [python_proxy.cpp](python_proxy.cpp)
- Scalar bindings: [python_bind.hpp](python_bind.hpp)
- Binding interface: [value_interface.hpp](value_interface.hpp)
- Struct reflection: [reflection_struct.hpp](reflection_struct.hpp)
