# Complete Ownership Models Guide

## Table of Contents

- [Overview](#overview)
1. [Ownership Fundamentals](#ownership-fundamentals)
2. [Registry Ownership (g_values)](#registry-ownership-g_values)
3. [Scalar Type Ownership](#scalar-type-ownership)
4. [Complex Type Ownership (Structs & Vectors)](#complex-type-ownership-structs--vectors)
5. [Wrapper Ownership Pattern](#wrapper-ownership-pattern)
6. [Parent-Child Proxy Reference Management](#parent-child-proxy-reference-management)
7. [Python Reference Counting](#python-reference-counting)
8. [Thread Safety & Singleton Management](#thread-safety--singleton-management)
9. [Ownership Decision Tree](#ownership-decision-tree)

## Overview

**Issue 44: Comprehensive Documentation of Ownership Models**

This document provides authoritative documentation of all ownership semantics in the CPP-Python binding system, resolving Issue 44 and serving as the canonical reference for understanding data lifetime, reference counting, and memory management patterns.

**Target Audience:** Developers implementing features involving ownership, debugging memory issues, or understanding the complete memory management architecture.

**Key Topics:** Registry ownership, scalar vs complex type ownership, wrapper pattern, parent-child reference management, Python reference counting, and thread safety.

---

[Back to Table of Contents](#table-of-contents)


## Ownership Fundamentals

### Core Principle

**Different ownership models serve different needs:**

1. **Registry Ownership (g_values):** Lifetime = program duration
2. **Scalar Ownership:** Copy-on-access (no proxy needed)
3. **Complex Type Ownership:** Wrapper copies held by proxies
4. **Parent-Child Relationship:** Reference counting for nested proxies

### Memory Domains

```
┌─────────────────────────────────────────────────────────────┐
│ C++ Domain (main.cpp)                                       │
│ ─────────────────────────────────────────────────────────   │
│ Owned by: Stack, global, heap (program code)                │
│ Lifetime: Original variable scope or allocation site        │
│ Access: Direct pointers, references                         │
└─────────────────────────────────────────────────────────────┘
          ▲ Raw pointers (borrowed, non-owning)
          │
┌─────────────────────────────────────────────────────────────┐
│ Registry (PyInterface::g_values)                            │
│ ─────────────────────────────────────────────────────────   │
│ Owned by: PyInterface static storage                        │
│ Lifetime: Program duration                                  │
│ Access: BoundValue metadata objects                         │
│ Pattern: Non-owning pointers to C++ data                    │
└─────────────────────────────────────────────────────────────┘
          ▲ Wrappers (owned by proxies) or copies
          │
┌─────────────────────────────────────────────────────────────┐
│ Python Proxies (SructProxy, VectorProxy, CppProxy)         │
│ ─────────────────────────────────────────────────────────   │
│ Owned by: Python garbage collector                          │
│ Lifetime: While Python code holds references                │
│ Access: Py_INCREF/Py_DECREF for reference management        │
│ Pattern: Wrapper copies for complex types, ref counting     │
└─────────────────────────────────────────────────────────────┘
          ▲ New/borrowed references
          │
┌─────────────────────────────────────────────────────────────┐
│ Python Objects (int, float, str, custom objects)           │
│ ─────────────────────────────────────────────────────────   │
│ Owned by: Python garbage collector                          │
│ Lifetime: While referenced by Python code                   │
│ Access: PyObject* with reference counting                   │
└─────────────────────────────────────────────────────────────┘
```

---

[Back to Table of Contents](#table-of-contents)


## Registry Ownership (g_values)

### Definition

```cpp
// In value_interface.hpp
class PyInterface {
public:
    static std::map<std::string, std::unique_ptr<BoundValue>> g_values;
    
    // Binding stores data with unique_ptr (owns the wrapper)
    static void bind(const std::string &name, T &value);
};

// Instantiation: g_values owns BoundValue pointers via unique_ptr
std::map<std::string, std::unique_ptr<BoundValue>> PyInterface::g_values;
```

### Ownership Characteristics

| Aspect | Detail |
|--------|--------|
| **Holder** | PyInterface static member |
| **Container** | std::map with unique_ptr values |
| **Ownership** | Registry OWNS BoundValue metadata objects |
| **Lifetime** | Entire program (never deleted) |
| **Non-Ownership** | Registry does NOT own C++ data being wrapped |
| **Access Pattern** | get_value_raw(), get_value(name) |
| **Reference Count** | Not applicable (non-Python C++) |

### What the Registry Owns

**Owns:**
- `BoundStruct` objects (metadata + non-owning pointers)
- `BoundVector` objects (metadata + non-owning pointers)
- `PyBoundInt`, `PyBoundFloat`, etc. (conversion functions)

**Does NOT own (borrowed pointers):**
- C++ variables themselves (`Player player; vector<Enemy> enemies;`)
- Application memory locations
- Any C++ dynamic allocations

### Example

```cpp
// In main.cpp
Player player = {100, 5.5f};  // Owned by main.cpp
vector<Enemy> enemies = {...};  // Owned by main.cpp

// When binding:
PyInterface::bind("player", player);
PyInterface::bind("enemies", enemies);

// What g_values now contains:
g_values["player"] = unique_ptr<BoundStruct>(
    new BoundStruct("player", &player, &PlayerInfo)  // Registry owns BoundStruct
)                                        // &player is borrowed pointer

g_values["enemies"] = unique_ptr<BoundVector>(
    new BoundVector("enemies", &enemies, &EnemyVectorInfo)  // Registry owns BoundVector
)                                      // &enemies is borrowed pointer

// When accessed from Python:
import cpp
player_proxy = cpp.player  // Creates NEW BoundStruct wrapper, NOT taking from g_values

// When Python proxy (player_proxy) gets deleted:
// ✅ Only proxy's wrapper is deleted, NOT the entry in g_values
// ✅ &player in main.cpp remains untouched

// When program exits:
// g_values entries are deleted (unique_ptr cleanup)
// But since BoundStruct doesn't own &player, no double-free
```

---

[Back to Table of Contents](#table-of-contents)


## Scalar Type Ownership

### Pattern: Copy-on-Access

Scalars (int, float, bool, string) use **copy-on-access** - no proxy needed, no shared ownership risks.

### Ownership Model

| Aspect | Detail |
|--------|--------|
| **Storage** | Owned by C++ code (stack, global, or dynamic) |
| **Registry Entry** | BoundValue metadata with non-owning pointer |
| **Access** | Registry → PyBoundValue::to_python() → NEW PyObject |
| **Python Object** | NEW Python object created each access |
| **Lifetime** | Python object discarded after use |
| **Reference Counting** | Standard Python refcount (created with refcount=1) |

### Data Flow

```
C++ Memory          Registry              Python
───────────         ────────              ──────
int x = 10          BoundInt*             
                    ├─ptr → &x            PyLong(10) ◄─ NEW object
                    └─to_python()         refcount=1
                       │
                       └─ PyLong_FromLong(*ptr)
                       
x = 10 (unchanged)   BoundInt unchanged   Python object eventually deleted
```

### Code Example

**C++ Side (main.cpp):**
```cpp
int health = 100;
PyInterface::bind("health", health);
```

**Python Side (controller.py):**
```python
import cpp

# Access 1: Creates new Python int
h1 = cpp.health  # PyLong(100), refcount=1
print(h1)        # Prints: 100
# h1 goes out of scope → Python deletes PyLong, refcount→0

# Access 2: Creates different Python int
h2 = cpp.health  # NEW PyLong(100), refcount=1
h1 == h2         # True (same value)
h1 is h2         # False (different objects!)
```

**Why Safe:**
- No proxy needed (no shared mutable state)
- No double-free risk (each access creates new Python object)
- No wrapper ownership complexity
- Simple copy semantics understood by everyone

---

[Back to Table of Contents](#table-of-contents)


## Complex Type Ownership (Structs & Vectors)

### Pattern: Wrapper-Based Ownership

Complex types (structs and vectors) use **wrapper copies** to separate proxy ownership from registry ownership and prevent double-frees.

### The Problem Without Wrappers

```cpp
// ❌ UNSAFE: Direct ownership
BoundStruct *from_registry = g_values["player"].get();
StructProxy_New(from_registry);  // Proxy thinks it owns this!

// When proxy is deleted by Python GC:
StructProxy_dealloc() {
    delete proxy->bound;  // ❌ Deletes g_values entry!
}

// Later when program exits:
// g_values tries to delete same BoundStruct
// ❌ DOUBLE-FREE CRASH!
```

### The Solution: Wrappers

```cpp
// ✅ SAFE: Wrapper pattern
BoundStruct *original = g_values["player"].get();
BoundStruct *wrapper = new BoundStruct(
    original->name,      // Copy metadata
    original->instance(),  // Keep same C++ pointer (&player)
    original->info()     // Copy info pointer
);
PyObject *proxy = StructProxy_New(wrapper);  // Proxy owns wrapper

// When proxy is deleted:
StructProxy_dealloc() {
    delete proxy->bound;  // ✅ Deletes wrapper only!
}

// When program exits:
// g_values deletes original safely (separate object)
// ✅ NO CRASH!
```

### Ownership Characteristics

| Aspect | Detail |
|--------|--------|
| **C++ Data** | Owned by C++ (main.cpp, stack, globals) |
| **Registry Entry** | Owned by PyInterface::g_values |
| **Proxy Wrapper** | Owned by Python proxy object |
| **Multiple Proxies** | Each proxy owns its own wrapper copy |
| **Reference Counting** | Parent proxies reference-counted for nested types |

### Complex Type Access Flow

```
Python: player = cpp.player

Step 1: Look up in registry
  g_values["player"] → unique_ptr<BoundStruct> (original)
  
Step 2: Create wrapper copy
  wrapper = new BoundStruct(
      name="player",
      instance=&player,    ◄─ Still points to original C++ data
      info=PlayerInfo
  )
  
Step 3: Create proxy
  StructProxyObject proxy {
      .bound = wrapper,    ◄─ Owns wrapper
      .parent_proxy = NULL
  }
  returns: PyObject* (refcount=1)
  
Step 4: Return to Python
  Python now owns: StructProxyObject
  g_values still owns: original BoundStruct
  Wrapper: owned by proxy
  C++ memory: Player player (owned by main.cpp)

Memory after access:
┌──────────────────┐
│ g_values         │ owns → original BoundStruct (metadata #1)
│ ["player"]       │
└──────────────────┘

┌──────────────────┐
│ Python proxy     │ owns → wrapper BoundStruct (metadata #2)
│ (StructProxy)    │       └─→ &player (borrowed pointer)
└──────────────────┘

Cleanup:
- Python proxy deleted → deletes wrapper #2 ✅
- Python exits → g_values deletes original #1 ✅
- NO DOUBLE-FREE!
```

### Wrapper vs Registry Entry

| Component | Registry Entry | Proxy Wrapper |
|-----------|---|---|
| Ownership | PyInterface::g_values | StructProxyObject |
| Lifetime | Program duration | While Python proxy exists |
| C++ data pointer | Same | Same (borrowed) |
| Metadata | Full copy | Full copy |
| Purpose | Registry lookup | Proxy operation |
| Deletion | g_values cleanup | Proxy dealloc |

---

[Back to Table of Contents](#table-of-contents)


## Wrapper Ownership Pattern

### Formal Definition

**The wrapper ownership pattern separates object lifetime into two independent hierarchies:**

1. **Registry Hierarchy:** Registry owns metadata wrappers (lifetime = program)
2. **Proxy Hierarchy:** Proxies own metadata wrappers (lifetime = Python GC)

**Key Invariant:** Same C++ data pointed to by non-owning pointers in both hierarchies.

### Proxy Object Definition

**StructProxyObject (python_proxy.cpp lines 222-226):**
```cpp
typedef struct {
    PyObject_HEAD
    BoundStruct *bound;        // Owns this wrapper
    PyObject *parent_proxy;     // Reference to parent for nested elements
} StructProxyObject;
```

**VectorProxyObject (python_proxy.cpp lines 472-476):**
```cpp
typedef struct {
    PyObject_HEAD
    BoundVector *bound;        // Owns this wrapper
    PyObject *parent_proxy;     // Reference to parent for nested vectors
} VectorProxyObject;
```

### Reference Counting Rules

**For proxy->bound (wrapper ownership):**
- Proxy constructor: Takes ownership (`new BoundStruct(...)`)
- Proxy dealloc: Releases ownership (`delete proxy->bound`)
- Python GC timing: Proxy lifetime = wrapper lifetime

**For parent_proxy (nested ownership):**
- Proxy constructor: `Py_XINCREF(parent)` (increments refcount)
- Proxy dealloc: `Py_XDECREF(parent)` (decrements refcount)
- Python GC timing: Parent must outlive child (refcount keeps it alive)

---

[Back to Table of Contents](#table-of-contents)


## Parent-Child Proxy Reference Management

### The Nesting Problem (Issue 48)

When accessing nested structures (vector elements, vector of vectors), child proxies need parent proxies to stay alive because they hold dynamic pointers.

#### Scenario: Vector of Structs

```cpp
// C++ (enemy_waves.cpp):
vector<vector<Enemy>> enemy_waves;
  ├─ [0] → vector<Enemy>   [100 enemies]
  ├─ [1] → vector<Enemy>   [145 enemies]
  └─ [2] → vector<Enemy>   [90 enemies]
  
Each Enemy struct:
  ├─ health: int
  ├─ x, y: float
  └─ name: string
```

#### Python Access:

```python
# Step 1: Get outer vector
waves = cpp.enemy_waves
  → Creates VectorProxyObject (line 1)
  → proxy.bound = BoundVector (enemy_waves metadata)

# Step 2: Get inner vector (element of outer)
wave = waves[0]
  → Creates VectorProxyObject (line 2)
  → proxy.bound = BoundVector (enemy_waves[0] metadata)
  → proxy.parent_proxy = waves (line 1's proxy)
  → Calls Py_INCREF on waves proxy
  → refcount(waves) increases: 1 → 2

# Step 3: Delete waves reference
del waves
  → waves refcount decreases: 2 → 1
  → waves proxy NOT deleted (wave still references it)
  ✅ wave's parent stays alive!

# Step 4: Access enemy
enemy = wave[0]
  → Creates StructProxyObject
  → proxy.bound = BoundStruct (wave[0] metadata)
  → proxy.parent_proxy = wave
  → Calls Py_INCREF on wave proxy
  → refcount(wave) increases: 1 → 2

# Step 5: All cleanup
del wave
  → wave refcount: 2 → 1
  → wave proxy NOT deleted
del enemy
  → enemy refcount: 1 → 0
  → enemy proxy deleted
  → Calls Py_DECREF on parent (wave)
  → wave refcount: 1 → 0
  → wave proxy deleted
  → Calls Py_DECREF on parent (waves)
  → waves refcount: 1 → 0
  → waves proxy deleted
```

### Reference Counting Pattern

**Creation (StructProxy_New, VectorProxy_New):**
```cpp
PyObject *StructProxy_New(BoundStruct *bound, PyObject *parent = nullptr)
{
    StructProxyObject *obj = PyObject_New(StructProxyObject, &StructProxyType);
    obj->bound = bound;
    obj->parent_proxy = parent;
    Py_XINCREF(parent); // ◄─ Increment if parent not nullptr
    return (PyObject *)obj;
}
```

**Destruction (StructProxy_dealloc):**
```cpp
static void StructProxy_dealloc(PyObject *self)
{
    StructProxyObject *proxy = (StructProxyObject *)self;
    delete proxy->bound;
    Py_XDECREF(proxy->parent_proxy); // ◄─ Decrement if parent exists
    PyObject_Del(self);
}
```

### Where Parent References Are Set

**In VectorProxy_getitem (accessing element):**
```cpp
// Line 547-552: Getting struct from vector
case ValueType::Struct:
{
    const StructInfo *sinfo = static_cast<const StructInfo *>(info->element_meta);
    BoundStruct *bstruct = new BoundStruct(field->name, ...);
    return StructProxy_New(bstruct, self); // ◄─ Pass parent proxy
}
```

**In VectorProxy_append_new:**
```cpp
// Line 709: Appending new struct to vector
BoundStruct *bstruct = new BoundStruct(vec->name, vec, last_idx, sinfo);
return StructProxy_New(bstruct, self); // ◄─ Pass parent vector proxy
```

**In VectorProxy_append_new_vector:**
```cpp
// Line 796: Appending new vector to vector
BoundVector *bvec = new BoundVector(vec->name, vec, last_idx, inner_info);
return VectorProxy_New(bvec, self); // ◄─ Pass parent vector proxy
```

---

[Back to Table of Contents](#table-of-contents)


## Python Reference Counting

### C-API Reference Semantics

All Python proxy functions follow strict reference counting rules:

### New vs Borrowed References

| Type | Definition | Action | Caller Responsibility |
|------|---|---|---|
| **New Ref** | PyObject* with refcount incremented | `Py_XINCREF` called or PyObject_New | **Must Py_DECREF** |
| **Borrowed Ref** | Temporary access to existing refcount | No increment | Don't Py_DECREF |

### Proxy Function Return Rules

**Functions that return NEW references (Caller must Py_DECREF):**
- `create_cpp_proxy()` - Creates CppProxy singleton
- `StructProxy_New(bound, parent)` - Creates StructProxy wrapper
- `VectorProxy_New(bound, parent)` - Creates VectorProxy wrapper
- `cppproxy_getattro(self, name)` - Returns struct/vector/scalar
- `StructProxy_getattro(self, name)` - Returns field value/proxy
- `VectorProxy_getitem(self, index)` - Returns element proxy
- All PyObject_New() calls for iterator objects

**Setter functions (no return value, but may set error):**
- `cppproxy_setattro(self, name, value)` - Returns 0 on success, -1 on error
- `StructProxy_setattro(self, name, value)` - Returns 0 on success, -1 on error
- `VectorProxy_setitem(self, index, value)` - Returns 0 on success, -1 on error

### Singleton Reference Counting (create_cpp_proxy)

**Fast path (returning existing singleton):**
```cpp
if (g_cpp_proxy_instance)
{
    Py_INCREF(g_cpp_proxy_instance);  // ◄─ New reference for caller
    return g_cpp_proxy_instance;       // Caller must Py_DECREF
}
```

**First creation path:**
```cpp
g_cpp_proxy_instance = PyObject_New(CppProxyObject, &CppProxyType);
// PyObject_New returns new reference (refcount=1)
// No additional INCREF needed
return g_cpp_proxy_instance;  // Caller must Py_DECREF
```

**Pattern explanation (Issue 39 documentation):**
- Both paths return **new references** to the caller
- Fast path uses explicit `Py_INCREF` for clarity
- First path implicit (PyObject_New already creates refcount=1)
- Asymmetry resolved by documentation

---

[Back to Table of Contents](#table-of-contents)


## Thread Safety & Singleton Management

### The Race Condition (Issue 34)

**Original unsafe code:**
```cpp
// ❌ RACE CONDITION
PyObject *create_cpp_proxy()
{
    if (g_cpp_proxy_instance)  // ◄─ Check OUTSIDE lock
    {
        Py_INCREF(g_cpp_proxy_instance);
        return g_cpp_proxy_instance;
    }
    
    // RACE: Multiple threads could reach here
    if (PyType_Ready(&CppProxyType) < 0)
        return nullptr;
    
    g_cpp_proxy_instance = PyObject_New(...);
    return g_cpp_proxy_instance;
}
```

**Race condition scenario:**
```
Thread 1:  if (g_cpp_proxy_instance)  // False
Thread 2:  if (g_cpp_proxy_instance)  // False
Thread 1:  PyType_Ready(...)  // ◄─ UB: called twice!
Thread 2:  PyType_Ready(...)
Thread 1:  g_cpp_proxy_instance = NEW INSTANCE A
Thread 2:  g_cpp_proxy_instance = NEW INSTANCE B  (overwrites!)
Result: INSTANCE A leaked, INSTANCE B unused
```

### Thread-Safe Implementation (Issue 34)

**Fixed code with mutex:**
```cpp
// ✅ THREAD SAFE
static std::mutex g_cpp_proxy_mutex;  // Protection lock

PyObject *create_cpp_proxy()
{
    std::lock_guard<std::mutex> lock(g_cpp_proxy_mutex);  // ◄─ Critical section
    
    // Check again inside lock (double-checked locking)
    if (g_cpp_proxy_instance)
    {
        Py_INCREF(g_cpp_proxy_instance);
        return g_cpp_proxy_instance;
    }
    
    // Only one thread reaches here
    if (PyType_Ready(&CppProxyType) < 0)
        return nullptr;
    
    g_cpp_proxy_instance = PyObject_New(...);
    if (!g_cpp_proxy_instance)
    {
        PyErr_NoMemory();
        return nullptr;
    }
    
    return g_cpp_proxy_instance;
}
```

### Ownership Under Multiple Threads

| Aspect | Detail |
|--------|--------|
| **Mutex** | `g_cpp_proxy_mutex` protects initialization |
| **Check Timing** | Once outside (optimization), once inside (safety) |
| **PyType_Ready** | Called exactly once per process |
| **Instance Creation** | Atomic: single assignment under lock |
| **Reference Count** | Standard Python refcount from each thread |
| **Destruction** | Python GC handles cleanup (single instance) |

---

[Back to Table of Contents](#table-of-contents)


## Ownership Decision Tree

**Use this tree to understand ownership for any proxy operation:**

```
Start: I'm accessing a C++ variable from Python
  │
  ├─ Is it a scalar (int, float, bool, string)?
  │  │
  │  ├─ YES:
  │  │  └─ Copy-on-Access Model
  │  │     • Registry: Non-owning pointer to C++ value
  │  │     • Python: NEW value created each access
  │  │     • No proxy needed
  │  │     • Each access creates new PyObject*
  │  │     • No shared ownership risks
  │  │
  │  └─ NO: Continue...
  │
  ├─ Is it a struct or vector?
  │  │
  │  ├─ YES:
  │  │  └─ Wrapper Ownership Model
  │  │     • Registry: unique_ptr<BoundValue> (owns metadata)
  │  │     • Proxy: proxy->bound = wrapper (owns wrapper)
  │  │     • Two independent BoundValue hierarchies
  │  │     • Same C++ pointer, different wrapper objects
  │  │     • Safe: no double-free from separate ownership
  │  │
  │  └─ NO: Error (unknown type)
  │
  ├─ Is this a nested structure?
  │  │  (vector element, vector of vectors)
  │  │
  │  ├─ YES:
  │  │  └─ Parent-Child Reference Counting
  │  │     • Child proxy holds PyObject* to parent
  │  │     • Py_INCREF on creation (increments parent refcount)
  │  │     • Py_DECREF on destruction (decrements parent refcount)
  │  │     • Parent stays alive while child exists
  │  │     • Safe: reference counting prevents use-after-free
  │  │
  │  └─ NO: Simple ownership above applies
  │
  └─ Implementation notes:
     • Scalar return: NEW PyObject*, caller Py_DECREF
     • Proxy return: NEW StructProxyObject*, caller Py_DECREF
     • Parent references: NEW PyObject_Head*, proxy manages via Py_XDECREF
```

---

[Back to Table of Contents](#table-of-contents)


## Summary: Ownership Models at a Glance

| Type | Storage | Registry | Proxy | Parent | Reference Count |
|------|---------|----------|-------|--------|---|
| **Scalar** | C++ (main) | BoundInt* (non-owning) | NEW PyLong | N/A | Standard Py refcount |
| **Struct** | C++ (main) | BoundStruct (non-owning) | wrapper+proxy | N/A if root | Proxy refcount |
| **Vector** | C++ (main) | BoundVector (non-owning) | wrapper+proxy | N/A if root | Proxy refcount |
| **Nested Struct** | C++ (vector) | BoundVector → Index | wrapper+proxy | parent_proxy | Py_XINCREF/DECREF |
| **Nested Vector** | C++ (vector) | BoundVector → Index | wrapper+proxy | parent_proxy | Py_XINCREF/DECREF |
| **Singleton (CppProxy)** | Python process | N/A | global instance | N/A | Thread-safe (mutex) |

---

[Back to Table of Contents](#table-of-contents)


## See Also

- **WRAPPER_OWNERSHIP_PATTERN.md** - Deep dive into wrapper pattern
- **SCALAR_VS_COMPLEX_OWNERSHIP.md** - Scalar vs complex comparison
- **PARENT_TRACKING_IMPLEMENTATION_GUIDE.md** - Dynamic element resolution
- **python_proxy.cpp** - Implementation details (lines 55-290 for singleton, 222-240 for proxy defs)

[Back to Table of Contents](#table-of-contents)

