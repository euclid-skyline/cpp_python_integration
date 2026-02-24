# Comprehensive Function Reference: Data Flow and Implementation Details

## Table of Contents

- [Overview](#overview)
- [I. Entry Point and Initialization](#i-entry-point-and-initialization)
  - [main.cpp: Application Lifecycle](#maincpp-application-lifecycle)
    - [`int main()`](#int-main)
- [II. Module and Dynamic Attribute Resolution](#ii-module-and-dynamic-attribute-resolution)
  - [cpp_module.cpp: Python Module Interface](#cpp_modulecpp-python-module-interface)
    - [`static PyObject *cpp_module_getattr(PyObject *module, PyObject *name)`](#static-pyobject-cpp_module_getatttrpyobject-module-pyobject-name)
    - [`static PyObject *cpp_module_setattr(PyObject *module, PyObject *name, PyObject *value)`](#static-pyobject-cpp_module_setattrpyobject-module-pyobject-name-pyobject-value)
    - [`static PyObject *PyInit_cpp(void)`](#static-pyobject-pyinit_cppvoid)
- [III. Structure Reflection and Field Access](#iii-structure-reflection-and-field-access)
  - [reflection_struct.hpp: Metadata and Bound Structures](#reflection_structhpp-metadata-and-bound-structures)
    - [`const FieldInfo *StructInfo::get_field(const std::string &name)`](#const-fieldinfo-structinfoget_fieldconst-stdstring-name)
    - [`void *BoundStruct::get_field_ptr(const FieldInfo *f)`](#void-boundstructget_field_ptrconst-fieldinfo-f)
  - [python_proxy.cpp: Structure Proxy Implementation](#python_proxycpp-structure-proxy-implementation)
    - [`PyObject *StructProxy_getattro(PyObject *self, PyObject *attr)`](#pyobject-structproxy_getatttropyobject-self-pyobject-attr)
    - [`PyObject *StructProxy_setattro(PyObject *self, PyObject *attr, PyObject *value)`](#pyobject-structproxy_setatttropyobject-self-pyobject-attr-pyobject-value)
    - [`Py_ssize_t StructProxy_len(PyObject *self)`](#py_ssize_t-structproxy_lenpyobject-self)
- [IV. Vector Reflection and Element Access](#iv-vector-reflection-and-element-access)
  - [reflection_vector.hpp: Vector Metadata](#reflection_vectorhpp-vector-metadata)
    - [`std::size_t BoundVector::size()`](#stdsize_t-boundvectorsize)
    - [`void *BoundVector::element_ptr(std::size_t index)`](#void-boundvectorelement_ptrstdsize_t-index)
  - [python_proxy.cpp: Vector Proxy Implementation](#python_proxycpp-vector-proxy-implementation)
    - [`PyObject *VectorProxy_getitem(PyObject *self, Py_ssize_t index)`](#pyobject-vectorproxy_getitempyobject-self-py_ssize_t-index)
    - [`PyObject *VectorProxy_setitem(PyObject *self, Py_ssize_t index, PyObject *value)`](#pyobject-vectorproxy_setitempyobject-self-py_ssize_t-index-pyobject-value)
    - [`Py_ssize_t VectorProxy_len(PyObject *self)`](#py_ssize_t-vectorproxy_lenpyobject-self)
    - [`PyObject *VectorProxy_append(PyObject *self, PyObject *args)`](#pyobject-vectorproxy_appendpyobject-self-pyobject-args)
    - [`PyObject *VectorProxy_append_new(PyObject *self, PyObject *args)`](#pyobject-vectorproxy_append_newpyobject-self-pyobject-args)
    - [`PyObject *VectorProxy_append_new_vector(PyObject *self, PyObject *args)`](#pyobject-vectorproxy_append_new_vectorpyobject-self-pyobject-args)
  - [Iterator Protocol Implementation](#iterator-protocol-implementation)
    - [`PyObject *VectorProxy_iter(PyObject *self)`](#pyobject-vectorproxy_iterpyobject-self)
    - [`PyObject *VectorIterator_next(PyObject *self)`](#pyobject-vectoriterator_nextpyobject-self)
- [V. Type Conversion and Binding](#v-type-conversion-and-binding)
  - [value_interface.hpp: Registry and Binding](#value_interfacehpp-registry-and-binding)
    - [`template <typename T> static void PyInterface::bind(const std::string &name, T &variable)`](#template-typename-t-static-void-pyinterfacebindconst-stdstring-name-t-variable)
    - [`static BoundValue *PyInterface::get_value_raw(const std::string &name)`](#static-boundvalue-pyinterfaceget_value_rawconst-stdstring-name)
  - [python_bind.hpp: Scalar Type Conversions](#python_bindhpp-scalar-type-conversions)
    - [`PyObject *PyBoundInt::to_python()`](#pyobject-pyboundintto_python)
    - [`bool PyBoundInt::from_python(PyObject *obj)`](#bool-pyboundinttfrom_pythonpyobject-obj)
    - [`PyObject *PyBoundString::to_python()`](#pyobject-pyboundstringto_python)
    - [`bool PyBoundString::from_python(PyObject *obj)`](#bool-pyboundstringfrom_pythonpyobject-obj)
- [VI. Memory Management and Lifetime](#vi-memory-management-and-lifetime)
  - [Ownership Model](#ownership-model)
  - [No Heap Allocation in Reflection Layer](#no-heap-allocation-in-reflection-layer)
  - [Critical Memory Safety Patterns](#critical-memory-safety-patterns)
    - [Pattern 1: Wrapper Ownership (Prevents Double-Free)](#pattern-1-wrapper-ownership-prevents-double-free)
    - [Pattern 2: Parent Tracking for Vector Elements (Prevents Use-After-Free)](#pattern-2-parent-tracking-for-vector-elements-prevents-use-after-free)
    - [Pattern 3: Thread-Safe Singleton Initialization (Issue #34)](#pattern-3-thread-safe-singleton-initialization-issue-34)
    - [Pattern 4: Python Reference Counting Semantics (Issue #39)](#pattern-4-python-reference-counting-semantics-issue-39)
    - [Pattern 5: Parent-Child Proxy Lifetime Management (Issue #48)](#pattern-5-parent-child-proxy-lifetime-management-issue-48)
- [VII. Summary: Data Flow Example](#vii-summary-data-flow-example)
  - [Complete Example Walkthrough: `cpp.enemies[0].health = 50`](#complete-example-walkthrough-cppenemies0health--50)
  - [Memory Map at Execution](#memory-map-at-execution)

## Overview

This document provides function-by-function implementation details and data flow analysis for every major component of the C++/Python integration framework. Each function is explained with execution flow diagrams, parameter descriptions, return value semantics, and memory management patterns.

**Target Audience:** Developers implementing features, debugging issues, or understanding how specific operations work internally.

**Key Topics:** Function execution flows, data flow through the system, type conversion mechanics, memory management patterns, and complete end-to-end operation walkthroughs.

---

[Back to Table of Contents](#table-of-contents)


## I. Entry Point and Initialization

### main.cpp: Application Lifecycle

#### `int main()`

**Purpose:** Orchestrate entire application: locate Python, configure interpreter, bind variables, run control script.

**Execution Flow:**

```
1. Signal handler registration
   └─→ signal(SIGINT/SIGTERM/...) maps to exit handler

2. Python location
   └─→ pyembed::locate_python()
       Tries: bundled → registry → environment variables → system
       Returns: Python installation path with version

3. Python configuration
   ├─→ PyConfig_InitPythonConfig(&config)
   ├─→ Set search paths for modules
   ├─→ Optionally disable SITE_PACKAGES (sandboxing)
   └─→ Py_InitializeFromConfig(&config)

4. Module registration
   └─→ PyImport_AppendInittab("cpp", PyInit_cpp)
       Makes our custom cpp module available to import

5. Variable binding
   ├─→ PyInterface::bind("player", player_instance)
   │   └─→ Creates BoundStruct, stores in g_values["player"]
   ├─→ PyInterface::bind("scores", scores_vector)
   │   └─→ Creates BoundVector, stores in g_values["scores"]
   ├─→ PyInterface::bind("enemies", enemies_vector)
   ├─→ PyInterface::bind("grid", nested_vector)
   └─→ PyInterface::bind("enemy_waves", nested_struct_vector)

6. Script execution
   ├─→ PyRun_SimpleString("import controller")
   ├─→ Controller calls cpp.update_values()
   └─→ Loop: update, render, sleep

7. Shutdown
   ├─→ Py_FinalizeEx()
   └─→ return 0;
```

**Memory Management:**
- `running` flag: `static bool` (stack)
- `player`, `team`: Stack objects (auto-freed at scope end)
- `scores`, `enemies`: Vector capacity on heap, managed by std::vector
- `PyInterface::g_values`: Global map, destroyed at program exit

**Key Decision: Why Explicit Configuration?**
- Supports multiple Python deployment models (bundled, system, zip)
- Enables sandboxing by disabling system site-packages
- Works across Windows, Linux, macOS without platform-specific code

---

[Back to Table of Contents](#table-of-contents)


## II. Module and Dynamic Attribute Resolution

### cpp_module.cpp: Python Module Interface

#### `static PyObject *cpp_module_getattr(PyObject *module, PyObject *name)`

**Purpose:** Intercept `cpp.variable_name` access and return appropriate wrapper.

**Execution Flow:**

```
Input:  module (the cpp module), name (PyUnicode object)

Step 1: Extract C string from PyUnicode
        ├─→ const char *name_str = PyUnicode_AsUTF8(name)
        ├─→ if (!name_str) return NULL  (silently fail to Python)
        └─→ utf-8 string ready for lookup

Step 2: Look up in global registry
        ├─→ BoundValue *bound = PyInterface::get_value_raw(name_str)
        └─→ if (!bound) PyErr_Format(AttributeError, ...)

Step 3: Dynamic dispatch based on type
        ├─→ case ValueType::Int/Float/Bool/String:
        │   ├─→ Cast to PyBoundValue*
        │   ├─→ Call pv->to_python()
        │   └─→ Return converted PyObject* directly
        │
        ├─→ case ValueType::Struct:
        │   ├─→ Cast to BoundStruct*
        │   ├─→ Call StructProxy_New(bs)
        │   └─→ Return PyObject* wrapping the struct
        │
        └─→ case ValueType::Vector:
            ├─→ Cast to BoundVector*
            ├─→ Call VectorProxy_New(bv)
            └─→ Return PyObject* wrapping the vector

Output: PyObject* (Python object, NULL = error)
```

**Example Execution:**
```python
# User code:
import cpp
cpp.player

# Call sequence:
cpp_module_getattr(cpp_module, "player");
├─→ PyUnicode_AsUTF8("player") = "player"
├─→ PyInterface::get_value_raw("player") = BoundStruct*
├─→ dynamic_cast<BoundVector*>(...) = nullptr (not a vector)
├─→ dynamic_cast<PyBoundValue*>(...) = nullptr (not a scalar)
├─→ StructProxy_New(bound_struct)
└─→ Return StructProxy object

# User now has:
player = <StructProxy object>
```

#### `static PyObject *cpp_module_setattr(PyObject *module, PyObject *name, PyObject *value)`

**Purpose:** Handle `cpp.variable_name = value` assignments.

**Key Restriction:** Only allows modification of scalars (int, float, bool, string).

**Execution Flow:**

```
Input:  module (cpp), name (variable name), value (new value)

Step 1: Extract name string
        const char *name_str = PyUnicode_AsUTF8(name)

Step 2: Look up variable
        BoundValue *bound = PyInterface::get_value_raw(name_str)

Step 3: Type check – permit only scalars
        ├─→ if (bound->type == Int/Float/Bool/String): OK
        └─→ else: PyErr_Format("can't assign to struct/vector")

Step 4: Convert Python value to C++
        ├─→ Cast to PyBoundValue*
        ├─→ Call pv->from_python(value)
        └─→ Writes directly to original variable

Output: 0 (success) or -1 (error)
```

**Why No Struct/Vector Assignment?**
- Structs/vectors are complex objects with memory management
- Assigning entire struct is rare (modify fields instead)
- Prevents accidental replacement of vector capacity/allocation
- Encourages:
  ```python
  cpp.player.health = 100  # Modify specific field
  cpp.enemies[0].name = "boss"  # Modify via proxy
  ```

#### `static PyObject *PyInit_cpp(void)`

**Purpose:** Module initialization (required by Python C-API for `import cpp`).

**Execution Flow:**

```
Python calls: PyInit_cpp()  (when `import cpp` executes)

Step 1: Register all proxy type objects
        ├─→ PyType_Ready(&CppProxyType)
        ├─→ PyType_Ready(&StructProxyType)
        ├─→ PyType_Ready(&VectorProxyType)
        └─→ PyType_Ready(&VectorIteratorType)

Step 2: Create module
        ├─→ PyModule_Create(&cppmodule)
        └─→ Returns PyObject* (generic module)

Step 3: Change module type to custom type
        ├─→ PyObject_SetAttr(m, "__class__", (PyObject*)&CppModuleType)
        └─→ Now module uses our tp_getattro/tp_setattro!

Step 4: Return module
        return m;

Output: PyObject* (module)
```

**Key Detail: The Type Change**
- Standard Python modules have type `PyModule_Type`
- We replace it with `CppModuleType` (inherits from PyModule_Type)
- `CppModuleType` overrides getattr/setattr methods
- Result: Dynamic attribute access works!

---

[Back to Table of Contents](#table-of-contents)


## III. Structure Reflection and Field Access

### reflection_struct.hpp: Metadata and Bound Structures

#### `const FieldInfo *StructInfo::get_field(const std::string &name)`

**Purpose:** Look up field metadata by name.

**Implementation:**
```cpp
const FieldInfo *StructInfo::get_field(const std::string &name) const {
    for (const auto &field : fields) {
        if (field.name == name) return &field;
    }
    return nullptr;
}
```

**Performance:** O(n) where n = number of fields (typically 5-20)

**Optimization Opportunity:** Could use unordered_map for O(1), but fields are small/linear

#### `void *BoundStruct::get_field_ptr(const FieldInfo *f)`

**Purpose:** Calculate memory address of a field within the struct instance.

**Implementation:**
```cpp
void *BoundStruct::get_field_ptr(const FieldInfo *f) {
    return static_cast<void*>(
        static_cast<char*>(m_instance) + f->offset
    );
}
```

**Why Casting to char*?**
- void* arithmetic is undefined (doesn't know element size)
- char* arithmetic is 1 byte per increment (well-defined)
- `+ f->offset` adds bytes
- Cast back to void* for generic return

**Example:**
```cpp
// Given:
struct Player { int health; std::string name; };  // health at offset 0, name at offset 4
Player player_instance;
BoundStruct bs(&player_instance, &player_struct_info);

// Call:
const FieldInfo *health_field = player_struct_info.get_field("health");
void *health_ptr = bs.get_field_ptr(health_field);

// Calculation:
static_cast<char*>(&player_instance) + 0 = (char*)&player_instance
cast back to void* = &player_instance (unchanged)

// Actual type:
(int*)health_ptr == &player_instance.health
```

---

### python_proxy.cpp: Structure Proxy Implementation

#### `PyObject *StructProxy_getattro(PyObject *self, PyObject *attr)`

**Purpose:** Handle `struct_proxy.field_name` read access.

**Execution Flow (Detailed):**

```
Input:  self (StructProxy instance), attr (field name as PyUnicode)

Step 1: Unpack container structure
        auto *proxy = (StructProxy *)self;
        const BoundStruct *bound = proxy->bound;  // Points to BoundStruct

Step 2: Extract and validate field name
        const char *attr_name = PyUnicode_AsUTF8(attr);
        if (!attr_name) return nullptr;  // Invalid Unicode

Step 3: Metadata lookup
        const FieldInfo *field = bound->get_field(attr_name);
        if (!field) {
            return PyErr_Format(PyExc_AttributeError,
                "'Player' object has no attribute '%s'", attr_name);
        }

Step 4: Calculate field memory address
        void *field_ptr = bound->get_field_ptr(field);
        // field_ptr = m_instance + field->offset

Step 5: Type-dispatch conversion
        Based on field->type:
        
        case ValueType::Int:
            return PyLong_FromLong(*(int*)field_ptr);
        
        case ValueType::Float:
            return PyFloat_FromDouble(*(float*)field_ptr);
        
        case ValueType::Bool: {
            auto byte_val = *(ByteBool*)field_ptr;
            return PyBool_FromLong(byte_val == TRUE_BYTE ? 1 : 0);
        }
        
        case ValueType::String: {
            auto *str_ptr = static_cast<std::string*>(field_ptr);
            return PyUnicode_FromString(str_ptr->c_str());
        }
        
        case ValueType::Struct: {
            auto *si = static_cast<StructInfo*>(field->type_meta);
            BoundStruct *inner = new BoundStruct("field", field_ptr, si);
            return StructProxy_New(inner);
        }
        
        case ValueType::Vector: {
            auto *vi = static_cast<VectorInfo*>(field->type_meta);
            BoundVector *inner = new BoundVector("field", field_ptr, vi);
            return VectorProxy_New(inner);
        }

Output: PyObject* (Python value or NULL if error)
```

**Example Data Flow:**
```cpp
// C++ memory layout:
Player player = { health: 100, name: "Alice" };

// Python execution:
py_player.health

// Execution:
StructProxy_getattro(py_player_proxy, "health")
├─→ bound = &py_player_proxy->bound  // Points to BoundStruct
├─→ field = bound->get_field("health")  // FieldInfo { offset: 0, type: Int }
├─→ field_ptr = bound->get_field_ptr(field)  // Pointer arithmetic: &player + 0
├─→ value = *(int*)field_ptr  // Dereference: 100
└─→ return PyLong_FromLong(100)

# Python result: 100 (int)
```

#### `PyObject *StructProxy_setattro(PyObject *self, PyObject *attr, PyObject *value)`

**Purpose:** Handle `struct_proxy.field_name = value` write access.

**Execution Flow:**

```
Input:  self (StructProxy), attr (field name), value (new value or NULL for delete)

Step 1: Check delete operation
        if (value == NULL) {
            return PyErr_Format(PyExc_TypeError, "can't delete attribute");
        }

Step 2-3: Same as getattro – lookup field
        [... field validation omitted for brevity ...]

Step 4: Type validation
        switch (field->type) {
            case ValueType::Int:
                Check: PyLong_Check(value)
            case ValueType::Float:
                Check: PyFloat_Check(value) || PyLong_Check(value)
            case ValueType::Bool:
                Check: PyBool_Check(value)
            case ValueType::String:
                Check: PyUnicode_Check(value)
            default:
                Reject structs and vectors (don't reassign complex objects)
        }

Step 5: Type-specific conversion and write
        case ValueType::Int: {
            long val = PyLong_AsLong(value);
            *(int*)field_ptr = (int)val;
        }
        
        case ValueType::String: {
            const char *str = PyUnicode_AsUTF8(value);
            auto *s = static_cast<std::string*>(field_ptr);
            *s = str;  // std::string copy
        }
        // ... similar for Float, Bool ...

Output: 0 (success) or -1 (error)
```

#### `Py_ssize_t StructProxy_len(PyObject *self)`

**Purpose:** Support `len(struct_proxy)` – returns number of fields.

**Implementation:**
```cpp
Py_ssize_t StructProxy_len(PyObject *self) {
    auto *proxy = (StructProxy *)self;
    return (Py_ssize_t)proxy->bound->info()->fields.size();
}
```

**Usage Example:**
```python
player_fields = len(cpp.player)  # Returns 3 if Player has 3 fields
```

---

[Back to Table of Contents](#table-of-contents)


## IV. Vector Reflection and Element Access

### reflection_vector.hpp: Vector Metadata

#### `std::size_t BoundVector::size()`

**Purpose:** Get element count in vector.

**Implementation:**
```cpp
std::size_t BoundVector::size() const {
    return m_info->size_fn ? m_info->size_fn(m_vec_ptr) : 0;
}
```

**Where size_fn Comes From:**
- Defined at compile-time for each vector type
- Example for `std::vector<int>`:
  ```cpp
  [](void *ptr) { return static_cast<std::vector<int>*>(ptr)->size(); }
  ```

#### `void *BoundVector::element_ptr(std::size_t index)`

**Purpose:** Get memory address of element at given index.

**Implementation:**
```cpp
void *BoundVector::element_ptr(std::size_t index) const {
    return m_info->element_ptr_fn ? m_info->element_ptr_fn(m_vec_ptr, index) : nullptr;
}
```

**Example:** For `std::vector<Player>`:
```cpp
std::vector<Player> *vec = ...;
element_ptr_fn = [](void *ptr, size_t i) {
    auto *v = static_cast<std::vector<Player>*>(ptr);
    return &(*v)[i];  // Return address of element
};
```

---

### python_proxy.cpp: Vector Proxy Implementation

#### `PyObject *VectorProxy_getitem(PyObject *self, Py_ssize_t index)`

**Purpose:** Handle `vector_proxy[i]` read access.

**Execution Flow:**

```
Input:  self (VectorProxy), index (integer index, may be negative)

Step 1: Get bounds information
        auto *proxy = (VectorProxy *)self;
        const BoundVector *bound = proxy->bound;
        Py_ssize_t size = (Py_ssize_t)bound->size();

Step 2: Handle negative indexing
        if (index < 0) index += size;
        // Example: index = -1, size = 5 → index = 4 (last element)

Step 3: Validate bounds
        if (index < 0 || index >= size) {
            return PyErr_Format(PyExc_IndexError, "index out of range");
        }

Step 4: Get element memory address
        void *elem_ptr = bound->element_ptr(index);

Step 5: Type-dispatch conversion
        Based on bound->info()->element_type:
        
        case ValueType::Int:
            return PyLong_FromLong(*(int*)elem_ptr);
        
        case ValueType::Float:
            return PyFloat_FromDouble(*(float*)elem_ptr);
        
        case ValueType::Struct: {
            auto *si = static_cast<StructInfo*>(bound->info()->element_meta);
            BoundStruct *bs = new BoundStruct("elem", elem_ptr, si);
            return StructProxy_New(bs);
        }
        
        case ValueType::Vector: {
            auto *vi = static_cast<VectorInfo*>(bound->info()->element_meta);
            BoundVector *bv = new BoundVector("elem", elem_ptr, vi);
            return VectorProxy_New(bv);
        }

Output: PyObject* (Python value representation)
```

**Example - Access Nested Vector:**
```python
# C++: std::vector<std::vector<int>> grid = { {1, 2}, {3, 4} }
# Python:

cpp.grid[0]          # Calls VectorProxy_getitem with index=0
├─→ elem_ptr = &grid[0]  // Address of first inner vector
├─→ element_type = Vector
├─→ Create new BoundVector wrapping &grid[0]
└─→ Return VectorProxy wrapping that

cpp.grid[0][1]       # Calls VectorProxy_getitem on inner proxy with index=1
├─→ elem_ptr = &grid[0][1]  // Address of second element: 2
├─→ element_type = Int
└─→ Return PyLong(2)

# Result: 2
```

#### `PyObject *VectorProxy_setitem(PyObject *self, Py_ssize_t index, PyObject *value)`

**Purpose:** Handle `vector_proxy[i] = value` write access.

**Execution Flow (Key Parts):**

```
Input:  self (VectorProxy), index, value

[... validation and indexing same as getitem ...]

Step 4: Type-specific assignment
        
        case ValueType::Int: {
            if (!PyLong_Check(value)) return error;
            long v = PyLong_AsLong(value);
            *(int*)elem_ptr = (int)v;
            return 0;
        }
        
        case ValueType::String: {
            if (!PyUnicode_Check(value)) return error;
            const char *str = PyUnicode_AsUTF8(value);
            auto *s = static_cast<std::string*>(elem_ptr);
            *s = str;  // Copy assign
            return 0;
        }
        
        case ValueType::Struct: {
            if (!PyObject_TypeCheck(value, &StructProxyType)) return error;
            auto *value_proxy = (StructProxy *)value;
            auto *elem_struct = static_cast<Player*>(elem_ptr);
            *elem_struct = *BoundStruct::instance(value_proxy->bound);
            return 0;
        }

Output: 0 (success) or -1 (error)
```

#### `Py_ssize_t VectorProxy_len(PyObject *self)`

**Purpose:** Support `len(vector_proxy)`.

**Implementation:**
```cpp
Py_ssize_t VectorProxy_len(PyObject *self) {
    auto *proxy = (VectorProxy *)self;
    return (Py_ssize_t)proxy->bound->size();
}
```

#### `PyObject *VectorProxy_append(PyObject *self, PyObject *args)`

**Purpose:** Support `vector_proxy.append(value)`.

**Execution Flow:**

```
Input:  self (VectorProxy), args (Python tuple of arguments, expect 1 element)

Step 1: Parse arguments
        PyObject *value = nullptr;
        if (!PyArg_ParseTuple(args, "O", &value)) return nullptr;

Step 2: Type-dispatch append based on element type
        const VectorInfo *vinfo = bound->info();
        
        case ValueType::Int: {
            if (!PyLong_Check(value)) raise TypeError;
            int v = PyLong_AsLong(value);
            bool ok = bound->append_from_cpp(&v);
        }
        
        case ValueType::Struct: {
            if (!PyObject_TypeCheck(value, &StructProxyType)) raise TypeError;
            auto *proxy = (StructProxy *)value;
            Player instance = *BoundStruct::instance(proxy->bound);
            bool ok = bound->append_from_cpp(&instance);
        }
        
        case ValueType::Vector: {
            if (!PyObject_TypeCheck(value, &VectorProxyType)) raise TypeError;
            auto *proxy = (VectorProxy *)value;
            [... complex nested logic ...]
            bool ok = bound->append_from_cpp(&inner_vector);
        }

Output: None (Python None object) or NULL (error)
```

#### `PyObject *VectorProxy_append_new(PyObject *self, PyObject *args)`

**Purpose:** Allocate default struct element and append.

**Example Use Case:**
```python
enemies.append_new()  # Creates Enemy() with default values and appends
```

**Execution Flow:**

```
Input:  self (VectorProxy)

Step 1: Verify element type is Struct
        if (vinfo->element_type != ValueType::Struct) raise TypeError;

Step 2: Allocate memory for new struct
        const StructInfo *si = static_cast<StructInfo*>(vinfo->element_meta);
        size_t elem_size = calculate_struct_size(si);
        void *new_elem = malloc(elem_size);

Step 3: Initialize with defaults
        new_elem should be zero-initialized for:
        - Int fields: 0
        - Float fields: 0.0
        - Bool fields: FALSE_BYTE
        - String fields: empty string (requires placement-new)
        - Struct fields: recursively initialized
        
        for each field in si->fields:
            if field.type == String:
                new (&field_at_offset) std::string();  // Placement-new!

Step 4: Append to vector
        bool ok = bound->append_from_cpp(new_elem);

Step 5: Clean up and return
        free(new_elem);  // Vector made its own copy
        return StructProxy_New(new BoundStruct(...));

Output: StructProxy (proxy wrapping newly appended element)
```

**Memory Flow:**
```
Stack buffer with default values
    ↓
Copy to vector (vector's copy-assignment)
    ↓
Free stack buffer
    ↓
Return proxy to newly-created vector element
```

#### `PyObject *VectorProxy_append_new_vector(PyObject *self, PyObject *args)`

**Purpose:** Allocate default vector element and append (for nested vectors).

**Example Use Case:**
```python
grid.append_new()  # Appends empty vector<int>()
```

**Key Insight:** Uses VectorInfo's function pointers to allocate inner vector

---

### Iterator Protocol Implementation

#### `PyObject *VectorProxy_iter(PyObject *self)`

**Purpose:** Support `iter(vector_proxy)` and `for item in vector_proxy:` loops.

**Implementation:**
```cpp
PyObject *VectorProxy_iter(PyObject *self) {
    auto *it = PyObject_New(VectorIteratorObject, &VectorIteratorType);
    it->vector = (VectorProxy*)self;
    it->index = 0;
    Py_INCREF((PyObject*)self);  // Keep vector alive for duration of iteration
    return (PyObject*)it;
}
```

**Python Behavior Enabled:**
```python
for enemy in cpp.enemies:
    print(enemy.name)

# Behind the scenes:
it = iter(cpp.enemies)          # Calls VectorProxy_iter
while True:
    try:
        enemy = next(it)         # Calls VectorIterator_next
        print(enemy.name)
    except StopIteration:
        break
```

#### `PyObject *VectorIterator_next(PyObject *self)`

**Purpose:** Return next element in iteration (called by `next(iterator)`).

**Execution Flow:**

```
Input:  self (VectorIteratorObject)

Step 1: Unpack iterator state
        auto *it = (VectorIteratorObject *)self;
        auto *proxy = it->vector;
        size_t current_size = proxy->bound->size();

Step 2: Check if iteration complete
        if ((size_t)it->index >= current_size) {
            PyErr_SetNone(PyExc_StopIteration);
            return nullptr;  // Signals end to Python for-loop
        }

Step 3: Get current element
        PyObject *result = VectorProxy_getitem(
            (PyObject*)proxy, 
            it->index
        );

Step 4: Advance iterator
        it->index++;

Output: PyObject* (next element) or NULL with StopIteration (end)
```

**State Machine:**
```
Initially:  it->index = 0
Loop 1:     Return element[0], index becomes 1
Loop 2:     Return element[1], index becomes 2
...
Loop N:     Return element[N-1], index becomes N
Loop N+1:   index (N) >= size (N) → StopIteration, end loop
```

---

[Back to Table of Contents](#table-of-contents)


## V. Type Conversion and Binding

### value_interface.hpp: Registry and Binding

#### `template <typename T> static void PyInterface::bind(const std::string &name, T &variable)`

**Purpose:** Register a C++ variable for Python access.

**Compile-Time Dispatch:**

```cpp
template <typename T>
static void bind(const std::string &name, T &variable) {
    if constexpr (is_reflected_struct<T>::value) {
        // Brand 1: User-defined struct → BoundStruct
        g_values[name] = std::make_unique<BoundStruct>(
            name, 
            &variable, 
            get_struct_info<T>()
        );
    } 
    else if constexpr (is_std_vector<T>::value) {
        // Branch 2: std::vector<Element>
        using Element = typename T::value_type;
        
        g_values[name] = std::make_unique<BoundVector>(
            name,
            &variable,
            get_vector_info<Element>()  // Dispatch on element type
        );
    }
    else if constexpr (std::is_same_v<T, int>) {
        // Branch 3: Scalar int
        g_values[name] = std::make_unique<PyBoundInt>(name, variable);
    }
    // ... more scalar branches ...
}
```

**Compile-Time Behavior:**

```
bind("player", player_instance)
├─→ is_reflected_struct<Player>::value = true
└─→ Only "Branch 1" code is compiled
    (Branches 2-4 completely eliminated)

bind("scores", scores_vector)
├─→ is_reflected_struct<decltype(scores_vector)> = false
├─→ is_std_vector<decltype(scores_vector)> = true
└─→ Only "Branch 2" code is compiled
    (Branches 1, 3-4 eliminated)

bind("mode", mode_int)
├─→ is_reflected_struct<int> = false
├─→ is_std_vector<int> = false
├─→ std::is_same_v<int, int> = true
└─→ Only "Branch 3" code is compiled
    (Branches 1-2, 4 eliminated)

Result: Zero runtime overhead!
Each call compiles to specific instantiation.
```

#### `static BoundValue *PyInterface::get_value_raw(const std::string &name)`

**Purpose:** Look up bound variable by name.

**Implementation:**
```cpp
static BoundValue *get_value_raw(const std::string &name) {
    auto it = g_values.find(name);
    return it != g_values.end() ? it->second.get() : nullptr;
}
```

**Performance:** O(1) average (unordered_map hash lookup).

---

### python_bind.hpp: Scalar Type Conversions

#### `PyObject *PyBoundInt::to_python()`

**Purpose:** Convert `int*` to Python int.

**Implementation:**
```cpp
PyObject *PyBoundInt::to_python() {
    return PyLong_FromLong(*m_ptr);
}
```

**Example:**
```cpp
int health = 100;
PyBoundInt bound_health("health", health);
PyObject *py_int = bound_health.to_python();
// py_int represents Python: 100 (int)
```

#### `bool PyBoundInt::from_python(PyObject *obj)`

**Purpose:** Convert Python int to `int*`.

**Implementation:**
```cpp
bool PyBoundInt::from_python(PyObject *obj) {
    if (!PyLong_Check(obj)) return false;
    
    long value = PyLong_AsLong(obj);
    if (PyErr_Occurred()) return false;  // Overflow/error
    
    *m_ptr = (int)value;  // Write to original memory
    return true;
}
```

**Example:**
```python
# Python code:
cpp.mode = 42

# Execution:
PyBoundInt::from_python(py_42);
├─→ PyLong_Check(py_42) = true
├─→ long value = 42
└─→ *m_ptr = 42  (writes to original int)
```

#### `PyObject *PyBoundString::to_python()`

**Purpose:** Convert `std::string*` to Python str.

**Implementation:**
```cpp
PyObject *PyBoundString::to_python() {
    return PyUnicode_FromString(m_ptr->c_str());
}
```

#### `bool PyBoundString::from_python(PyObject *obj)`

**Purpose:** Convert Python str to `std::string*`.

**Critical Safety Pattern:**
```cpp
bool PyBoundString::from_python(PyObject *obj) {
    if (!PyUnicode_Check(obj)) return false;
    
    // Get UTF-8 C string from Python Unicode object
    // THIS POINTER POINTS TO PYTHON'S INTERNAL BUFFER
    const char *str = PyUnicode_AsUTF8(obj);
    if (!str) return false;
    
    // ✓ SAFE: std::string constructor immediately copies
    *m_ptr = str;  // String copy assignment operator
    
    // At next Python operation, we no longer need the pointer
    // Python may move/reallocate the buffer
    return true;
}
```

**Why This Pattern Works:**
1. `PyUnicode_AsUTF8()` returns pointer to internal Python buffer
2. Immediately call `operator=()` which copies data
3. By the time we call any Python C-API function, we own a copy
4. Python is free to reallocate/move its buffer

**Wrong Pattern (Segmentation Fault):**
```cpp
const char *str = PyUnicode_AsUTF8(obj);
m_c_str = str;  // ← Just storing pointer!
// ...later...
PyObject *other = PyUnicode_FromString("X");  // Python reallocates!
// m_c_str now points to freed memory
```

---

[Back to Table of Contents](#table-of-contents)


## VI. Memory Management and Lifetime

### Ownership Model

**Scalars (int, float, bool, std::string):**
- Owned by: User code (main.cpp)
- Lifetime: Variable scope
- PyBoundInt/Float/Bool/String: Hold raw pointer, no responsibility

**Structs:**
- Owned by: User code
- Lifetime: Variable scope or container lifetime
- BoundStruct: Holds void* pointer (never deletes)
- StructProxy: Wraps BoundStruct, does not own

**Vectors:**
- Owned by: User code (main.cpp)
- Lifetime: Variable scope
- BoundVector: Holds void* to vector (never deletes)
- VectorProxy: Wraps BoundVector, does not own

**VectorIterator:**
- Owns: Reference to VectorProxy
- Ensures vector stays valid during iteration
- Uses `Py_INCREF`/`Py_DECREF` for reference counting

### No Heap Allocation in Reflection Layer

**Reflection has NO new/delete:**
```cpp
// reflection_struct.hpp
class BoundStruct {
    void *m_instance;           // Never deleted
    const StructInfo *m_info;   // Pointer to static metadata
};

// reflection_vector.hpp
class BoundVector {
    void *m_vec_ptr;            // Never deleted
    const VectorInfo *m_info;   // Pointer to static metadata
};
```

**Python Layer Allocates Proxies:**
```cpp
// python_proxy.cpp
StructProxy *proxy = PyObject_New(StructProxy, &StructProxyType);
proxy->bound = new BoundStruct(...);  // ← Allocated in Python extension

// When proxy is deleted:
StructProxy_dealloc(PyObject *self) {
    delete ((StructProxy*)self)->bound;
    PyObject_Del(self);
}
```

---

### Critical Memory Safety Patterns

The system incorporates two critical memory safety patterns that prevent double-free and use-after-free vulnerabilities. These patterns are essential for reliable C++/Python integration.

#### Pattern 1: Wrapper Ownership (Prevents Double-Free)

**Problem Solved:** Issue #18 - Double-free when multiple proxies point to same underlying data

**Original Problem:**
```cpp
// BAD: Multiple proxies sharing single BoundValue* (FIXED)
PyInterface::g_values["player"] = bound_struct_ptr;  // Central registry

// When creating proxy:
StructProxy *proxy1 = create_proxy();
proxy1->bound = g_values["player"];  // ❌ Shares pointer

StructProxy *proxy2 = create_proxy();
proxy2->bound = g_values["player"];  // ❌ Shares same pointer

// Cleanup:
delete proxy1->bound;  // Deletes BoundStruct
delete proxy2->bound;  // ❌ DOUBLE-FREE! (same pointer)
```

**Solution: Proxy-Owned Wrapper Copies**
```cpp
// python_proxy.cpp
StructProxy *StructProxy_New(BoundStruct *bound) {
    StructProxy *proxy = PyObject_New(StructProxy, &StructProxyType);
    
    // Create COPY of wrapper, pointing to SAME underlying data
    proxy->bound = new BoundStruct(*bound);  // ✓ Copy wrapper
    
    // Wrapper contains:
    //   void *m_instance (points to actual struct in C++)
    //   StructInfo *m_info (metadata pointer)
    // Both copied, but m_instance still points to original data
    
    return proxy;
}

void StructProxy_dealloc(PyObject *self) {
    StructProxy *proxy = (StructProxy*)self;
    delete proxy->bound;  // ✓ Deletes THIS proxy's wrapper copy
    PyObject_Del(self);   // ✓ No double-free possible
}
```

**Key Insight:**
- `g_values` registry stores wrappers (BoundValue*, BoundStruct*, BoundVector*)
- Each proxy gets its **own copy** of the wrapper
- Wrappers contain pointers to actual data (void *m_instance)
- Multiple wrapper copies → all point to same data → safe cleanup

**Memory Layout:**
```
C++ Memory:
┌──────────────────────┐
│ Player player;       │  ← Actual data (owned by main.cpp)
│   health: 100        │
│   name: "Alice"      │
└──────────────────────┘
         ↑         ↑
         │         │
         │         └─────────────┐
         │                       │
Python Proxies:                  │
┌─────────────────────┐  ┌─────────────────────┐
│ StructProxy #1      │  │ StructProxy #2      │
│  bound:             │  │  bound:             │
│   BoundStruct {     │  │   BoundStruct {     │
│     m_instance: ────┼──┘     m_instance: ────┘
│     m_info: ...     │  │     m_info: ...     │
│   }                 │  │   }                 │
└─────────────────────┘  └─────────────────────┘
    ↑ Owns wrapper         ↑ Owns DIFFERENT wrapper
    
Cleanup:
  delete proxy1->bound;  // Deletes BoundStruct copy #1
  delete proxy2->bound;  // Deletes BoundStruct copy #2 (SAFE)
  // Original Player in C++ unaffected
```

**See:** [WRAPPER_OWNERSHIP_PATTERN.md](WRAPPER_OWNERSHIP_PATTERN.md) for detailed analysis

---

#### Pattern 2: Parent Tracking for Vector Elements (Prevents Use-After-Free)

**Problem Solved:** Issue #26 - Vector element proxy invalidation after reallocation

**Original Problem:**
```cpp
// BAD: Storing raw pointers to vector elements (FIXED)
void *elem_ptr = vector.data() + index * elem_size;  // Raw pointer
StructProxy *proxy = create_proxy(elem_ptr);

// Later:
vector.push_back(new_element);  // Reallocation!
// elem_ptr now points to FREED MEMORY

// Python access:
proxy->bound->m_instance;  // ❌ USE-AFTER-FREE!
```

**Memory Problem:**
```
Before reallocation:
┌────────────────────────────┐
│ Vector capacity: 4         │
│ [0] Player {100, "Alice"}  │ ← elem_ptr points here
│ [1] Player {80, "Bob"}     │
│ [2] Player {50, "Carol"}   │
│ [3] Player {120, "Dave"}   │
└────────────────────────────┘

After push_back (triggers reallocation):
Old memory: FREED ❌
┌────────────────────────────┐
│ GARBAGE (deallocated)      │ ← elem_ptr still points here!
└────────────────────────────┘

New memory:
┌────────────────────────────────┐
│ Vector capacity: 8             │
│ [0] Player {100, "Alice"}      │ ← Moved to new location
│ [1] Player {80, "Bob"}         │
│ [2] Player {50, "Carol"}       │
│ [3] Player {120, "Dave"}       │
│ [4] Player {90, "Eve"}         │ ← Newly added
└────────────────────────────────┘
```

**Solution: Store Parent + Index, Resolve Dynamically**

**Option A (Initial Implementation):**
```cpp
// reflection_vector.hpp
class BoundVector {
public:
    void *element_ptr(std::size_t index) {
        return m_info->element_ptr_fn(m_vec_ptr, index);
        // ✓ Always gets fresh pointer from current vector memory
    }
};

// python_proxy.cpp
StructProxy *VectorProxy_getitem(VectorProxy *self, Py_ssize_t index) {
    void *elem_ptr = self->bound->element_ptr(index);  // ✓ Fresh every time
    BoundStruct *bs = new BoundStruct(..., elem_ptr, ...);
    return StructProxy_New(bs);
}
```

**Option B (Enhanced Implementation - Current):**
```cpp
// reflection_struct.hpp
class BoundStruct {
    void *m_instance;
    const StructInfo *m_info;
    
    // Parent tracking for dynamic resolution
    BoundVector *m_parent_vector = nullptr;  // ✓ Parent container
    std::size_t m_parent_index = 0;          // ✓ Position in parent
    
public:
    // Get actual pointer (resolves dynamically if has parent)
    void *get_instance_ptr() const {
        if (m_parent_vector != nullptr) {
            // ✓ Resolve fresh pointer from parent
            return m_parent_vector->element_ptr(m_parent_index);
        }
        return m_instance;  // Regular struct (no parent)
    }
    
    void *get_field_ptr(const FieldInfo *field) {
        return static_cast<char*>(get_instance_ptr()) + field->offset;
        // ✓ Uses fresh pointer, safe after reallocation
    }
};

// python_proxy.cpp
StructProxy *VectorProxy_getitem(VectorProxy *self, Py_ssize_t index) {
    // Store PARENT + INDEX instead of raw pointer
    BoundStruct *bs = new BoundStruct(
        nullptr,              // m_instance (not used when parent set)
        elem_struct_info,
        self->bound,          // ✓ Parent vector
        index                 // ✓ Index in parent
    );
    return StructProxy_New(bs);
}
```

**Safety Guarantee:**
```python
enemies = cpp.enemies  # VectorProxy
enemy0 = enemies[0]    # StructProxy with parent tracking

# enemy0 stores:
#   m_parent_vector = pointer to VectorProxy's BoundVector
#   m_parent_index = 0

cpp.enemies.append_new()  # Triggers reallocation

# Later access:
health = enemy0.health

# Execution:
#   get_field_ptr("health")
#   → get_instance_ptr()
#   → m_parent_vector->element_ptr(m_parent_index)
#   → Returns FRESH pointer from NEW vector memory
#   → ✓ SAFE, no use-after-free
```

**Performance Impact:** Minimal - one extra indirection for vector elements

**See:** 
- [VECTOR_ELEMENT_PROXY_INVALIDATION.md](VECTOR_ELEMENT_PROXY_INVALIDATION.md) - Problem analysis
- [PARENT_TRACKING_IMPLEMENTATION_GUIDE.md](PARENT_TRACKING_IMPLEMENTATION_GUIDE.md) - Implementation details
- [CIRCULAR_DEPENDENCY_RESOLUTION.md](CIRCULAR_DEPENDENCY_RESOLUTION.md) - Header dependency solution

---

### Memory Safety Summary

**Two-Layer Safety Strategy:**

1. **Wrapper Ownership (Issue #18):**
   - Prevents: Double-free when multiple proxies exist
   - Solution: Each proxy owns its own wrapper copy
   - Cost: Negligible (wrapper is just two pointers)

2. **Parent Tracking (Issue #26):**
   - Prevents: Use-after-free for vector element proxies
   - Solution: Store parent + index, resolve dynamically
   - Cost: One indirection per field access on vector elements

**Combined Result:**
```python
# Safe operations:
p1 = cpp.player        # ✓ Proxy owns wrapper copy
p2 = cpp.player        # ✓ Different wrapper, same data
del p1                 # ✓ No double-free

e = cpp.enemies[0]     # ✓ Parent tracking enabled
cpp.enemies.append_new()  # Vector reallocates
print(e.health)        # ✓ Fresh pointer resolved, safe
```

**Trade-offs:**
- Small memory overhead (parent pointers in BoundStruct)
- Small performance overhead (dynamic resolution for vector elements)
- **Eliminates entire classes of memory errors**

---

### Pattern 3: Thread-Safe Singleton Initialization (Issue #34)

**Function:** `create_cpp_proxy()`

**Location:** python_proxy.cpp

**Purpose:** Create or return the singleton `cpp` module proxy instance in a thread-safe manner.

**Implementation:**

```cpp
#include <mutex>

// Global state protected by mutex
static std::mutex g_cpp_proxy_mutex;
static PyObject *g_cpp_proxy_instance = nullptr;

PyObject *create_cpp_proxy() {
    // ISSUE 34: Thread-safe singleton pattern with lock guard
    std::lock_guard<std::mutex> lock(g_cpp_proxy_mutex);
    
    // Double-checked locking inside mutex
    if (g_cpp_proxy_instance) {
        // ISSUE 39: Return NEW reference to existing instance
        Py_INCREF(g_cpp_proxy_instance);
        return g_cpp_proxy_instance;
    }
    
    // First initialization
    if (PyType_Ready(&CppProxyType) < 0)
        return nullptr;
    
    // ISSUE 39: PyObject_New returns NEW reference (refcount=1)
    g_cpp_proxy_instance = 
        reinterpret_cast<PyObject*>(PyObject_New(CppProxyObject, &CppProxyType));
    
    if (!g_cpp_proxy_instance) {
        PyErr_NoMemory();
        return nullptr;
    }
    
    // Return NEW reference to caller
    return g_cpp_proxy_instance;
}
```

**Execution Flow:**

```
Thread A                           Thread B
─────────────────────────────────  ─────────────────────────────────
Call create_cpp_proxy()            Call create_cpp_proxy()
  ↓                                  ↓
Acquire g_cpp_proxy_mutex          Wait for mutex...
  ↓                                  │
Check: instance == nullptr         │
  ↓                                  │
PyType_Ready(&CppProxyType)        │
  ↓                                  │
Create instance                     │
  ↓                                  │
Release mutex                       │
  ↓                                  ↓
Return instance                    Acquire g_cpp_proxy_mutex
                                     ↓
                                   Check: instance != nullptr
                                     ↓
                                   Py_INCREF(instance)
                                     ↓
                                   Release mutex
                                     ↓
                                   Return instance
```

**Thread Safety Guarantees:**

1. **Mutual Exclusion:** Only one thread executes critical section at a time
2. **Memory Ordering:** std::mutex provides full memory barrier (seq_cst)
3. **No Race Conditions:** Singleton initialized exactly once
4. **No Memory Leaks:** All paths properly manage refcount

**Why std::lock_guard?**

```cpp
// RAII pattern: exception-safe
void example() {
    std::lock_guard<std::mutex> lock(g_cpp_proxy_mutex);
    
    if (some_operation_fails())
        throw std::runtime_error("...");
    // ✓ lock_guard destructor releases mutex automatically
}

// ❌ Manual locking: exception-unsafe
void example_wrong() {
    g_cpp_proxy_mutex.lock();
    
    if (some_operation_fails())
        throw std::runtime_error("...");
        // ❌ Mutex never unlocked! Deadlock!
    
    g_cpp_proxy_mutex.unlock();
}
```

**Performance Characteristics:**

- **First call:** ~100 CPU cycles (initialization + mutex overhead)
- **Subsequent calls:** ~10 CPU cycles (mutex lock/unlock)
- **Memory cost:** 40 bytes (std::mutex on most platforms)

---

### Pattern 4: Python Reference Counting Semantics (Issue #39)

**Affected Functions:** All proxy creation functions

**Core Principle:** Every function that returns `PyObject*` must follow strict reference ownership rules.

#### Reference Ownership Rules

**NEW Reference:** Caller owns the reference and MUST call `Py_DECREF` when done.

```cpp
PyObject *obj = PyLong_FromLong(42);  // Returns NEW reference (refcount=1)
// ... use obj ...
Py_DECREF(obj);  // ✓ Required: release ownership
```

**BORROWED Reference:** Caller does NOT own the reference and must NOT call `Py_DECREF`.

```cpp
PyObject *item = PyTuple_GetItem(tuple, 0);  // Returns BORROWED reference
// ... use item ...
// Py_DECREF(item);  ❌ WRONG! Tuple still owns this reference
```

#### create_cpp_proxy() Reference Counting

```cpp
PyObject *create_cpp_proxy() {
    std::lock_guard<std::mutex> lock(g_cpp_proxy_mutex);
    
    if (g_cpp_proxy_instance) {
        // Fast path: instance already exists
        // ISSUE 39: Must return NEW reference
        Py_INCREF(g_cpp_proxy_instance);  // Increment refcount
        return g_cpp_proxy_instance;
        // Caller now owns a reference (must Py_DECREF later)
    }
    
    // Slow path: first initialization
    g_cpp_proxy_instance = PyObject_New(CppProxyObject, &CppProxyType);
    // PyObject_New returns NEW reference (refcount=1)
    // No Py_INCREF needed
    return g_cpp_proxy_instance;
    // Caller receives object with refcount=1
}
```

**Refcount Timeline:**

```
Scenario 1: First Call
  create_cpp_proxy() called
    → PyObject_New() creates object (refcount=1)
    → Return to caller (refcount=1)
    → Caller uses object
    → Caller does Py_DECREF (refcount=0)
    → Object destroyed

Scenario 2: Subsequent Call
  create_cpp_proxy() called
    → Instance already exists (refcount=1, held by singleton)
    → Py_INCREF (refcount=2)
    → Return to caller (refcount=2)
    → Caller uses object
    → Caller does Py_DECREF (refcount=1)
    → Singleton still holds reference (object survives)
```

#### StructProxy_New() and VectorProxy_New() Reference Counting

```cpp
PyObject *StructProxy_New(BoundStruct *bound) {
    StructProxyObject *self = PyObject_New(StructProxyObject, &StructProxyType);
    if (!self) return nullptr;
    
    self->bound = bound;           // Transfer ownership of BoundStruct
    self->parent_proxy = nullptr;  // No parent by default
    
    // ISSUE 39: PyObject_New returns NEW reference
    return (PyObject*)self;
    // Caller must Py_DECREF when done
}

PyObject *StructProxy_New(BoundStruct *bound, PyObject *parent) {
    StructProxyObject *self = PyObject_New(StructProxyObject, &StructProxyType);
    if (!self) {
        delete bound;  // Clean up on failure
        return nullptr;
    }
    
    self->bound = bound;
    
    // ISSUE 48: Store parent reference
    self->parent_proxy = parent;
    Py_INCREF(parent);  // ✓ Increment parent refcount (now owned)
    
    // ISSUE 39: Return NEW reference
    return (PyObject*)self;
}
```

**Usage Pattern:**

```cpp
// In VectorProxy_getitem:
BoundStruct *element_wrapper = new BoundStruct(...);
PyObject *proxy = StructProxy_New(element_wrapper, (PyObject*)self);
// proxy refcount=1 (NEW reference)
// parent (self) refcount incremented by StructProxy_New

return proxy;  // Transfer NEW reference to Python
// Python runtime will Py_DECREF when proxy is no longer needed
```

#### Destructor Reference Management

```cpp
static void StructProxy_dealloc(PyObject *self) {
    StructProxyObject *proxy = (StructProxyObject*)self;
    
    // Free owned C++ wrapper
    delete proxy->bound;
    
    // ISSUE 48: Release parent reference
    Py_XDECREF(proxy->parent_proxy);
    // Py_XDECREF is safe for NULL pointers
    // Decrements parent refcount (may trigger parent destruction)
    
    // Free Python object
    Py_TYPE(self)->tp_free(self);
}
```

**Reference Counting Summary Table:**

| Function | Return Type | Refcount Behavior | Caller Must |
|----------|-------------|-------------------|-------------|
| `create_cpp_proxy()` | NEW reference | Py_INCREF if existing | Py_DECREF |
| `StructProxy_New()` | NEW reference | refcount=1 from PyObject_New | Py_DECREF |
| `VectorProxy_New()` | NEW reference | refcount=1 from PyObject_New | Py_DECREF |
| `to_python()` | NEW reference | Created from C++ value | Py_DECREF |
| `parent_proxy` field | OWNED reference | Py_INCREF in constructor | Py_DECREF in dealloc |

---

### Pattern 5: Parent-Child Proxy Lifetime Management (Issue #48)

**Problem:** Vector element proxies hold pointers to their parent vector. If the parent VectorProxy is destroyed while element proxies still exist, those pointers become dangling.

**The Scenario:**

```python
def get_first_enemy():
    enemies = cpp.enemies     # VectorProxy created (refcount=1)
    first = enemies[0]        # StructProxy created
    return first              # Return StructProxy
    # ← enemies local variable destroyed (refcount=0)
    # → VectorProxy deallocated
    # → BoundVector freed
    # → first.bound->m_parent_vector now DANGLING POINTER

enemy = get_first_enemy()
print(enemy.health)           # ❌ CRASH: parent_vector points to freed memory
```

**The Solution: Parent Reference in Proxy Object**

```cpp
typedef struct {
    PyObject_HEAD
    BoundStruct *bound;        // Owned wrapper
    PyObject *parent_proxy;    // ISSUE 48: Reference to parent VectorProxy
} StructProxyObject;
```

**Implementation in VectorProxy_getitem:**

```cpp
static PyObject *VectorProxy_getitem(PyObject *self, Py_ssize_t index) {
    VectorProxyObject *vec_proxy = (VectorProxyObject*)self;
    BoundVector *bound = vec_proxy->bound;
    
    // ... bounds checking ...
    
    // Create element wrapper with parent tracking
    BoundStruct *element_wrapper = new BoundStruct(
        bound->get_name() + "[" + std::to_string(index) + "]",
        bound,          // parent vector
        index,          // parent index
        bound->get_element_struct_info()
    );
    
    // Create proxy with parent reference
    StructProxyObject *proxy = PyObject_New(StructProxyObject, &StructProxyType);
    if (!proxy) {
        delete element_wrapper;
        return nullptr;
    }
    
    proxy->bound = element_wrapper;
    
    // ISSUE 48: Keep parent alive
    proxy->parent_proxy = self;    // Store reference to VectorProxy
    Py_INCREF(self);               // ✓ Increment VectorProxy refcount
    
    return (PyObject*)proxy;
}
```

**Lifetime Management:**

```
State 1: Initial Access
  Python: enemies = cpp.enemies
    VectorProxy created (refcount=1)
  
  Python: first = enemies[0]
    StructProxy created
    parent_proxy = VectorProxy (refcount=2: enemies var + parent_proxy)

State 2: Local Variable Released
  Python: return first
    enemies local variable destroyed
    VectorProxy refcount: 2 → 1 (still alive due to parent_proxy)

State 3: Element Still Valid
  Python: enemy = get_first_enemy()
    StructProxy returned (refcount=1)
    VectorProxy alive (refcount=1 from parent_proxy)
  
  Python: print(enemy.health)
    ✓ Dynamic resolution works
    ✓ parent_vector pointer still valid

State 4: Element Destroyed
  Python: del enemy (or goes out of scope)
    StructProxy_dealloc called
    Py_XDECREF(parent_proxy)
    VectorProxy refcount: 1 → 0
    VectorProxy_dealloc called
    BoundVector freed
```

**Memory Diagram:**

```
Python Heap:
┌──────────────────────────────────┐
│ VectorProxy (refcount=1)         │ ← Kept alive by parent_proxy
│   bound: BoundVector*            │
└──────────────────────────────────┘
         ↑
         │ parent_proxy reference
         │
┌──────────────────────────────────┐
│ StructProxy (refcount=1)         │
│   bound: BoundStruct*            │
│     m_parent_vector ──────┐      │
│     m_parent_index: 0     │      │
│   parent_proxy ───────────┼──────┘
└───────────────────────────┼───────┘
                            │
                            ↓
C++ Heap:                   
┌──────────────────────────────────┐
│ BoundVector                      │ ← Still valid!
│   m_vec_ptr: &enemies            │
│   m_info: &enemy_vector_info     │
└──────────────────────────────────┘
```

**Without parent_proxy (UNSAFE):**

```python
def get_first_enemy():
    enemies = cpp.enemies     # VectorProxy refcount=1
    first = enemies[0]        # StructProxy created, NO parent_proxy
    return first
    # ← VectorProxy refcount: 1 → 0, DESTROYED
    # → BoundVector FREED

enemy = get_first_enemy()
# enemy.bound->m_parent_vector points to FREED MEMORY
print(enemy.health)  # ❌ Use-after-free, CRASH or garbage data
```

**With parent_proxy (SAFE):**

```python
def get_first_enemy():
    enemies = cpp.enemies     # VectorProxy refcount=1
    first = enemies[0]        # parent_proxy increments refcount to 2
    return first
    # ← VectorProxy refcount: 2 → 1, STILL ALIVE

enemy = get_first_enemy()
# VectorProxy alive, BoundVector valid
print(enemy.health)  # ✓ Safe dynamic resolution
```

**Cost Analysis:**

| Aspect | Cost |
|--------|------|
| Memory | 8 bytes per element proxy (parent_proxy pointer) |
| CPU | ~2 cycles (Py_INCREF/DECREF overhead) |
| Complexity | Minimal (RAII-style reference management) |

For complete details, see the dedicated memory safety documentation:
- [WRAPPER_OWNERSHIP_PATTERN.md](WRAPPER_OWNERSHIP_PATTERN.md)
- [VECTOR_ELEMENT_PROXY_INVALIDATION.md](VECTOR_ELEMENT_PROXY_INVALIDATION.md)
- [PARENT_TRACKING_IMPLEMENTATION_GUIDE.md](PARENT_TRACKING_IMPLEMENTATION_GUIDE.md)
- [OWNERSHIP_MODELS_GUIDE.md](OWNERSHIP_MODELS_GUIDE.md) - Complete ownership and thread safety reference

---

[Back to Table of Contents](#table-of-contents)


## VII. Summary: Data Flow Example

### Complete Example Walkthrough: `cpp.enemies[0].health = 50`

```
Python Source Code:
    cpp.enemies[0].health = 50

Step 1: Module Access (cpp_module_getattr)
    Python: cpp.enemies
    → cpp_module_getattr(cpp_module, "enemies")
    → BoundVector *bound = get_value_raw("enemies")
    → VectorProxy *proxy = VectorProxy_New(bound)
    → Return proxy to Python

Step 2: Indexing (VectorProxy_setitem)
    Python: proxy[0]
    → VectorProxy_getitem(proxy, 0)
    → void *elem_ptr = bound->element_ptr(0)
    → element_ptr returns address of enemies[0]
    → element_type = Struct
    → StructProxy *inner = StructProxy_New(BoundStruct(..., elem_ptr, ...))
    → Return inner proxy to Python

Step 3: Field Access (StructProxy_setattro)
    Python: inner.health = 50
    → StructProxy_setattro(inner, "health", py_50)
    → const FieldInfo *field = get_field("health")
    → void *field_ptr = get_field_ptr(field)
    → field_ptr = elem_ptr + field->offset
    → PyLong_AsLong(py_50) = 50
    → *(int*)field_ptr = 50
    → Return 0 (success)

Result:
    enemies[0].health in C++ is now 50
    Python and C++ share the same memory
```

### Memory Map at Execution

```
Main Memory (conceptual):
┌─────────────────────────────────────────┐
│ Arena: std::vector<Enemy>               │
├─────────────────────────────────────────┤
│ [0] Enemy { health: 50, name: "Goblin"} │  ← Modified by Python!
│ [1] Enemy { health: 120, name: "Orc"  } │
│ [2] Enemy { health: 80, name: "Troll" } │
└─────────────────────────────────────────┘
     ↑
bound->element_ptr(0) points here

Python Proxy Stack:
  StructProxy {
      bound: BoundStruct {
          m_instance: &enemies[0],
          m_info: &enemy_struct_info
      }
  }

When we read .health:
  health_ptr = m_instance + offset_of_health = &(enemies[0].health)
  value = *(int*)health_ptr = 50
```

This completes the comprehensive function reference.

[Back to Table of Contents](#table-of-contents)

