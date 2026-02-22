# Architecture Deep Dive: Pure C++ Reflection Enables Multi-Language Scripting

## I. The Three-Layer Design Philosophy

This project solves a fundamental problem in embedded systems: **How do we expose C++ data structures to scripting languages without baking language-specific details throughout the codebase?**

### The Problem with Traditional Approaches

```cpp
// ❌ BAD: Python mixed throughout
void expose_player(const char *var_name, Player &player) {
    PyObject *dict = PyDict_New();
    PyDict_SetItemString(dict, "health", 
        PyLong_FromLong(player.health));  // Python details here
    PyDict_SetItemString(dict, "name",
        PyUnicode_FromString(player.name.c_str()));  // And here
    // If you need Lua later: duplicate all this with lua_* calls!
    Python_register_struct(var_name, dict);
}
```

**Cost to add Lua support:** Rewrite entire function with Lua API calls (4+ hours)

### The Solution: Three Independent Layers

```
LAYER 3: PYTHON INTEGRATION LAYER
┌──────────────────────────────────────────────────────────┐
│ (cpp_module.cpp, python_proxy.cpp, python_bind.hpp)      │
│ INCLUDES: #include <Python.h>                            │
│ CONCERN: "How do we make C++ data Pythonic?"             │
│ • PyObject manipulation                                  │
│ • Python protocol methods (tp_getattro, tp_iternext)     │
│ • Type conversion functions                              │
└──────────────────────────────────────────────────────────┘
         ↑ depends on, wraps results from
         │
LAYER 2: BINDING BRIDGE LAYER
┌──────────────────────────────────────────────────────────┐
│ (value_interface.hpp)                                    │
│ INCLUDES: STL only, NO Python.h                          │
│ CONCERN: "How do we register and dispatch types?"        │
│ • Type traits (is_reflected_struct, is_std_vector)       │
│ • Compile-time dispatch (if constexpr)                   │
│ • Central registry (PyInterface::g_values)               │
│ • Factory pattern (create appropriate BoundValue)        │
└──────────────────────────────────────────────────────────┘
         ↑ depends on, inspects metadata from
         │
LAYER 1: PURE C++ REFLECTION LAYER
┌──────────────────────────────────────────────────────────┐
│ (reflection_value.hpp, reflection_struct.hpp,            │
│  reflection_vector.hpp)                                  │
│ INCLUDES: <vector>, <string> ONLY                        │
│ CONCERN: "How do we describe C++ data structures?"       │
│ • ValueType enum (Int, Float, Bool, String, Struct...)   │
│ • BoundValue, BoundStruct, BoundVector (base classes)    │
│ • FieldInfo, StructInfo, VectorInfo (metadata)           │
│ • Offset arithmetic (zero-copy field access)             │
│ • Function pointers (type-agnostic operations)           │
└──────────────────────────────────────────────────────────┘
```

---

## II. Layer 1: Pure C++ Reflection – The Foundation

### Design Mandate: Zero Python Dependencies

**Rationale:**
- Reflection describes *data structure shape*, not how to display it
- Whether we're talking to Python, Lua, or a C CLI shouldn't affect how we describe a struct
- Enables third-party language bindings without source code access

**Verification:**
```bash
$ grep -r "Python" reflection_*.hpp
# (no output – no Python references!)

$ grep -r "#include" reflection_value.hpp reflection_struct.hpp reflection_vector.hpp
#include <vector>     # Absolutely necessary (VectorInfo stores elements)
#include <string>     # Absolutely necessary (Names must be strings)
#include <cstring>    # (optional: for offset calculations)
```

### Component 1: reflection_value.hpp (~25 lines)

**The Type System:**
```cpp
enum class ValueType {
    Int,        // 4 bytes, portable across platforms
    Float,      // 4 bytes
    Bool,       // 1 byte (uses ByteBool = std::byte)
    String,     // std::string (variable size)
    Struct,     // User-defined compound type
    Vector      // std::vector<ElementType>
};

struct BoundValue {
    std::string name;       // "health", "inventory", "enemies"
    ValueType type;         // What am I?
    virtual ~BoundValue() = default;
};
```

**Key Decision: Enum vs Virtual Methods**
- ❌ Virtual methods: Each value object carries a vtable pointer (8 bytes overhead)
- ✅ Enum: Single byte, enables straightforward switch statements
- ✅ Pattern matches Unreal Engine, Godot, and other reflection systems

### Component 2: reflection_struct.hpp (~100 lines)

**The Problem:** How describe a struct like `Player { int health; std::string name; }` without templates?

**The Answer:** Offset-based field metadata

```cpp
struct FieldInfo {
    std::string name;           // Field identifier
    size_t offset;              // Byte position from struct start
    ValueType type;             // What kind of field
    void *type_meta;            // Extra info for compound types
};

struct StructInfo {
    std::string name;           // "Player"
    std::vector<FieldInfo> fields;  // All fields with metadata
};
```

**The Offset Magic – How It Works:**

```cpp
// C++ struct definition (user code, main.cpp):
struct Player {
    int health;        // Offset 0
    std::string name;  // Offset 4 (assuming int is 4 bytes)
    bool is_alive;     // Offset ~32 (std::string is variable size, typically 24-32 bytes)
};

// Metadata (generated once, static):
FieldInfo fields[] = {
    { "health", offsetof(Player, health), ValueType::Int, nullptr },
    { "name", offsetof(Player, name), ValueType::String, nullptr },
    { "is_alive", offsetof(Player, is_alive), ValueType::Bool, nullptr }
};

// Reflection runtime (BoundStruct):
class BoundStruct {
    void *m_instance;           // Points to actual Player object
    const StructInfo *m_info;   // Points to metadata above
    
    // Read field from memory
    int read_health() {
        const FieldInfo *field = m_info->get_field("health");
        void *field_ptr = m_instance + field->offset;
        return *(int*)field_ptr;  // D read from &player.health
    }
    
    // Modify field in-place
    void set_health(int new_value) {
        const FieldInfo *field = m_info->get_field("health");
        void *field_ptr = m_instance + field->offset;
        *(int*)field_ptr = new_value;  // Write directly to memory
    }
};
```

**Why This Is Powerful:**

1. **Zero-Copy:** Reading doesn't copy data – just read from memory
2. **Zero-Overhead:** Offset is compile-time, pointer arithmetic is one instruction
3. **Language-Agnostic:** Offset arithmetic works the same in Lua, Ruby, Go
4. **Direct Modification:** Write directly without marshaling through objects

**Example – Nested Structures:**
```cpp
struct Team {
    std::string name;
    Player leader;          // Nested struct!
};

// Metadata for nested player:
FieldInfo fields[] = {
    { "name", offsetof(Team, name), ValueType::String, nullptr },
    { "leader", offsetof(Team, leader), ValueType::Struct, &player_info }
    //                                                     ^^^ Points to Player's StructInfo
};

// Reading leader.health:
StructInfo *team_info = ...;
const FieldInfo *leader_field = team_info->get_field("leader");
void *leader_ptr = team_instance + leader_field->offset;
// leader_ptr now points to the nested Player struct!
// Access health via Player's StructInfo...
```

### Component 3: reflection_vector.hpp (~85 lines)

**The Problem:** How describe `std::vector<T>` where `T` can be int, Player, or even `std::vector<int>`?

**The Answer:** Function pointers + type metadata

```cpp
struct VectorInfo {
    ValueType element_type;      // What elements does this vector hold?
    void *element_meta;          // StructInfo* or VectorInfo* if compound
    
    // Function pointers – how to operate on this specific vector type
    std::size_t (*size_fn)(void *vec_ptr);               // Get size
    void *(*element_ptr_fn)(void *vec_ptr, std::size_t); // Get element address
    bool (*append_fn)(void *vec_ptr, void *value_ptr);   // Append element
};

class BoundVector : public BoundValue {
    void *m_vec_ptr;            // Pointer to actual std::vector<T>
    const VectorInfo *m_info;   // Metadata describing T
    
    size_t size() const {
        return m_info->size_fn(m_vec_ptr);  // Calls appropriate function
    }
    
    void *element_ptr(size_t index) {
        return m_info->element_ptr_fn(m_vec_ptr, index);  // Type-specific accessor
    }
};
```

**Function Pointers: Why This Pattern?**

```cpp
// For std::vector<int>:
static size_t vec_int_size(void *ptr) {
    return static_cast<std::vector<int>*>(ptr)->size();
}
static void *vec_int_element(void *ptr, size_t i) {
    auto *v = static_cast<std::vector<int>*>(ptr);
    return &(*v)[i];  // Return address of element
}
static bool vec_int_append(void *ptr, void *value_ptr) {
    auto *v = static_cast<std::vector<int>*>(ptr);
    v->push_back(*(int*)value_ptr);
    return true;
}

// For std::vector<Player>:
static size_t vec_player_size(void *ptr) {
    return static_cast<std::vector<Player>*>(ptr)->size();
}
static void *vec_player_element(void *ptr, size_t i) {
    auto *v = static_cast<std::vector<Player>*>(ptr);
    return &(*v)[i];
}
// ... etc

// Stored generically across different vector types:
VectorInfo scores_info{
    ValueType::Int,
    nullptr,
    vec_int_size,           // ← Specific function
    vec_int_element,        // ← Specific function
    vec_int_append          // ← Specific function
};

VectorInfo enemies_info{
    ValueType::Struct,
    &player_struct_info,
    vec_player_size,        // ← Different function!
    vec_player_element,     // ← Different function!
    vec_player_append       // ← Different function!
};
```

**Why Not Virtual Methods?**
- Virtual methods tie types to objects via vtable
- std::vector<T> doesn't have behavior – it just holds data
- Function pointers encode "how to operate on T" as compile-time knowledge
- Enables C-only applications to use reflection (C has no virtual methods!)

---

## III. Layer 2: Binding Bridge – Type Detection & Registration

### File: value_interface.hpp (~150 lines)

**Core Challenge:** User writes `PyInterface::bind("player", player_instance)` – how do we know what `player_instance` is?

**Solution:** Type traits + compile-time dispatch

### Type Traits Pattern

```cpp
// Detect std::vector<T>
template<typename T>
struct is_std_vector : std::false_type {};

template<typename T>
struct is_std_vector<std::vector<T>> : std::true_type {};

// Detect user-defined structs (user specializes this!)
template<typename T>
struct is_reflected_struct : std::false_type {};

// In data_game_traits.hpp or similar:
template<>
struct is_reflected_struct<Player> : std::true_type {};

template<>
struct is_reflected_struct<Enemy> : std::true_type {};

template<>
struct is_reflected_struct<Team> : std::true_type {};
```

**Why Type Traits?**
- Compile-time checks (zero runtime cost)
- User can specialize for their types
- Works with SFINAE (Substitution Failure Is Not An Error)
- Enables detection without modifying source code

### Metadata Provider Pattern

```cpp
// User specializes for each reflected type:
template<>
inline const StructInfo *get_struct_info<Player>() {
    static StructInfo player_info{
        "Player",
        {
            { "health", offsetof(Player, health), ValueType::Int, nullptr },
            { "name", offsetof(Player, name), ValueType::String, nullptr }
        }
    };
    return &player_info;
}

// Ditto for Enemy, Team, etc.
```

### Compile-Time Dispatch with if constexpr

```cpp
template <typename T>
static void bind(const std::string &name, T &variable) {
    if constexpr (is_reflected_struct<T>::value) {
        // Branch A: User-defined struct
        g_values[name] = std::make_unique<BoundStruct>(
            name, 
            &variable, 
            get_struct_info<T>()  // Get metadata
        );
    } else if constexpr (is_std_vector<T>::value) {
        // Branch B: std::vector<Element>
        using Element = typename T::value_type;
        
        if constexpr (is_reflected_struct<Element>::value) {
            // std::vector<Player>
            g_values[name] = std::make_unique<BoundVector>(
                name, 
                &variable, 
                get_vector_info<Element>()
            );
        } else if constexpr (is_std_vector<Element>::value) {
            // std::vector<std::vector<...>> (nested!)
            g_values[name] = std::make_unique<BoundVector>(
                name, 
                &variable, 
                get_vector_info<Element>()
            );
        } else {
            // std::vector<int>, std::vector<float>, etc.
            g_values[name] = std::make_unique<BoundVector>(
                name, 
                &variable, 
                get_vector_info<Element>()
            );
        }
    } else if constexpr (std::is_same_v<T, int>) {
        // Branch C: Scalar int
        g_values[name] = std::make_unique<PyBoundInt>(name, variable);
    } else if constexpr (std::is_same_v<T, float>) {
        // Branch D: Scalar float
        g_values[name] = std::make_unique<PyBoundFloat>(name, variable);
    } else if constexpr (std::is_same_v<T, bool>) {
        // Branch E: Scalar bool
        g_values[name] = std::make_unique<PyBoundBool>(name, variable);
    } else if constexpr (std::is_same_v<T, std::string>) {
        // Branch F: Scalar string
        g_values[name] = std::make_unique<PyBoundString>(name, variable);
    }
}
```

**Optimization: Dead Code Elimination**

```cpp
// When user calls: PyInterface::bind("player", player_instance);
// Type is: Player (a reflected struct)

// Only branch A compiles for Player!
// Branches B, C, D, E, F are completely eliminated by compiler
// → Zero runtime cost for type detection
// → No if/else chains at runtime

// When user calls: PyInterface::bind("scores", scores_vector);
// Type is: std::vector<int>

// Only branch B compiles for vector<int>
// All other branches eliminated
// → Different code path, optimized for vectors
```

### Global Registry Pattern

```cpp
class PyInterface {
    // Single source of truth for all bound variables
    static inline std::unordered_map<
        std::string,
        std::unique_ptr<BoundValue>
    > g_values;
    
    static BoundValue* get_value_raw(const std::string &name) {
        auto it = g_values.find(name);
        return it != g_values.end() ? it->second.get() : nullptr;
    }
};
```

**Why Centralized Registry?**

1. **Dynamic Discovery:** Python module doesn't list all variables upfront
2. **Language-Agnostic:** Lua, Ruby, Perl all use same registry
3. **Hot Addition:** Can add new variables at runtime without recompiling module
4. **Single Source Truth:** All features (introspection, validation) use one registry

---

## IV. Layer 3: Python Integration – Bringing It All Together

### Core Challenge: Making C++ Data Pythonic

When Python accesses `cpp.player.health`, it expects:
- Attribute access (`.` operator)
- Type conversion (int → Python int)
- Error handling (field doesn't exist → AttributeError)
- Memory safety (can't access freed memory)

### cpp_module.cpp: Dynamic Module With Custom Getattr

**Standard Python Modules are Static:**
```python
import math
# math.sin, math.cos, etc. are defined in C
# module has fixed attribute list
```

**Our Custom Module is Dynamic:**
```python
import cpp
cpp.player         # Dynamically looks up "player"
cpp.scores         # Dynamically looks up "scores"
cpp.any_new_var    # Can add variables at runtime!
```

**How? Custom PyTypeObject for the module:**

```cpp
static PyTypeObject CppModuleType = {
    PyVarObject_HEAD_INIT(&PyModule_Type, 0)  // Inherit from module
    .tp_name = "cpp.module",
    .tp_getattro = cpp_module_getattr,        // Override attribute access!
    .tp_setattro = cpp_module_setattr,        // Override assignment!
    .tp_doc = "...",
};

static PyObject *cpp_module_getattr(PyObject *module, PyObject *name) {
    // Extract string from Python name object
    const char *name_str = PyUnicode_AsUTF8(name);
    if (!name_str) return nullptr;
    
    // Look up in our registry
    BoundValue *bound = PyInterface::get_value_raw(name_str);
    if (!bound) {
        return PyErr_Format(PyExc_AttributeError, 
            "cpp module has no attribute '%s'", name_str);
    }
    
    // Dispatch based on type
    switch (bound->type) {
        case ValueType::Int: {
            PyBoundInt *pv = static_cast<PyBoundInt*>(bound);
            return pv->to_python();  // Convert int* → PyObject*
        }
        case ValueType::Struct: {
            BoundStruct *bs = static_cast<BoundStruct*>(bound);
            return StructProxy_New(bs);  // Wrap in proxy
        }
        case ValueType::Vector: {
            BoundVector *bv = static_cast<BoundVector*>(bound);
            return VectorProxy_New(bv);  // Wrap in proxy
        }
        // ... handle all ValueTypes ...
    }
}
```

**Flow Example: `cpp.player.health`**

```
Python:        cpp.player
                    ↓
Module:        cpp_module_getattr(module, "player")
                    ↓
Registry:      PyInterface::get_value_raw("player") → BoundStruct
                    ↓
Proxy:         StructProxy_New(bound_struct) → Returns PyObject*
                    ↓
Python:        <StructProxy object>
                    ↓
Python:        <StructProxy>.health
                    ↓
Proxy:         StructProxy_getattro(proxy, "health")
                    ↓
Reflection:    BoundStruct::get_field_ptr("health")
               → (void*)&player + offset_of_health
                    ↓
Memory:        Read from memory at that address
                    ↓
Conversion:    PyLong_FromLong(value) → PyObject*
                    ↓
Python:        100  (int object)
```

### python_proxy.cpp: Pythonic Semantics for C++ Data

**StructProxy: Field Access (450 lines)**

```cpp
// When user writes: player.health
PyObject *StructProxy_getattro(PyObject *self, PyObject *attr) {
    auto *proxy = (StructProxy *)self;
    const BoundStruct *bound = proxy->bound;
    const char *attr_name = PyUnicode_AsUTF8(attr);
    
    // Step 1: Metadata lookup
    const FieldInfo *field = bound->get_field(attr_name);
    if (!field) {
        return PyErr_Format(PyExc_AttributeError, 
            "struct has no field '%s'", attr_name);
    }
    
    // Step 2: Pointer arithmetic
    void *field_ptr = bound->get_field_ptr(field);
    
    // Step 3: Type-dispatch conversion
    switch (field->type) {
        case ValueType::Int:
            return PyLong_FromLong(*(int*)field_ptr);
        
        case ValueType::Float:
            return PyFloat_FromDouble(*(float*)field_ptr);
        
        case ValueType::Bool: {
            auto b = *(ByteBool*)field_ptr;
            return PyBool_FromLong(b == TRUE_BYTE ? 1 : 0);
        }
        
        case ValueType::String: {
            auto *s = static_cast<std::string*>(field_ptr);
            return PyUnicode_FromString(s->c_str());
        }
        
        case ValueType::Struct: {
            auto *si = static_cast<StructInfo*>(field->type_meta);
            BoundStruct *inner = new BoundStruct(...);
            return StructProxy_New(inner);
        }
        
        case ValueType::Vector: {
            auto *vi = static_cast<VectorInfo*>(field->type_meta);
            BoundVector *inner = new BoundVector(...);
            return VectorProxy_New(inner);
        }
    }
}
```

**VectorProxy: Indexing (600 lines)**

```cpp
PyObject *VectorProxy_getitem(PyObject *self, Py_ssize_t index) {
    auto *proxy = (VectorProxy *)self;
    const BoundVector *bound = proxy->bound;
    
    Py_ssize_t size = (Py_ssize_t)bound->size();
    
    // Handle negative indexing (Python feature)
    if (index < 0) index += size;
    
    if (index < 0 || index >= size) {
        return PyErr_Format(PyExc_IndexError, "index out of range");
    }
    
    // Pointer to element
    void *elem_ptr = bound->element_ptr(index);
    
    // Type-specific conversion
    switch (bound->info()->element_type) {
        case ValueType::Int:
            return PyLong_FromLong(*(int*)elem_ptr);
        case ValueType::Struct:
            // Wrap in StructProxy
            auto *si = static_cast<StructInfo*>(bound->info()->element_meta);
            BoundStruct *bs = new BoundStruct(..., elem_ptr, si);
            return StructProxy_New(bs);
        // ... etc
    }
}
```

**Iterator Protocol: For Loops (150 lines)**

```cpp
// When user writes: for enemy in enemies:
// Python calls: iter(enemies_proxy)

PyObject *VectorProxy_iter(PyObject *self) {
    auto *it = PyObject_New(VectorIteratorObject, &VectorIteratorType);
    it->vector = (VectorProxy*)self;
    it->index = 0;
    Py_INCREF((PyObject*)it->vector);  // Keep vector alive
    return (PyObject*)it;
}

// Python then repeatedly calls: next(iterator)
PyObject *VectorIterator_next(PyObject *self) {
    auto *it = (VectorIteratorObject *)self;
    auto *proxy = it->vector;
    const BoundVector *bound = proxy->bound;
    
    // Check if done
    if ((size_t)it->index >= bound->size()) {
        PyErr_SetNone(PyExc_StopIteration);
        return nullptr;  // Signals end of iteration
    }
    
    // Return next element
    PyObject *result = VectorProxy_getitem((PyObject*)proxy, it->index);
    it->index++;  // Advance current position
    return result;
}
```

### python_bind.hpp: Scalar Conversions (180 lines)

**Problem:** How do we convert between Python types and C++ types safely?

**Solution:** Type-specific conversion classes

```cpp
class PyBoundValue : public BoundValue {
    virtual PyObject *to_python() = 0;          // C++ → Python
    virtual bool from_python(PyObject *obj) = 0;  // Python → C++
};

// For int*
class PyBoundInt : public PyBoundValue {
    int *m_ptr;
    
    PyObject *to_python() override {
        return PyLong_FromLong(*m_ptr);  // *m_ptr = 42 → Python 42
    }
    
    bool from_python(PyObject *obj) override {
        if (!PyLong_Check(obj)) return false;
        long value = PyLong_AsLong(obj);
        if (PyErr_Occurred()) return false;
        *m_ptr = (int)value;  // Python 42 → *m_ptr = 42
        return true;
    }
};

// For std::string*
class PyBoundString : public PyBoundValue {
    std::string *m_ptr;
    
    PyObject *to_python() override {
        return PyUnicode_FromString(m_ptr->c_str());  // "hello" → Python "hello"
    }
    
    bool from_python(PyObject *obj) override {
        const char *str = PyUnicode_AsUTF8(obj);
        if (!str) return false;
        *m_ptr = str;  // Python "world" → *m_ptr = "world"
        return true;
    }
};
```

**Key Safety Pattern: PyUnicode_AsUTF8**

```cpp
// ❌ WRONG: Gets temporary pointer into Python's buffer
const char *str = PyUnicode_AsUTF8(obj);
strcpy(my_buffer, str);  // ← String may be freed before copy!

// ✅ RIGHT: Gets pointer, immediately copy
const char *str = PyUnicode_AsUTF8(obj);
if (!str) return false;
my_string = str;  // std::string constructor copies immediately!
```

---

## V. Design Patterns and Their Justifications

### Pattern 1: Type-Erasure with void*

**Usage:** Field pointers, element pointers, vector pointers

**Why:**
- C++ templates resolve fully at compile time
- Real-world C++ code has types unknown at library build time
- Only runtime solution: opaque pointers + type metadata
- Matches C convention for generic code

**Trade-off:**
- ❌ No compile-time type checking for pointer operations (must verify at runtime)
- ✅ Works with user-defined types without recompilation
- ✅ Enables runtime type discovery

### Pattern 2: Function Pointers for Type-Specific Operations

**Usage:** VectorInfo::size_fn, element_ptr_fn, append_fn

**Why:**
- Avoids virtual inheritance on containers (semantically wrong)
- Enables C-only tools to use reflection
- Compiler can inline known function pointers
- Pattern used in Linux kernel, GTK+, etc.

**Alternative Considered:** std::function
- ❌ Requires additional heap allocation
- ❌ Adds overhead for every operation
- ✅ Function pointers are zero-cost abstractions

### Pattern 3: Offset-Based Field Access

**Usage:** StructInfo::FieldInfo::offset

**Why:**
- Computed once with offsetof() macro
- Pointer arithmetic is single CPU instruction
- Works across all languages that support memory access
- Portable across platforms (same struct layout guarantee)
- Enables in-place modification without copying

**Requirement:** C++ standard layout structs (POD types)
- No virtual functions
- No inheritance
- No private/protected members (for offset visibility)

### Pattern 4: Compile-Time Dispatch with if constexpr

**Usage:** PyInterface::bind<T>()

**Why:**
- Each instantiation compiles only the necessary branches
- Dead code elimination by compiler
- Zero runtime branch prediction cost
- Type system enforced by compiler (catch errors early)

---

## VI. Future Multi-Language Extensions

### Why This Architecture Scales

**Today:** C++ ↔ Python (all via python_proxy.cpp)

**Tomorrow (Hypothetical):**
```
C++ ↔ Python   (python_proxy.cpp, cpp_module.cpp)
C++ ↔ Lua      (lua_proxy.cpp, lua_module.cpp)
C++ ↔ Ruby     (ruby_proxy.cpp, ruby_module.cpp)

All sharing: reflection_*.hpp (UNCHANGED!)
```

### Adding Lua Support: What Changes?

**New Files (200 lines each):**
- `lua_proxy.cpp` - Lua userdata wrappers (analog to StructProxy, VectorProxy)
- `lua_module.c` - Module initialization (analog to PyInit_cpp)

**Reused Unchanged:**
- `reflection_value.hpp` - ValueType enum still describes element types
- `reflection_struct.hpp` - BoundStruct still maps fields via offset
- `reflection_vector.hpp` - BoundVector still uses function pointers
- `value_interface.hpp` - Type dispatch still works!

**Compilation:**
```bash
# Reflection compiled once
g++ -c reflection_struct.cpp -o reflection_struct.o

# Python binding
g++ -I/usr/include/python3.11 ... main.cpp cpp_module.cpp python_proxy.cpp reflection_struct.o

# Lua binding (NEW, with reflection reuse)
gcc -I/usr/include/lua5.4 ... main.c lua_module.c lua_proxy.c reflection_struct.o
```

### Code Reuse Example – Reading a Struct Field

**Python Implementation (python_proxy.cpp):**
```cpp
void *field_ptr = bound_struct->get_field_ptr(field);
return PyLong_FromLong(*(int*)field_ptr);  // Python-specific
```

**Equivalent Lua Implementation (hypothetical lua_proxy.c):**
```cpp
void *field_ptr = bound_struct->get_field_ptr(field);
lua_pushinteger(L, *(int*)field_ptr);  // Lua-specific
```

**Both use the same:**
- BoundStruct class
- StructInfo metadata
- Offset-based get_field_ptr() method

---

## VII. Summary: Layers and Concerns

| Layer | Files | Concern | Python.h? |
|-------|-------|---------|-----------|
| 3: Integration | cpp_module.cpp, python_proxy.cpp, python_bind.hpp | How do we expose C++ to Python? | ✅ Required |
| 2: Bridge | value_interface.hpp | How do we detect and register types? | ❌ No |
| 1: Reflection | reflection_*.hpp | How do we describe C++ structures? | ❌ No |

**Each layer has a single responsibility:**
- Layer 1: Describe data (struct fields, vector elements)
- Layer 2: Detect type and create appropriate wrapper
- Layer 3: Make wrappers Pythonic (attributes, indexing, iteration)

**Each layer depends only on layers below it:**
- Layer 3 calls Layer 2's BoundValue, PyInterface
- Layer 2 calls Layer 1's BoundStruct, StructInfo
- Layer 1 has no dependencies (except STL)

**This separation enables:**
- Third-party language implementations without modifying reflection code
- Audit of critical data structure layer independent of Python
- Reuse of reflection in other contexts (game engines, databases, APIs)
- Clear responsibility boundaries for maintenance
