# Ownership Models: Scalars vs Structs vs Vectors

## Overview

Issue #18 (Double-Free Risk in Root Proxy Attribute Access) revealed a critical vulnerability in how complex types (structs and vectors) managed ownership between the reflection registry and Python proxies. Scalar types, by contrast, were never vulnerable. This document explains why, the ownership differences, and how each type safely manages its lifecycle.

**Key Finding:** Scalars are inherently safe because they are copied at access time. Structs and vectors required a wrapper ownership pattern to eliminate shared ownership risks.

---

## Part 1: Ownership Fundamentals

### The Central Problem

**The Registry (`PyInterface::g_values`)** stores bound C++ variables:
```cpp
std::map<std::string, BoundValue*> PyInterface::g_values;

// When binding:
PyInterface::bind("player", player);
// Result: g_values["player"] = BoundStruct* (holds &player metadata)

PyInterface::bind("scores", scores);
// Result: g_values["scores"] = BoundVector* (holds &scores metadata)

int x = 10;
PyInterface::bind("x", x);
// Result: g_values["x"] = PyBoundInt* (holds &x metadata)
```

**The Access Path** converts registry entries to Python objects:
```python
import cpp
player = cpp.player  # Triggers getattr → creates proxy?
scores = cpp.scores  # Triggers getattr → creates proxy?
x = cpp.x            # Triggers getattr → creates Python value
```

**The Ownership Question:** Who owns the wrapper (BoundStruct*, BoundVector*) after access?

---

## Part 2: Scalar Types (int, float, bool, string)

### Design: Copy-on-Access

**Declaration:**
```cpp
struct PyBoundInt : PyBoundValue {
    int *ptr;  // Points to actual int variable
    
    PyObject *to_python() override { 
        return PyLong_FromLong(*ptr);  // ← Creates NEW Python object
    }
};

struct PyBoundFloat : PyBoundValue {
    float *ptr;  // Points to actual float variable
    
    PyObject *to_python() override { 
        return PyFloat_FromDouble(*ptr);  // ← Creates NEW Python object
    }
};

struct PyBoundString : PyBoundValue {
    std::string *ptr;  // Points to actual string variable
    
    PyObject *to_python() override { 
        return PyUnicode_FromString(ptr->c_str());  // ← Creates NEW Python object
    }
};
```

### Access Flow

```
Python: x = cpp.x

Step 1: Look up in registry
  g_values["x"] → PyBoundInt* pyval
  
Step 2: Convert to Python
  pyval->to_python()
  ↓
  PyLong_FromLong(*ptr)
  ↓
  Creates NEW PyObject (PyLong with value 10)
  
Step 3: Return to Python
  Python now owns: PyLong object
  G_values still owns: PyBoundInt* (metadata wrapper)
  C++ memory: int x (owned by main.cpp)

Memory ownership after access:
┌─────────────────┐
│ g_values["x"]   │ ─→ PyBoundInt* ─→ int x (in main.cpp)
│ (registry)      │    (metadata)     (original value)
└─────────────────┘
         
Python local scope:
┌─────────────────┐
│ x = PyLong(10)  │ ← NEW object, independent copy
│ (Python owns)   │
└─────────────────┘

Cleanup:
  Python: del x        ← Deletes PyLong(10)
  C++ end: delete pyval (in registry) ← Deletes metadata
  C++) end: int x destroyed ← User scope ends
```

### Key Safety Property

**Scalars create independent copies at access time:**

```cpp
// Access multiple times
int value = 10;
PyInterface::bind("value", value);

// Python:
a = cpp.value  # ← Creates PyLong(10) #1
b = cpp.value  # ← Creates PyLong(10) #2
c = cpp.value  # ← Creates PyLong(10) #3

# Each is independent:
del a          # Deletes PyLong #1, no effect on b or c
del b          # Deletes PyLong #2, no effect on a or c
del c          # Deletes PyLong #3, no effect on a or b

# g_values["value"] still intact, pointing to original int
```

### Why Scalars Are Safe from Issue #18

**Issue #18 Problem:** Shared ownership between registry and proxy

**Scalars solution:** No proxy involved
- No wrapper object returned
- No shared pointers
- Python gets independent value copy
- Registry ownership unchanged
- **Result:** Issue #18 is not applicable

---

## Part 3: Struct Types

### Design Before Fix: Shared Ownership (VULNERABLE)

**Before Issue #18 fix:**
```cpp
// python_proxy.cpp - OLD CODE (vulnerable)
case ValueType::Struct:
    return StructProxy_New(static_cast<BoundStruct *>(val));
    //                    ↑ Passing g_values' pointer directly!
```

**Memory ownership (BEFORE FIX):**
```
┌──────────────────┐
│ g_values["player"]│ ── owns ──→ BoundStruct*
└──────────────────┘                  ↓
                              ┌─────────────────┐
                              │ StructProxy #1  │
Python: p1 = cpp.player       │ bound = same*   │ ← Same pointer!
                              └─────────────────┘
                              
                              ┌─────────────────┐
Python: p2 = cpp.player       │ StructProxy #2  │
                              │ bound = same*   │ ← Same pointer again!
                              └─────────────────┘

Cleanup:
  Python: del p1  ← Calls StructProxy_dealloc()
                    └─→ delete bound;  ← DELETES BoundStruct*!
  
  Python: del p2  ← Calls StructProxy_dealloc()
                    └─→ delete bound;  ← DOUBLE-FREE! ❌
```

**The Disaster Sequence:**

```python
p1 = cpp.player         # StructProxy #1 created, bound = g_values["player"]
p2 = cpp.player         # StructProxy #2 created, bound = g_values["player"] (SAME)

# Proxies are identical objects in memory:
print(id(p1.bound) == id(p2.bound))  # True - THEY SHARE!

del p1                  # Calls destructor:
                        #   delete p1.bound  ← DELETES g_values["player"]!
                        #   g_values["player"] now dangling!

del p2                  # Calls destructor:
                        #   delete p2.bound  ← DOUBLE-FREE! ❌
                        #   Memory corruption!
```

### Design After Fix: Wrapper Ownership (SAFE)

**After Issue #18 fix:**
```cpp
// cpp_module.cpp - NEW CODE (safe)
case ValueType::Struct:
{
    auto *bs = static_cast<BoundStruct *>(val);
    // Create a wrapper that the proxy can own and delete safely
    BoundStruct *wrapper = new BoundStruct(bs->name, bs->instance(), bs->info());
    //                    ↑ COPY constructor - new object!
    return StructProxy_New(wrapper);
}
```

**Memory ownership (AFTER FIX - Wrapper Ownership Pattern):**
```
Registry (central, never deleted during proxy access):
┌──────────────────┐
│ g_values["player"]│ ── owns ──→ BoundStruct* (master)
└──────────────────┘              │
                                  ├─→ void *m_instance (→ Player in main.cpp)
                                  └─→ StructInfo* m_info (metadata)

Python Proxy #1:
┌──────────────────┐
│ StructProxy #1   │ ── owns ──→ BoundStruct* (COPY)
└──────────────────┘             │
                                 ├─→ void *m_instance (→ Player in main.cpp)
                                 └─→ StructInfo* m_info (metadata)

Python Proxy #2:
┌──────────────────┐
│ StructProxy #2   │ ── owns ──→ BoundStruct* (DIFFERENT COPY)
└──────────────────┘             │
                                 ├─→ void *m_instance (→ Player in main.cpp)
                                 └─→ StructInfo* m_info (metadata)

All wrappers point to SAME C++ data but are DIFFERENT objects
```

**The Safe Sequence:**

```python
p1 = cpp.player         # StructProxy #1 created, bound = COPY of g_values["player"]
p2 = cpp.player         # StructProxy #2 created, bound = DIFFERENT COPY

# Proxies have different wrapper objects:
print(id(p1.bound) == id(p2.bound))  # False - DIFFERENT OBJECTS

del p1                  # Calls destructor:
                        #   delete p1.bound  ← DELETES WRAPPER COPY #1
                        #   g_values["player"] UNAFFECTED ✓
                        
del p2                  # Calls destructor:
                        #   delete p2.bound  ← DELETES WRAPPER COPY #2
                        #   g_values["player"] UNAFFECTED ✓
                        #   No double-free! ✓
```

### Wrapper Ownership Pattern Details

**The Copy Constructor:**
```cpp
class BoundStruct {
    void *m_instance;          // Pointer to actual struct
    const StructInfo *m_info;  // Pointer to metadata
    BoundVector *m_parent_vector = nullptr;  // Parent for vector elements
    std::size_t m_parent_index = 0;
    
public:
    // Constructor for new wrapper
    BoundStruct(const std::string &name, void *inst, const StructInfo *info)
        : m_instance(inst), m_info(info) {}
        
    // Copy constructor (creates independent wrapper)
    BoundStruct(const BoundStruct &other)
        : m_instance(other.m_instance),      // Shallow copy (both point to same data)
          m_info(other.m_info),              // Shallow copy (metadata)
          m_parent_vector(other.m_parent_vector),
          m_parent_index(other.m_parent_index) {}
};
```

**Why Shallow Copy of Pointers Works:**
- `m_instance`: Points to C++ object (Player, Team, etc.) owned by main.cpp
- `m_info`: Pointer to static metadata (never deleted)
- `m_parent_vector`: Pointer to parent container (owned by registry)
- Shallow copying these pointers is safe - we're not transferring ownership

**Result:**
- Each proxy owns its wrapper copy (can delete it)
- All wrappers point to same underlying C++ data
- No double-free possible

---

## Part 4: Vector Types

### Design Before Fix: Shared Ownership (VULNERABLE)

**Before Issue #18 fix:**
```cpp
// python_proxy.cpp - OLD CODE (vulnerable)
case ValueType::Vector:
    return VectorProxy_New(static_cast<BoundVector *>(val));
    //                    ↑ Passing g_values' pointer directly!
```

**Exact same vulnerability as structs:**
```
g_values["enemies"] ─→ BoundVector* (master)
                            ↓
  ┌──────────────────┐  ┌──────────────────┐
  │ VectorProxy #1   │  │ VectorProxy #2   │
  │ bound = same*    │  │ bound = same*    │ ← SHARED!
  └──────────────────┘  └──────────────────┘

When proxies are deleted:
  delete proxy1.bound  ← Deletes g_values["enemies"]
  delete proxy2.bound  ← DOUBLE-FREE ❌
```

### Design After Fix: Wrapper Ownership (SAFE)

**After Issue #18 fix:**
```cpp
// cpp_module.cpp - NEW CODE (safe)
case ValueType::Vector:
{
    auto *bv = static_cast<BoundVector *>(val);
    // Create a wrapper that the proxy can own and delete safely
    BoundVector *wrapper = new BoundVector(bv->name, bv->raw_vector(), bv->info());
    //                    ↑ COPY constructor - new object!
    return VectorProxy_New(wrapper);
}
```

**Memory ownership (AFTER FIX):**
```
Registry:
┌──────────────────┐
│ g_values["enemies"]│ ─→ BoundVector* (master)
└──────────────────┘     │
                         ├─→ void *m_vec_ptr (→ std::vector<Enemy>)
                         └─→ VectorInfo* m_info (element metadata)

Python Proxy #1:
┌──────────────────┐
│ VectorProxy #1   │ ─→ BoundVector* (COPY)
└──────────────────┘    └─→ Points to same vector

Python Proxy #2:
┌──────────────────┐
│ VectorProxy #2   │ ─→ BoundVector* (DIFFERENT COPY)
└──────────────────┘    └─→ Points to same vector
```

**Safe vector access:**
```python
v1 = cpp.enemies              # VectorProxy #1, wrapper copy
v2 = cpp.enemies              # VectorProxy #2, different wrapper copy

for enemy in v1:              # Iterates using proxy #1's wrapper
    pass

del v1                        # Safe - deletes wrapper copy #1
del v2                        # Safe - deletes wrapper copy #2
v1 = cpp.enemies              # Safe - creates new wrapper copy
```

### Vector-Specific Ownership: Parent Tracking (Issue #26)

Vectors introduce an additional ownership pattern for element proxies:

**Without parent tracking (UNSAFE):**
```cpp
// OLD - stores raw pointer to element
void *elem_ptr = &vector[0];
StructProxy *elem_proxy = create_proxy(elem_ptr);

// Later: vector reallocates (different memory location)
vector.push_back(new_elem);

// Now elem_proxy points to FREED MEMORY
```

**With parent tracking (SAFE - Issue #26 fix):**
```cpp
// NEW - stores parent container + index
struct BoundStruct {
    BoundVector *m_parent_vector = nullptr;
    std::size_t m_parent_index = 0;
    
    void *get_instance_ptr() {
        if (m_parent_vector != nullptr) {
            // Dynamic resolution: always gets current pointer
            return m_parent_vector->element_ptr(m_parent_index);
        }
        return m_instance;
    }
};

// Now after reallocation:
elem_proxy->get_instance_ptr()  // ← Resolves fresh pointer
```

**Ownership model:**
- Element proxy owns wrapper copy (Issue #18 safety)
- Wrapper stores parent reference + index (Issue #26 safety)
- Parent container (vector) owned by registry/main.cpp
- Safe after vector reallocation

---

## Part 5: Comparison Matrix

### Ownership Model Summary

| Aspect | Scalars | Structs | Vectors |
|--------|---------|---------|---------|
| **What's returned** | New Python value (PyLong, PyFloat, etc.) | Proxy object | Proxy object |
| **Wrapper** | Metadata only (PyBoundInt*) | BoundStruct* | BoundVector* |
| **Wrapper shared?** | N/A - no proxy | Before fix: Yes (VULNERABLE) | Before fix: Yes (VULNERABLE) |
| | | After fix: No (wrapper copy) | After fix: No (wrapper copy) |
| **Proxy owns wrapper?** | N/A | Before fix: Shared (BAD) | Before fix: Shared (BAD) |
| | | After fix: Copy (GOOD) | After fix: Copy (GOOD) |
| **Issue #18 risk** | ✓ Safe (no proxy) | ❌ Before fix | ❌ Before fix |
| | | ✅ After fix | ✅ After fix |
| **Issue #26 risk** | ✓ N/A | ✓ N/A | ❌ Before fix |
| | | | ✅ After fix |
| **Memory cost** | None (values copied) | 8-16 bytes per access | 8-16 bytes per access |
| **Access speed** | O(1) conversion | O(1) wrapper copy | O(1) wrapper copy |

### Code Path Comparison

**Scalars - cpp_module_getattr():**
```cpp
case ValueType::Int:
    PyBoundValue *pyval = dynamic_cast<PyBoundValue *>(val);
    return pyval->to_python();  // ← Returns NEW Python object
    
// Result: Python owns independent value
// Result: G_values unchanged (still has metadata wrapper)
```

**Structs - cpp_module_getattr():**
```cpp
case ValueType::Struct:
    auto *bs = static_cast<BoundStruct *>(val);
    BoundStruct *wrapper = new BoundStruct(*bs);  // ← Creates COPY
    return StructProxy_New(wrapper);  // ← Proxy owns copy
    
// Result: Proxy owns wrapper copy
// Result: G_values still has master wrapper
// Result: No shared ownership
```

**Vectors - cpp_module_getattr():**
```cpp
case ValueType::Vector:
    auto *bv = static_cast<BoundVector *>(val);
    BoundVector *wrapper = new BoundVector(*bv);  // ← Creates COPY
    return VectorProxy_New(wrapper);  // ← Proxy owns copy
    
// Result: Proxy owns wrapper copy
// Result: G_values still has master wrapper
// Result: Element access uses parent tracking (safe after reallocation)
```

---

## Part 6: Access Flow Diagrams

### Scalar Access Flow

```
Python: x = cpp.x

┌─────────────────────────────────────┐
│ cpp_module_getattr("x")             │
└─────────────────────────────────────┘
            ↓
┌─────────────────────────────────────┐
│ BoundValue *val = g_values["x"];    │
│ type = ValueType::Int               │
└─────────────────────────────────────┘
            ↓
┌─────────────────────────────────────┐
│ case ValueType::Int:                │
│   PyBoundValue *pyval = val;        │
│   return pyval->to_python();        │
└─────────────────────────────────────┘
            ↓
┌─────────────────────────────────────┐
│ PyBoundInt::to_python()             │
│   return PyLong_FromLong(*ptr);     │
│   ↓ Creates NEW PyLong(10)          │
└─────────────────────────────────────┘
            ↓
┌─────────────────────────────────────┐
│ Python receives PyLong object       │
│ Python x = PyLong(10)               │
│ ✓ g_values["x"] unchanged           │
└─────────────────────────────────────┘
```

### Struct Access Flow (After Fix)

```
Python: p = cpp.player

┌─────────────────────────────────────┐
│ cpp_module_getattr("player")        │
└─────────────────────────────────────┘
            ↓
┌─────────────────────────────────────┐
│ BoundValue *val = g_values["player"];
│ type = ValueType::Struct            │
└─────────────────────────────────────┘
            ↓
┌─────────────────────────────────────┐
│ case ValueType::Struct:             │
│   BoundStruct *bs = val;            │
│   BoundStruct *wrapper =            │
│      new BoundStruct(*bs);  ← COPY  │
│   return StructProxy_New(wrapper);  │
└─────────────────────────────────────┘
            ↓
┌─────────────────────────────────────┐
│ StructProxy created                 │
│   proxy->bound = wrapper (owns it)   │
│   ✓ g_values["player"] unchanged    │
│   ✓ wrapper is independent copy     │
└─────────────────────────────────────┘
            ↓
┌─────────────────────────────────────┐
│ Python receives StructProxy object  │
│ Python p = <StructProxy at 0x...>   │
└─────────────────────────────────────┘
```

### Vector Access Flow (After Fix)

```
Python: v = cpp.enemies

┌─────────────────────────────────────┐
│ cpp_module_getattr("enemies")       │
└─────────────────────────────────────┘
            ↓
┌─────────────────────────────────────┐
│ BoundValue *val = g_values["enemies"];
│ type = ValueType::Vector            │
└─────────────────────────────────────┘
            ↓
┌─────────────────────────────────────┐
│ case ValueType::Vector:             │
│   BoundVector *bv = val;            │
│   BoundVector *wrapper =            │
│      new BoundVector(*bv);  ← COPY  │
│   return VectorProxy_New(wrapper);  │
└─────────────────────────────────────┘
            ↓
┌─────────────────────────────────────┐
│ VectorProxy created                 │
│   proxy->bound = wrapper (owns it)   │
│   ✓ g_values["enemies"] unchanged   │
│   ✓ wrapper points to same vector   │
└─────────────────────────────────────┘
            ↓
┌─────────────────────────────────────┐
│ Python receives VectorProxy object  │
│ Python v = <VectorProxy at 0x...>   │
└─────────────────────────────────────┘
```

---

## Part 7: Practical Examples

### Example 1: Multiple Accesses

**Scalars - Safe (independent copies):**
```python
cpp.x = 10  # In C++

a = cpp.x   # ← PyLong(10) #1
b = cpp.x   # ← PyLong(10) #2
c = cpp.x   # ← PyLong(10) #3

del a, b    # Deletes PyLong #1 and #2
print(c)    # ← 10 (still valid)

# Modify in C++:
cpp.x = 20  # C++ variable changed

# Python didn't update (already copied):
print(c)    # ← Still 10 (old copy)

# But new access gets new value:
d = cpp.x   # ← PyLong(20)
```

**Structs - Safe (wrapper copies):**
```python
cpp.team = Team(...)  # In C++

p1 = cpp.player  # ← StructProxy #1, owns wrapper copy #1
p2 = cpp.player  # ← StructProxy #2, owns wrapper copy #2

del p1, p2       # Deletes wrapper copies (safe, no double-free)

# Later access:
p3 = cpp.player  # ← StructProxy #3, owns new wrapper copy

# Modify field:
p3.health = 100  # ← Modifies C++ object directly

# Other references also see change:
p4 = cpp.player
print(p4.health) # ← 100 (all proxies access same C++ data)
```

### Example 2: Vector with Reallocation

**Vectors - Safe (parent tracking):**
```python
cpp.enemies = [Enemy(100, "Alice"), Enemy(80, "Bob")]

e1 = cpp.enemies[0]  # ← StructProxy with parent=enemies, index=0

print(e1.health)     # ← 100 (fresh pointer resolved)

# Reallocation:
cpp.enemies.append_new()  # Vector reallocates!
cpp.enemies.append_new()
cpp.enemies.append_new()

# e1 still valid due to parent tracking:
print(e1.health)     # ✓ Still 100 (fresh pointer from parent)

e1.health = 150      # ✓ Modifies correct memory location

# New access also works:
e2 = cpp.enemies[0]
print(e2.health)     # ✓ 150 (both proxies see same data)
```

---

## Part 8: Root Cause of Issue #18

### What Made Issue #18 Possible

1. **Structs and vectors returned proxy objects** (not value copies)
2. **Proxies managed wrapper lifecycle** (called delete in destructor)
3. **Wrappers were shared with registry** (same pointer in g_values)
4. **Python could create multiple proxies** (each thought it owned the wrapper)
5. **First proxy deletion deleted the registry entry** (dangling pointer in g_values)
6. **Second proxy deletion caused double-free** (deleting already-freed memory)

### Why Scalars Escaped This

Scalars never had steps 1-3:
- ✗ Don't return proxies (return Python values directly)
- ✗ Don't manage wrapper lifecycle (values don't have proxies)
- ✗ Don't share ownership (Python values independent from registry)

Result: Issue #18 literally can't occur for scalars.

### How the Fix Prevents It

The wrapper ownership pattern added:
1. **Each proxy gets its own wrapper copy** (new BoundStruct/BoundVector)
2. **No more shared pointers** between proxy and registry
3. **Independent ownership** - each proxy can safely delete its copy
4. **Registry remains unaffected** by proxy lifecycle

---

## Part 9: Memory Safety Summary

### The Three-Tier Safety Model

| Tier | Scalars | Structs | Vectors |
|------|---------|---------|---------|
| **Safety Mechanism** | Value copy | Wrapper copy | Wrapper copy + Parent tracking |
| **Issue #18 Protected** | N/A | ✅ Wrapper copy | ✅ Wrapper copy |
| **Issue #26 Protected** | N/A | N/A | ✅ Parent tracking |
| **Double-free risk** | None (no proxy) | Eliminated (wrapper copy) | Eliminated (wrapper copy) |
| **Use-after-free risk** | None (value copy) | None (wrapper points to valid C++) | Eliminated (parent tracking) |
| **Memory cost** | Negligible | 8-16 bytes/proxy | 16-24 bytes/proxy |

### Key Invariants Maintained

1. **Scalars:** Python value = copy of C++ value at access time
2. **Structs:** Proxy wrapper ≠ registry wrapper, but both point to same C++ struct
3. **Vectors:** Proxy wrapper ≠ registry wrapper, wrapper tracks parent for safe reallocation
4. **Registry:** Remains source of truth, unchanged during Python access
5. **Cleanup:** Each proxy cleanly deletes only what it owns

---

## Conclusion

**Scalars are inherently safe** because they create independent value copies at access time, eliminating any shared ownership concerns.

**Structs and vectors are now safe** through the wrapper ownership pattern (Issue #18 fix) combined with parent tracking for vectors (Issue #26 fix).

**The design principle:** If you don't share ownership, you can't have double-free. If you don't store raw pointers to container elements, you can't have use-after-free.

This architecture demonstrates how careful ownership management can eliminate entire classes of memory corruption bugs while maintaining zero-copy access to underlying C++ data.
