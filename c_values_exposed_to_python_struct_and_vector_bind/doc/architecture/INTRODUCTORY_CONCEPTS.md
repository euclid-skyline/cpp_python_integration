# Introductory Concepts: C++ Reflection and Python Modules

This document explains foundational concepts needed to understand C++/Python integration. It is **not specific to this project** — these concepts apply broadly to similar integration problems.

## Table of Contents

1. [Type Traits](#type-traits)
2. [Constexpr and if constexpr](#constexpr-and-if-constexpr)
3. [Creating Python Modules in C++](#creating-python-modules-in-c)
4. [Core Structures for Python Modules](#core-structures-for-python-modules)
5. [Module Registration and Loading](#module-registration-and-loading)
6. [Reflection Pattern](#reflection-pattern)
7. [Type Erasure Pattern](#type-erasure-pattern)
8. [Direct (Non-Proxy) Access for Scalars](#direct-non-proxy-access-for-scalars)
9. [Python Proxy Pattern](#python-proxy-pattern)
10. [Further Reading](#further-reading)

---

## Overview

This guide introduces core concepts you need before reading the architecture docs for this project. It starts with templates and type traits, then moves to compile-time features, Python C extension basics, and finally the patterns used to bridge C++ data to Python.

Use it as a gentle on-ramp:
- Read top to bottom if you are new to C++ metaprogramming or Python C API.
- Jump to specific sections if you need a quick refresher.
- Follow the "Further Reading" links at the end of each section for deep dives.

---

## Type Traits

### Foundation: C++ Templates

Before understanding type traits, you need to understand C++ **templates** — they're the foundation that type traits build upon.

#### What Are Templates?

**Templates** are blueprints for code that work with different types. Instead of writing the same logic for `int`, `double`, and `std::string`, you write it once as a template:

```cpp
// Template function: works for ANY type
template <typename T>
T add(T a, T b) {
    return a + b;
}

// Compiler generates three versions:
add(3, 4);           // T = int,         returns 7
add(3.0, 4.0);       // T = double,      returns 7.0
add("x"s, "y"s);     // T = std::string, returns "xy"
```

**Key insight:** The compiler creates specialized versions of your template for each type you use. This is called **instantiation**.

#### Template Specialization

You can provide custom implementations for specific types:

```cpp
// Generic template: works for any type
template <typename T>
void print(T value) {
    std::cout << "Generic: " << value << std::endl;
}

// Specialization: only for std::string
template <>
void print<std::string>(std::string value) {
    std::cout << "String: '" << value << "'" << std::endl;
}

print(42);          // Generic version: "Generic: 42"
print("hello"s);    // String version: "String: 'hello'"
```

**Why specialize?** Different types need different handling. This is where type traits shine.

---

### From Templates to Type Traits

**Type traits answer questions about types at compile-time.**

Think of type traits as a **query system**: "Is this type a vector?" "Is this type an integer?"

The magic is that:
1. **Templates let code adapt to types** (specialization)
2. **Type traits detect type properties** (queries)
3. **Together they enable compile-time dispatch** (type-safe, zero-overhead)

#### The Bridge: How Type Traits Use Templates

```cpp
// Step 1: Generic template (false for any type)
template <typename T>
struct is_vector : std::false_type { };
//                 ^
//                 Default: not a vector

// Step 2: Specialization (true only for std::vector)
template <typename T, typename Alloc>
struct is_vector<std::vector<T, Alloc>> : std::true_type { };
//                ^^^^^^^^^^^^^^^^^^^
//                Only matches if T is exactly std::vector

// Step 3: Use it to specialize behavior
template <typename T>
void print_size(T& obj) {
    if constexpr (is_vector<T>::value) {
        std::cout << "Vector size: " << obj.size() << std::endl;
    } else {
        std::cout << "Not a vector" << std::endl;
    }
}
```

**What's happening?**
- Generic template catches everything: `is_vector<int>::value` = **false**
- Specialization catches vectors only: `is_vector<std::vector<int>>::value` = **true**
- `if constexpr` uses this information to compile different code

---

### What Are Type Traits?

**Type traits** are compile-time predicates that answer yes/no questions about types. They let you query type properties without runtime overhead.

Example questions type traits answer:
- "Is `T` an integer?"
- "Is `T` a vector?"
- "Is `T` a pointer?"
- "Is `T` a user-defined struct?"

#### Simple Type Trait Example

```cpp
// Generic version: assume false
template <typename T>
struct is_vector : std::false_type { };

// Specialization: true only for std::vector
template <typename T, typename Alloc>
struct is_vector<std::vector<T, Alloc>> : std::true_type { };

// Access the result:
std::cout << is_vector<int>::value;              // false
std::cout << is_vector<std::vector<int>>::value; // true
```

**How it works:**
- `std::false_type` and `std::true_type` are structs with a `::value` member
- `std::false_type::value` is `false`, `std::true_type::value` is `true`

#### The Basic Structure

Every type trait follows this pattern:

```cpp
// 1. Generic version (default case)
template <typename T>
struct MyTrait : std::false_type { };

// 2. Specializations (specific cases)
template <>
struct MyTrait<int> : std::true_type { };

template <>
struct MyTrait<double> : std::true_type { };

// 3. Usage
if (MyTrait<T>::value) {
    // T matches one of our specializations
}
```

---

### Key Type Traits Concepts

| Concept | Purpose | Example |
|---------|---------|---------|
| **Generic Template** | Catch-all case (default behavior) | `template<typename T> struct is_foo : std::false_type` |
| **Template Specialization** | Override for specific types | `template<> struct is_foo<int> : std::true_type` |
| **Partial Specialization** | Override for type patterns | `template<typename T> struct is_foo<std::vector<T>>` |
| **::value** | Access the boolean result | `is_foo<int>::value` evaluates to `true` or `false` |
| **Inheritance** | Inherit from true/false type | Inheriting sets `::value` automatically |

---

### Standard Type Traits Library

C++ provides built-in type traits in `<type_traits>`:

```cpp
#include <type_traits>

std::is_integral_v<int>                 // true
std::is_floating_point_v<double>        // true
std::is_same_v<int, int>                // true
std::is_same_v<int, double>             // false
std::is_pointer_v<int*>                 // true
std::is_const_v<const int>              // true
std::is_class_v<MyStruct>               // true (if MyStruct is a class)
```

**Note:** The `_v` suffix is C++17. In older C++:
```cpp
std::is_integral<int>::value            // older style
std::is_integral_v<int>                 // C++17+ (same result)
```

---

### Creating User-Defined Type Traits

You can create custom type traits for your domain:

```cpp
// Step 1: Generic template (default: not reflected)
template <typename T>
struct is_reflected_struct : std::false_type { };

// Step 2: Specialization for each reflected type
template <>
struct is_reflected_struct<Player> : std::true_type { };

template <>
struct is_reflected_struct<Enemy> : std::true_type { };

// Step 3: Use in template functions
template <typename T>
void register_type(const std::string& name) {
    if constexpr (is_reflected_struct<T>::value) {
        // Only compiles for Player and Enemy
        std::cout << "Registering reflected type: " << name << std::endl;
    } else {
        // Only compiles for other types
        std::cout << "Cannot register non-reflected type" << std::endl;
    }
}

// Usage:
register_type<Player>("Player");    // Calls first branch
register_type<int>("int");          // Calls second branch
```

---

### How Templates and Type Traits Work Together

```
┌─ Your Code ────────────────────────────────────┐
│  template <typename T> process(T& value);      │
└──────────────────┬──────────────────────────────┘
                   │
                   ├─ Is T a vector?
                   │  (Type Trait Query)
                   │
                   │      ┌─ Specialization 1: T is vector
                   │      │  → Compile vector-specific code
                   └─ Yes ┤
                          │  ┌─ Specialization 2: T is int
                          └─ No
                             → Compile generic code
```

---

### Further Reading

**In This Project:**
- See `ARCHITECTURE_DEEP_DIVE.md` Section II for how type traits enable the binding bridge
- See `DESIGN_PATTERNS_AND_EXTENSIBILITY.md` Section I for `is_std_vector` and `is_reflected_struct` usage
- See `value_interface.hpp` for actual type trait implementations in the codebase

**External References:**
- cppreference.com — Type traits: https://en.cppreference.com/w/cpp/header/type_traits
- cppreference.com — Template specialization: https://en.cppreference.com/w/cpp/language/template_specialization
- cppreference.com — Variadic templates: https://en.cppreference.com/w/cpp/language/parameter_pack
- C++ Standard Library documentation for `std::true_type` and `std::false_type`

## Constexpr and if constexpr

### What Is Constexpr?

**Constexpr** means "constant expression" — a value computed at **compile-time** instead of runtime.

### Constexpr Functions

Regular functions execute at runtime:
```cpp
int multiply(int a, int b) {
    return a * b;  // Runtime computation
}

int main() {
    int result = multiply(3, 4);  // Calls function at runtime
    int array[20];                 // 20 is a compile-time constant
    // int arr[multiply(3,4)];     // ERROR: multiply result not compile-time
}
```

Constexpr functions can execute at compile-time:
```cpp
constexpr int multiply(int a, int b) {
    return a * b;  // Compile-time computation (if inputs are compile-time)
}

int main() {
    constexpr int result = multiply(3, 4);  // Computed at compile-time
    int array[result];                      // OK: result is compile-time constant
    
    int x = 5;
    int y = multiply(x, 4);  // Also works at runtime if needed
}
```

### Key Constexpr Benefit: Zero Overhead

```cpp
constexpr int size_of_int = sizeof(int);  // Computed at compile-time
// No runtime code needed — it's already known

// Without constexpr (hypothetical):
int size_of_int = sizeof(int);  // Still compile-time, but stored as variable
```

### if constexpr (C++17)

**if constexpr** lets you branch code at compile-time, with unneeded branches removed from the binary.

#### Example: Type-Specific Handling

```cpp
template <typename T>
void print_value(T value) {
    if constexpr (std::is_integral_v<T>) {
        // Only this exists in binary if T is integral
        std::cout << "Integer: " << value << std::endl;
    }
    else if constexpr (std::is_floating_point_v<T>) {
        // Only this exists in binary if T is floating-point
        std::cout << "Float: " << std::fixed << value << std::endl;
    }
    else {
        // All other branches removed from binary
        std::cout << "Unknown type" << std::endl;
    }
}

int main() {
    print_value(42);        // Compiles to only "Integer" branch
    print_value(3.14);      // Compiles to only "Float" branch
    print_value("hello");   // Compiles to only "Unknown" branch
}
```

#### Why This Matters

1. **Zero Runtime Cost**: Unneeded branches don't exist in the compiled binary
2. **Type Safety**: Different branches can have incompatible syntax (e.g., `.size()` only for vectors)
3. **Clean Dispatch**: Single function template with type-specific behavior

```cpp
// This works because vector doesn't have .count() method
template <typename T>
void report_collection(T& coll) {
    if constexpr (std::is_same_v<T, std::vector<int>>) {
        std::cout << "Vector size: " << coll.size() << std::endl;
    }
    else if constexpr (std::is_same_v<T, std::map<std::string, int>>) {
        std::cout << "Map entries: " << coll.size() << std::endl;
        // Even though vector and map have different APIs
    }
}
```

### Constexpr Performance Impact

```cpp
// Without constexpr:
int get_type_size(int type_id) {
    if (type_id == 1) return 4;  // int
    if (type_id == 2) return 8;  // long
    // Runtime if-statements every function call
}

// With constexpr + if constexpr:
template <typename T>
constexpr int get_type_size() {
    if constexpr (std::is_same_v<T, int>) {
        return 4;
    }
    else if constexpr (std::is_same_v<T, long>) {
        return 8;
    }
    // Zero runtime cost — answer known at compile-time
}
```

### Further Reading

**In This Project:**
- See `ARCHITECTURE_DEEP_DIVE.md` Section II for how `if constexpr` enables type-safe binding dispatch
- See `DESIGN_PATTERNS_AND_EXTENSIBILITY.md` for constexpr usage in type detection
- See `value_interface.hpp` for `if constexpr` branching in `PyInterface::bind<T>()`

**External References:**
- cppreference.com — constexpr: https://en.cppreference.com/w/cpp/language/constexpr
- cppreference.com — if constexpr: https://en.cppreference.com/w/cpp/language/if
- C++17 Standard proposal P0292: https://open-std.cpp.org/jtc1/sc22/wg21/docs/papers/2016/p0292r2.html

---

## Creating Python Modules in C++

### Overview: Python C Extension Architecture

A Python C extension (module) is a shared library (`.so` on Linux, `.pyd` on Windows, `.dylib` on macOS) that provides Python-accessible code written in C/C++.

### Python C Extension Structure

Every Python C extension has this basic structure:

1. **Type Definitions** — C structures that represent Python objects
2. **Method Definitions** — Functions callable from Python
3. **Module Definition** — Metadata about the module
4. **Module Initialization** — Called when module is imported

### Simple Python C Extension Example

```cpp
#include <Python.h>

// 1. TYPE DEFINITION
// C structure representing a Python object
typedef struct {
    PyObject_HEAD            // Python header (required)
    int value;               // Your custom data
} MyNumber;

// 2. METHOD DEFINITIONS
// Function called from Python: obj.double()
static PyObject* MyNumber_double(MyNumber* self, PyObject* args) {
    return PyLong_FromLong(self->value * 2);
}

// 3. METHOD TABLE
static PyMethodDef MyNumber_methods[] = {
    {"double", (PyCFunction)MyNumber_double, METH_NOARGS,
     "Return double the value"},
    {NULL}  // Sentinel
};

// 4. TYPE DEFINITION (metaclass info)
static PyTypeObject MyNumberType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "example.MyNumber",
    .tp_methods = MyNumber_methods,
    .tp_basicsize = sizeof(MyNumber),
    // ... other fields
};

// 5. MODULE DEFINITION
static PyModuleDef examplemodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "example",
    .m_doc = "Example module",
    .m_size = -1,
};

// 6. MODULE INITIALIZATION
PyMODINIT_FUNC PyInit_example(void) {
    PyObject* m = PyModule_Create(&examplemodule);
    
    if (PyType_Ready(&MyNumberType) < 0)
        return NULL;
    
    Py_INCREF(&MyNumberType);
    PyModule_AddObject(m, "MyNumber", (PyObject*)&MyNumberType);
    
    return m;
}
```

### Usage from Python

```python
import example

num = example.MyNumber()
result = num.double()
```

### Further Reading

**In This Project:**
- See `SOURCE_CODE_DOCUMENTATION.md` for `cpp_module.cpp` and module entry points
- See `FUNCTION_REFERENCE.md` for module and type initialization details

**External References:**
- Python C API — Extending and embedding: https://docs.python.org/3/extending/index.html
- Python C API — Defining extension modules: https://docs.python.org/3/extending/extending.html
- Python C API — Module objects: https://docs.python.org/3/c-api/module.html

---

## Core Structures for Python Modules

### 1. PyObject and Object Hierarchy

Every Python object in C API starts with `PyObject`:

```cpp
// From Python.h
// PyObject is the basic building block
struct PyObject {
    Py_ssize_t ob_refcnt;   // Reference count
    PyTypeObject *ob_type;  // Type information
};

// Your custom object extends PyObject:
typedef struct {
    PyObject_HEAD            // Includes the PyObject struct
    
    // Your custom fields:
    int health;
    float stamina;
    std::string name;
} Character;
```

### 2. PyMethodDef - Method Definitions

Maps Python method names to C functions:

```cpp
static PyMethodDef Character_methods[] = {
    // {python_name, c_function, flags, docstring}
    {"take_damage", (PyCFunction)Character_take_damage, METH_O,
     "Reduce health by damage amount"},
    
    {"heal", (PyCFunction)Character_heal, METH_O,
     "Restore health"},
    
    {"get_status", (PyCFunction)Character_get_status, METH_NOARGS,
     "Return current status as string"},
    
    {NULL}  // Sentinel value (required!)
};

// METH_NOARGS: function(self) — no arguments
// METH_O:      function(self, arg) — one argument
// METH_VARARGS: function(self, args) — multiple arguments
// METH_KEYWORDS: function(self, args, kwargs) — with keyword args
```

### 3. PyTypeObject - Type Metadata

Defines type behavior:

```cpp
static PyTypeObject CharacterType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    
    // REQUIRED fields:
    .tp_name = "game.Character",              // Python name
    .tp_basicsize = sizeof(Character),        // Size in bytes
    .tp_doc = "A game character",             // Docstring
    
    // IMPORTANT fields:
    .tp_methods = Character_methods,          // Method table
    .tp_new = PyType_GenericNew,              // Constructor
    
    // OPTIONAL fields (for advanced features):
    .tp_dealloc = Character_dealloc,          // Destructor
    .tp_getattro = Character_getattro,        // Get attribute: obj.attr
    .tp_setattro = Character_setattro,        // Set attribute: obj.attr = value
    .tp_iter = Character_iter,                // for loop support
    .tp_iternext = Character_iternext,        // next() support
    
    // Flags:
    .tp_flags = Py_TPFLAGS_DEFAULT,
};
```

### 4. PyModuleDef - Module Definition

Metadata about the entire module:

```cpp
static PyModuleDef gamemodule = {
    PyModuleDef_HEAD_INIT,
    
    .m_name = "game",                    // Module name (import game)
    .m_doc = "Game engine module",       // Module docstring
    .m_size = -1,                        // No module-level state (-1 means none)
    
    // Optional methods at module level:
    // .m_methods = gamemodule_methods,
};
```

### 5. PyTypeObject Slots vs Direct Assignment

Modern Python C API uses **slots** approach:

```cpp
// Older approach (still works in Python 3.x):
PyTypeObject MyType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "My.Type",
};

// Recommended approach (Python 3.9+, cleaner):
static PyType_Slot MyType_slots[] = {
    {Py_tp_dealloc, (destructor)MyType_dealloc},
    {Py_tp_methods, MyType_methods},
    {Py_nm_methods, PyMethodDef_methods},
    {0, NULL}  // Sentinel
};

static PyType_Spec MyType_spec = {
    "My.Type",
    sizeof(MyObject),
    0,
    Py_TPFLAGS_DEFAULT,
    MyType_slots,
};

// Create type from spec:
PyTypeObject *MyType = (PyTypeObject *)PyType_FromSpec(&MyType_spec);
```

### Further Reading

**In This Project:**
- See `SOURCE_CODE_DOCUMENTATION.md` for files like `cpp_module.cpp` showing a complete module
- See `FUNCTION_REFERENCE.md` for PyTypeObject implementations in this project
- See `python_proxy.cpp` for real CppProxyObject, StructProxyObject, VectorProxyObject definitions

**External References:**
- Python C API — Type objects: https://docs.python.org/3/c-api/type.html
- Python C API — Module objects: https://docs.python.org/3/c-api/module.html
- Python C API — Object definitions: https://docs.python.org/3/c-api/type_and_members.html
- PEP 384 — Stable ABI: https://www.python.org/dev/peps/pep-0384/

---

## Module Registration and Loading

### Step 1: Define Your Module

```cpp
// mymodule.cpp
#include <Python.h>

// Define types and methods
static PyModuleDef mymodule_def = {
    PyModuleDef_HEAD_INIT,
    .m_name = "mymodule",
    .m_doc = "My C++ module",
    .m_size = -1,
};
```

### Step 2: Create Initialization Function

The initialization function **must** follow naming convention: `PyInit_{module_name}`

```cpp
PyMODINIT_FUNC PyInit_mymodule(void) {
    // Create module object
    PyObject *module = PyModule_Create(&mymodule_def);
    if (!module) return NULL;
    
    // Prepare type
    if (PyType_Ready(&MyTypeObject) < 0) {
        Py_DECREF(module);
        return NULL;
    }
    
    // Add type to module
    Py_INCREF(&MyTypeObject);
    if (PyModule_AddObject(module, "MyType", 
                          (PyObject *)&MyTypeObject) < 0) {
        Py_DECREF(&MyTypeObject);
        Py_DECREF(module);
        return NULL;
    }
    
    return module;
}
```

### Step 3: Build Configuration (setup.py)

```python
from setuptools import setup, Extension

module = Extension(
    'mymodule',                          # Module name
    sources=['mymodule.cpp'],            # Source files
    include_dirs=[],                     # Include directories
    libraries=[],                        # Linked libraries
)

setup(
    name='mymodule',
    ext_modules=[module],
)
```

### Step 4: Build and Install

```bash
python setup.py build_ext --inplace
python -c "import mymodule; print(mymodule.MyType)"
```

### Step 5: Import and Use

```python
import mymodule

obj = mymodule.MyType()
obj.some_method()
```

### Module Loading Details

When you `import mymodule` in Python:

1. **Python searches** for `mymodule.so` (or `.pyd` on Windows)
2. **Operating system loads** the shared library
3. **Python calls** `PyInit_mymodule()` function
4. **Initialization function** creates module and types
5. **Module object** returned to Python and cached

```python
# First import
import mymodule  # Calls PyInit_mymodule(), module cached

# Second import (same Python process)
import mymodule  # Returns cached module, no re-initialization
```

### Further Reading

**In This Project:**
- See `ARCHITECTURE_DEEP_DIVE.md` Section III and IV for the full Python integration layer
- See `SOURCE_CODE_DOCUMENTATION.md` File List section for module structure
- See `cpp_module.cpp` implementation file for a real module initialization

**External References:**
- Python C API — Module initialization: https://docs.python.org/3/c-api/module.html#c.PyModuleDef
- Python C API — Type initialization: https://docs.python.org/3/c-api/type.html#c.PyType_Ready
- setuptools documentation: https://setuptools.pypa.io/en/latest/setup.html
- Python import system: https://docs.python.org/3/reference/import_system.html

---

## Reflection Pattern

### What Is Reflection?

**Reflection** is the ability for a program to examine and manipulate types and objects at runtime.

### Levels of Reflection

| Level | Capability | Language |
|-------|-----------|----------|
| **Zero** | No runtime type info | C (minimal), Basic |
| **Minimal** | Query type size, alignment | C (sizeof, alignof) |
| **Runtime** | List fields, call methods dynamically | Python, Java, C# (with effort) |
| **Full** | Create new types at runtime | Lisp, Ruby, Python |

### C++ Reflection Challenge

C++ has **no built-in reflection**. The type system information is erased at compile-time:

```cpp
struct Player {
    int health;
    std::string name;
};

// C++ cannot do this at runtime:
// auto fields = Player::get_fields();  // NOT AVAILABLE
```

### Manual Reflection Pattern

You manually provide the type information:

```cpp
struct FieldInfo {
    const char* name;
    std::size_t offset;      // Byte offset in struct
    ValueType type;          // int, string, float, etc.
};

struct StructInfo {
    const char* name;
    std::size_t size;
    const FieldInfo* fields;
    std::size_t field_count;
};

// Register your struct:
const FieldInfo player_fields[] = {
    {"health", offsetof(Player, health), ValueType::Int},
    {"name", offsetof(Player, name), ValueType::String},
};

StructInfo player_info = {
    "Player",
    sizeof(Player),
    player_fields,
    2,
};
```

### Why Manual Reflection?

1. **Zero Runtime Overhead** — Information known at compile-time
2. **C++ Compatibility** — Works with standard C++ types
3. **Type Safety** — Can validate field access
4. **Python Integration** — Easy to expose to Python

### Further Reading

**In This Project:**
- See `ARCHITECTURE_DEEP_DIVE.md` Section I for the pure C++ Reflection Layer
- See `reflection_struct.hpp` for StructInfo and FieldInfo implementations
- See `reflection_vector.hpp` for VectorInfo pattern (reflection for containers)
- See `reflection_value.hpp` for ValueType enum and scalar type reflection

**External References:**
- C++ std::offset: https://en.cppreference.com/w/cpp/types/offsetof
- C++ Standard proposals on reflection: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2996r0.html
- Type punning and memory safety: https://en.cppreference.com/w/cpp/language/reinterpret_cast

---

## Type Erasure Pattern

### Problem: Storing Different Types Together

You have many types and need to store them generically:

```cpp
int health = 100;
std::string name = "Hero";
float stamina = 85.5f;

// How do you store these in a single container?
// std::vector<???> values;  // Can't work — different types!
```

### Solution: Type Erasure with void*

Use void pointers + metadata to store different types:

```cpp
struct ValueInfo {
    ValueType type_id;       // int, string, float, etc.
    void* ptr;               // Points to the actual value
};

std::map<std::string, ValueInfo> values;

values["health"] = {ValueType::Int, &health};
values["name"] = {ValueType::String, &name};
values["stamina"] = {ValueType::Float, &stamina};
```

### Type Erasure with Function Pointers

Add type-specific operations via function pointers:

```cpp
struct ValueInfo {
    ValueType type_id;
    void* value_ptr;
    
    // Type-specific operations:
    PyObject* (*to_python)(void*);      // Convert to Python
    void (*from_python)(void*, PyObject*);  // Convert from Python
    void (*destroy)(void*);             // Cleanup
};

// For int:
PyObject* int_to_python(void* ptr) {
    return PyLong_FromLong(*(int*)ptr);
}

ValueInfo int_info = {
    ValueType::Int,
    nullptr,  // Set during registration
    int_to_python,
    int_from_python,
    int_destroy,
};

// For string:
PyObject* string_to_python(void* ptr) {
    auto s = (std::string*)ptr;
    return PyUnicode_FromStringAndSize(s->data(), s->size());
}

ValueInfo string_info = {
    ValueType::String,
    nullptr,
    string_to_python,
    string_from_python,
    string_destroy,
};
```

### Type Erasure Memory Layout

```
┌─ ValueInfo ──────────────┐
│ type_id = Int            │
│ value_ptr ──────────┐    │
│ to_python ──────┐   │    │
│ from_python ─┐  │   │    │
│ destroy ──┐  │  │   │    │
└───────────┼──┼──┼───┼────┘
            │  │  │   │
            │  │  │   └────→ [actual int value: 100]
            │  │  │
            │  │  └────→ int_from_python function
            │  │
            │  └────→ int_to_python function
            │
            └────→ Converts int → PyObject

Values stored as void* + metadata = can store any type
```

### Advantages and Trade-offs

| Aspect | Benefit | Cost |
|--------|---------|------|
| **Single container** | Store mixed types | Need metadata |
| **Extensibility** | Add new types easily | Function pointers overhead |
| **No templates** | Smaller binary | Less type safety |
| **Python integration** | Easy conversion | One extra indirection |

### Further Reading

**In This Project:**
- See `DESIGN_PATTERNS_AND_EXTENSIBILITY.md` Pattern 1 for type erasure implementation details
- See `ARCHITECTURE_DEEP_DIVE.md` Section III for void* + metadata usage
- See `value_interface.hpp` for ValueInfo and similar structures

**External References:**
- Type erasure pattern: https://en.wikibooks.org/wiki/More_C%2B%2B_Idioms/Type_Erasure
- "Effective C++" by Scott Meyers (Item 26-29 for PIMPL and similar patterns)
- "C++ Templates" by Vandevoorde & Josuttis (Advanced type manipulation chapters)
- Dynamic typing in C++: https://en.cppreference.com/w/cpp/memory/enable_shared_from_this

---

## Direct (Non-Proxy) Access for Scalars

### What "Direct" Means

For scalar values (int, float, bool, string), you can convert directly between C++ values and Python objects without creating a Python proxy class. Python receives a normal `int`, `float`, `bool`, or `str`, not a wrapper object.

### Why Scalars Can Be Direct

Scalars are **value types**. They are self-contained and do not need:
- Attribute access (`obj.field`)
- Lifetime management of nested objects
- Iteration or container semantics

Because of that, a direct conversion is enough.

### Simple Direct Conversion Pattern

```cpp
// C++ value -> Python object
PyObject* to_python_int(int value) {
    return PyLong_FromLong(value);
}

PyObject* to_python_float(double value) {
    return PyFloat_FromDouble(value);
}

PyObject* to_python_bool(bool value) {
    return PyBool_FromLong(value ? 1 : 0);
}

PyObject* to_python_string(const std::string& value) {
    return PyUnicode_FromStringAndSize(value.data(), value.size());
}

// Python object -> C++ value
bool from_python_int(PyObject* obj, int& out) {
    if (!PyLong_Check(obj)) return false;
    out = static_cast<int>(PyLong_AsLong(obj));
    return true;
}
```

### Example: Direct Scalar Access in Python

```python
# Python sees native values
health = 100
speed = 4.5
alive = True
name = "Hero"
```

### Why Proxies Are Needed for Complex Objects

Direct conversion fails for structs and containers because Python needs:
- **Field access** (`player.health`)
- **Mutation** that updates the original C++ object
- **Iteration** (`for enemy in enemies`)
- **Lifetime tracking** when objects reference each other

Proxies provide these behaviors by implementing Python object slots (`tp_getattro`, `tp_setattro`, `tp_iter`, etc.).

### Further Reading

**In This Project:**
- See `python_bind.hpp` for scalar conversion helpers
- See `reflection_value.hpp` for scalar value definitions
- See `FUNCTION_REFERENCE.md` for `PyBoundInt`, `PyBoundFloat`, `PyBoundBool`, `PyBoundString`

**External References:**
- Python C API — Numeric objects: https://docs.python.org/3/c-api/number.html
- Python C API — Unicode objects: https://docs.python.org/3/c-api/unicode.html
- Python C API — Boolean objects: https://docs.python.org/3/c-api/bool.html

---

## Python Proxy Pattern

### What Is a Python Proxy?

A **Python proxy** is a Python object that represents and provides access to a C++ object. It acts as a gateway between Python code and C++ data.

### Why Use Proxies?

| Challenge | Solution |
|-----------|----------|
| **Python doesn't understand C++ types** | Proxy wraps C++ object with Python interface |
| **C++ objects may not match Python lifecycle** | Proxy manages lifetime and access |
| **Need to intercept Python operations** | Proxy implements `__getattr__`, `__setattr__`, etc. |
| **Complex types need special handling** | Proxy converts between Python and C++ formats |

### Simple Proxy Example

Without proxy, Python can't see C++ objects:

```cpp
// C++ side
struct Player {
    int health;
    std::string name;
};

Player cpp_player{"Hero", 100};

// Python side — DOESN'T WORK
// import cpp_module
// player = cpp_player  # Can't import raw C++ object
```

With proxy, Python gets a wrapper:

```cpp
// C++ side: Create proxy
typedef struct {
    PyObject_HEAD
    Player* cpp_object;      // Points to C++ object
} PlayerProxy;

PyObject* create_player_proxy(Player* player) {
    PlayerProxy* self = (PlayerProxy*)PyObject_New(PlayerProxy, &PlayerProxyType);
    if (self) {
        self->cpp_object = player;
    }
    return (PyObject*)self;
}
```

```python
# Python side — NOW WORKS
import cpp_module
player = cpp_module.create_player()  # Returns PlayerProxy
print(player.health)                  # Proxy intercepts and returns 100
```

### Proxy Architecture: Three-Part Pattern

```
┌─────────────────────────────────────────┐
│         Python Layer                    │
│  player.health = 50                     │
│  player.name = "Knight"                 │
└──────────────────────┬──────────────────┘
                       │ (Python calls)
                       ▼
┌─────────────────────────────────────────┐
│    Proxy Layer (PyObject)               │
│  PlayerProxy {                          │
│    .cpp_object → ────────┐              │
│    .tp_getattro()        │              │
│    .tp_setattro()        │              │
│  }                       │              │
└─────────────────────────┼────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────┐
│    C++ Layer (Real Object)              │
│  Player {                               │
│    .health = 50                         │
│    .name = "Knight"                     │
│  }                                      │
└─────────────────────────────────────────┘
```

### Implementing Proxy Attribute Access

Python proxies implement `PyTypeObject` slots for attribute access:

```cpp
// Get attribute: obj.health
static PyObject* PlayerProxy_getattro(PlayerProxy* self, PyObject* name) {
    const char* attr_name = PyUnicode_AsUTF8(name);
    
    if (strcmp(attr_name, "health") == 0) {
        return PyLong_FromLong(self->cpp_object->health);
    }
    else if (strcmp(attr_name, "name") == 0) {
        return PyUnicode_FromString(self->cpp_object->name.c_str());
    }
    
    PyErr_SetString(PyExc_AttributeError, "Unknown attribute");
    return NULL;
}

// Set attribute: obj.health = 100
static int PlayerProxy_setattro(PlayerProxy* self, 
                                 PyObject* name, 
                                 PyObject* value) {
    const char* attr_name = PyUnicode_AsUTF8(name);
    
    if (strcmp(attr_name, "health") == 0) {
        self->cpp_object->health = PyLong_AsLong(value);
        return 0;  // Success
    }
    else if (strcmp(attr_name, "name") == 0) {
        self->cpp_object->name = PyUnicode_AsUTF8(value);
        return 0;
    }
    
    PyErr_SetString(PyExc_AttributeError, "Unknown attribute");
    return -1;  // Error
}

// Register in PyTypeObject:
static PyTypeObject PlayerProxyType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "cpp_module.Player",
    .tp_getattro = (getattrofunc)PlayerProxy_getattro,
    .tp_setattro = (setattrofunc)PlayerProxy_setattro,
    // ... other fields
};
```

### Two Proxy Ownership Models

Proxies can own their C++ objects or reference external ones:

#### Model 1: Proxy Owns the Object (Proxy Allocated)

```cpp
// Proxy creates and owns C++ object
typedef struct {
    PyObject_HEAD
    Player* cpp_object;      // Owned by proxy
} PlayerProxy;

static PyObject* PlayerProxy_new(PyTypeObject* type, 
                                  PyObject* args, 
                                  PyObject* kwds) {
    PlayerProxy* self = (PlayerProxy*)type->tp_alloc(type, 0);
    if (self) {
        self->cpp_object = new Player();  // Proxy allocates
    }
    return (PyObject*)self;
}

static void PlayerProxy_dealloc(PlayerProxy* self) {
    delete self->cpp_object;  // Proxy deletes when destroyed
    Py_TYPE(self)->tp_free(self);
}

// Python usage:
# player = cpp_module.Player()  # Proxy creates C++ object
# del player                     # Proxy deletes C++ object
```

#### Model 2: Proxy References External Object (Array Allocated)

```cpp
// C++ creates object in array, Python gets reference-only proxy
typedef struct {
    PyObject_HEAD
    Player* cpp_object;      // Points to external object
    bool owns_object;        // Tracks ownership
} PlayerProxy;

static PyObject* PlayerProxy_from_cpp(Player* player, bool take_ownership) {
    PlayerProxy* self = (PlayerProxy*)PyObject_New(PlayerProxy, &PlayerProxyType);
    if (self) {
        self->cpp_object = player;
        self->owns_object = take_ownership;
    }
    return (PyObject*)self;
}

static void PlayerProxy_dealloc(PlayerProxy* self) {
    if (self->owns_object) {
        delete self->cpp_object;  // Only delete if we own it
    }
    Py_TYPE(self)->tp_free(self);
}

// C++ usage:
std::vector<Player> players;
players.push_back(Player{"Hero", 100});

// Create reference-only proxy (Python doesn't own)
PyObject* proxy = PlayerProxy_from_cpp(&players[0], false);
// When Python deletes proxy, C++ object survives (in vector)
```

### Parent Tracking (Advanced Proxy Pattern)

When a proxy references a nested object, it must track the parent:

```cpp
// Example: Accessing player.equipment[0]
// If equipment is stored in player, Python proxy needs to know parent

typedef struct {
    PyObject_HEAD
    Equipment* cpp_object;
    PyObject* parent_proxy;  // Who owns me?
    Py_ssize_t index;        // Or which element in parent?
} EquipmentProxy;

static PyObject* EquipmentProxy_getattro(EquipmentProxy* self, 
                                         PyObject* name) {
    // Before accessing cpp_object, verify parent still exists
    if (self->parent_proxy == NULL) {
        PyErr_SetString(PyExc_RuntimeError, 
                       "Parent object was deleted");
        return NULL;
    }
    
    // Safe to access cpp_object
    // ... get attribute ...
}

static void EquipmentProxy_dealloc(EquipmentProxy* self) {
    Py_XDECREF(self->parent_proxy);  // Release parent reference
}
```

### Proxy Lifecycle and Reference Counting

Python uses reference counting. Proxies must participate:

```cpp
// Reference counting in proxies:

typedef struct {
    PyObject_HEAD
    Player* cpp_object;
    PyObject* parent_proxy;
} PlayerProxy;

// When proxy is created: increment refcount
PyObject* create_proxy(Player* player) {
    PlayerProxy* self = (PlayerProxy*)PyObject_New(PlayerProxy, &PlayerProxyType);
    if (self && parent) {
        Py_INCREF(parent);  // Increment parent's refcount
        self->parent_proxy = parent;
    }
    return (PyObject*)self;
}

// When proxy is destroyed: decrement refcount
static void PlayerProxy_dealloc(PlayerProxy* self) {
    Py_XDECREF(self->parent_proxy);  // Decrement parent's refcount
    Py_TYPE(self)->tp_free(self);
}

// Why? So Python knows when objects are still needed:
# Python holds reference = Py_INCREF incremented refcount
# Python releases reference = Py_DECREF decrements refcount
# When refcount reaches 0, object deleted
```

### Common Proxy Patterns

| Pattern | Use Case | Example |
|---------|----------|---------|
| **Value Proxy** | Simple POD types | Integer, string wrapper |
| **Struct Proxy** | User-defined structs | Player, Enemy proxy |
| **Vector Proxy** | Containers | std::vector<int> proxy |
| **Iterator Proxy** | Collection iteration | for loop support |
| **Callback Proxy** | Bridging C++ callbacks | Event system |

### Proxy vs Direct Exposure

| Approach | Pros | Cons |
|----------|------|------|
| **Direct (no proxy)** | Simple, fast | Only works for simple types; no lifetime control |
| **Proxy** | Full control; supports complex types; can add validation | Extra indirection; more code |
| **Smart Proxy** | Transparent behavior; tracks parent | Complex; must handle edge cases |

### Further Reading

**In This Project:**
- See `ARCHITECTURE_DEEP_DIVE.md` Section IV for proxy object implementations (StructProxyObject, VectorProxyObject)
- See `python_proxy.cpp` for real proxy implementation details
- See `FUNCTION_REFERENCE.md` for proxy slot implementations (tp_getattro, tp_setattro, tp_dealloc)
- See `WRAPPER_OWNERSHIP_PATTERN.md` for proxy ownership semantics and lifetime management

**External References:**
- Python C API — Object protocol: https://docs.python.org/3/c-api/object.html
- Python C API — Number protocol: https://docs.python.org/3/c-api/number.html
- Python C API — Mapping protocol: https://docs.python.org/3/c-api/mapping.html
- Reference counting and garbage collection: https://docs.python.org/3/c-api/refcounting.html

---

## Further Reading

### In This Project

These concepts are implemented in the project files. For specific details:

**For Type Traits and Constexpr:**
- See `ARCHITECTURE_DEEP_DIVE.md` Section II for binding bridge implementation
- See `DESIGN_PATTERNS_AND_EXTENSIBILITY.md` Section I for type dispatch patterns

**For Python Module Structure:**
- See `SOURCE_CODE_DOCUMENTATION.md` for files like `cpp_module.cpp`
- See `FUNCTION_REFERENCE.md` for type definitions and method implementations

**For Reflection Pattern:**
- See `ARCHITECTURE_DEEP_DIVE.md` Section I for reflection layer
- See `SOURCE_CODE_DOCUMENTATION.md` for `reflection_*.hpp` files

**For Type Erasure:**
- See `DESIGN_PATTERNS_AND_EXTENSIBILITY.md` Pattern 1
- See `ARCHITECTURE_DEEP_DIVE.md` Section III for void* + metadata usage

**For Python Proxies:**
- See `ARCHITECTURE_DEEP_DIVE.md` Section IV for proxy object implementations
- See `python_proxy.cpp` for real StructProxyObject, VectorProxyObject, and VectorIteratorObject
- See `FUNCTION_REFERENCE.md` for proxy slot implementations (tp_getattro, tp_setattro)
- See `WRAPPER_OWNERSHIP_PATTERN.md` for proxy ownership semantics

**For Integration Details:**
- See `USAGE_GUIDE.md` for practical examples
- See `FUNCTION_REFERENCE.md` for API details

### External References

**Python C API Documentation:**
- Official: https://docs.python.org/3/c-api/
- Type objects: https://docs.python.org/3/c-api/type.html
- Module definition: https://docs.python.org/3/c-api/module.html

**C++ Template Metaprogramming:**
- cppreference.com — Type traits: https://en.cppreference.com/w/cpp/header/type_traits
- cppreference.com — constexpr: https://en.cppreference.com/w/cpp/language/constexpr
- cppreference.com — if constexpr: https://en.cppreference.com/w/cpp/language/if

**Type Erasure Patterns:**
- "Effective C++" by Scott Meyers (Item 26-29: Resource management, PIMPL)
- "C++ Templates" by Josuttis and Vandevoorde (Advanced type manipulation)

**Reflection Techniques:**
- https://abiword.github.io/reflection/ — Modern C++ reflection proposals
- Boost.Reflection library discussions

---

## Quick Reference: Key Concepts

| Concept | Quick Definition | Use When |
|---------|------------------|----------|
| **Type Traits** | Compile-time type predicates | Need to dispatch on type without runtime cost |
| **Constexpr** | Compute at compile-time | Value known at build time |
| **if constexpr** | Branch at compile-time | Different branches for different types |
| **Reflection** | Runtime type information | Need to work with types dynamically |
| **Type Erasure** | void* + metadata | Store many types in one container |
| **PyModuleDef** | Module metadata | Define a Python extension module |
| **PyTypeObject** | Type metadata | Define a Python type/class |
| **PyMethodDef** | Method table | Define Python-callable functions |
| **PyMODINIT_FUNC** | Module entry point | Initialize module on import |
| **Field Offset** | Byte distance in struct | Access fields without knowing struct type |
| **Python Proxy** | Wrapper object representing C++ data | Expose C++ objects to Python with controlled access |
| **tp_getattro** | Get attribute slot | Intercept Python attribute access (obj.attr) |
| **tp_setattro** | Set attribute slot | Intercept Python attribute assignment (obj.attr = val) |
| **Reference Counting** | Track object ownership | Manage lifetime with Py_INCREF/Py_DECREF |
| **Parent Tracking** | Nested proxy reference tracking | Keep parent alive while child proxy exists |
| **Proxy Ownership** | Two owning models | Proxy-allocated vs reference-only proxies |

---

**Last Updated:** February 2026
