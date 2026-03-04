# Python GC Requirements Analysis
## Additional Garbage Collection Issues Beyond Issue 51

**Date:** March 4, 2026  
**Analysis Focus:** Complete GC support needed for all proxy types

---

## Summary

Issue 51 covers adding GC support to **StructProxyType** and **VectorProxyType**, but there are **3 additional implementation details** and **1 additional type** that need GC support.

---

## Issues Found

### 1. StructProxyType - Incomplete GC Implementation

**Current State:**
```cpp
typedef struct {
    PyObject_HEAD 
    BoundStruct *bound;
    PyObject *parent_proxy;  // ⚠️ HOLDS REFERENCE - NEEDS GC!
} StructProxyObject;

// Current creation
PyObject *StructProxy_New(BoundStruct *bound, PyObject *parent) {
    StructProxyObject *obj = PyObject_New(StructProxyObject, &StructProxyType);  // ❌
    // ...
    obj->parent_proxy = parent;
    Py_XINCREF(parent);
    return (PyObject *)obj;
}
```

**Issue 51 Specifies:**
- ✅ Add `tp_traverse`
- ✅ Add `tp_clear`
- ✅ Add `Py_TPFLAGS_HAVE_GC` flag

**But MISSES:**
- ❌ **Use `PyObject_GC_New` instead of `PyObject_New`** (Line 538)
- ❌ **Call `PyObject_GC_Track()` in constructor**
- ❌ **Call `PyObject_GC_UnTrack()` in destructor** (Line 258)
- ❌ **Use `PyObject_GC_Del()` instead of `PyObject_Del` in destructor**

**Full Required Changes:**

```cpp
// Destructor - Updated
static void StructProxy_dealloc(PyObject *self) {
    StructProxyObject *proxy = (StructProxyObject *)self;
    
    PyObject_GC_UnTrack(self);      // 🔴 MISSING
    
    delete proxy->bound;
    Py_XDECREF(proxy->parent_proxy);
    
    PyObject_GC_Del(self);          // 🔴 MISSING (was PyObject_Del)
}

// Type definition - Updated
PyTypeObject StructProxyType = {
    // ... existing fields ...
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,  // 🔴 MISSING HAVE_GC
    "Proxy for C++ struct",
    StructProxy_traverse,                       // 🔴 MISSING (was 0)
    StructProxy_clear,                          // 🔴 MISSING (was 0)
};

// Constructor - Updated
PyObject *StructProxy_New(BoundStruct *bound, PyObject *parent) {
    StructProxyObject *obj = 
        PyObject_GC_New(StructProxyObject, &StructProxyType);  // 🔴 MISSING (was PyObject_New)
    
    if (!obj) {
        PyErr_NoMemory();
        return nullptr;
    }
    
    obj->bound = bound;
    obj->parent_proxy = parent;
    Py_XINCREF(parent);
    
    PyObject_GC_Track((PyObject *)obj);  // 🔴 MISSING
    
    return (PyObject *)obj;
}

// NEW: Traverse function
static int StructProxy_traverse(PyObject *self, visitproc visit, void *arg) {
    StructProxyObject *proxy = (StructProxyObject *)self;
    Py_VISIT(proxy->parent_proxy);
    return 0;
}

// NEW: Clear function
static int StructProxy_clear(PyObject *self) {
    StructProxyObject *proxy = (StructProxyObject *)self;
    Py_CLEAR(proxy->parent_proxy);
    return 0;
}
```

---

### 2. VectorProxyType - Incomplete GC Implementation

**Current State:**
```cpp
typedef struct {
    PyObject_HEAD 
    BoundVector *bound;
    PyObject *parent_proxy;  // ⚠️ HOLDS REFERENCE - NEEDS GC!
} VectorProxyObject;

// Current creation
PyObject *VectorProxy_New(BoundVector *bound, PyObject *parent) {
    VectorProxyObject *obj = PyObject_New(VectorProxyObject, &VectorProxyType);  // ❌
    // ...
    obj->parent_proxy = parent;
    Py_XINCREF(parent);
    return (PyObject *)obj;
}
```

**Same Issues as StructProxyType:**
1. ❌ Using `PyObject_New` instead of `PyObject_GC_New` (Line 1277)
2. ❌ Missing `PyObject_GC_Track()` call in constructor
3. ❌ Missing `PyObject_GC_UnTrack()` call in destructor (Line 762)
4. ❌ Using `PyObject_Del` instead of `PyObject_GC_Del` in destructor
5. ❌ `tp_traverse` currently set to `0` (should be function)
6. ❌ `tp_clear` currently set to `0` (should be function)
7. ❌ Missing `Py_TPFLAGS_HAVE_GC` flag in `tp_flags`

**Full Required Changes:** (Same pattern as StructProxyType)

```cpp
// Type definition
PyTypeObject VectorProxyType = {
    // ...
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,  // Changed from Py_TPFLAGS_DEFAULT
    "Proxy for C++ vector",
    VectorProxy_traverse,                       // Changed from 0
    VectorProxy_clear,                          // Changed from 0
    // ...
};

// Destructor - Updated
static void VectorProxy_dealloc(PyObject *self) {
    VectorProxyObject *proxy = (VectorProxyObject *)self;
    
    PyObject_GC_UnTrack(self);      // NEW
    
    delete proxy->bound;
    Py_XDECREF(proxy->parent_proxy);
    
    PyObject_GC_Del(self);          // Changed from PyObject_Del
}

// Constructor - Updated
PyObject *VectorProxy_New(BoundVector *bound, PyObject *parent) {
    VectorProxyObject *obj =
        PyObject_GC_New(VectorProxyObject, &VectorProxyType);  // Changed from PyObject_New
    
    if (!obj) {
        PyErr_NoMemory();
        return nullptr;
    }
    
    obj->bound = bound;
    obj->parent_proxy = parent;
    Py_XINCREF(parent);
    
    PyObject_GC_Track((PyObject *)obj);  // NEW
    
    return (PyObject *)obj;
}

// NEW: Traverse function
static int VectorProxy_traverse(PyObject *self, visitproc visit, void *arg) {
    VectorProxyObject *proxy = (VectorProxyObject *)self;
    Py_VISIT(proxy->parent_proxy);
    return 0;
}

// NEW: Clear function
static int VectorProxy_clear(PyObject *self) {
    VectorProxyObject *proxy = (VectorProxyObject *)self;
    Py_CLEAR(proxy->parent_proxy);
    return 0;
}
```

---

### 3. VectorIteratorType - **NOT COVERED IN ISSUE 51** ⚠️

**Critical Omission:** Issue 51 doesn't mention the iterator type, which **ALSO HOLDS A PyObject REFERENCE**.

**Current State:**
```cpp
typedef struct {
    PyObject_HEAD 
    PyObject *vector;    // ⚠️ HOLDS REFERENCE - NEEDS GC!
    std::size_t index;   // Current iteration index
} VectorIteratorObject;

// Current creation (Line 1198)
PyObject *VectorProxy_iter(PyObject *self) {
    VectorIteratorObject *it = PyObject_New(VectorIteratorObject, &VectorIteratorType);  // ❌
    if (!it)
        return nullptr;
    
    Py_INCREF(self);  // Hold reference to vector
    it->vector = self;
    it->index = 0;
    
    return (PyObject *)it;
}
```

**Issues:**
1. ❌ Using `PyObject_New` instead of `PyObject_GC_New` (Line 1198)
2. ❌ Missing `PyObject_GC_Track()` call after creation
3. ❌ Missing `PyObject_GC_UnTrack()` in destructor (Line 1123)
4. ❌ Using `PyObject_Del` instead of `PyObject_GC_Del` in destructor
5. ❌ `tp_traverse` is `0` instead of function (Line 1184)
6. ❌ `tp_clear` is `0` instead of function (Line 1185)
7. ❌ Missing `Py_TPFLAGS_HAVE_GC` flag (Line 1183)

**Full Required Changes:**

```cpp
// Type definition
PyTypeObject VectorIteratorType = {
    // ...
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,  // Changed from Py_TPFLAGS_DEFAULT
    "Iterator for C++ vector",
    VectorIterator_traverse,                    // Changed from 0
    VectorIterator_clear,                       // Changed from 0
    // ...
};

// Destructor - Updated
static void VectorIterator_dealloc(PyObject *self) {
    VectorIteratorObject *it = (VectorIteratorObject *)self;
    
    PyObject_GC_UnTrack(self);      // NEW
    
    Py_XDECREF(it->vector);
    
    PyObject_GC_Del(self);          // Changed from PyObject_Del
}

// Constructor - Updated
static PyObject *VectorProxy_iter(PyObject *self) {
    VectorIteratorObject *it = 
        PyObject_GC_New(VectorIteratorObject, &VectorIteratorType);  // Changed from PyObject_New
    
    if (!it)
        return nullptr;
    
    Py_INCREF(self);
    it->vector = self;
    it->index = 0;
    
    PyObject_GC_Track((PyObject *)it);  // NEW
    
    return (PyObject *)it;
}

// NEW: Traverse function
static int VectorIterator_traverse(PyObject *self, visitproc visit, void *arg) {
    VectorIteratorObject *it = (VectorIteratorObject *)self;
    Py_VISIT(it->vector);
    return 0;
}

// NEW: Clear function
static int VectorIterator_clear(PyObject *self) {
    VectorIteratorObject *it = (VectorIteratorObject *)self;
    Py_CLEAR(it->vector);
    return 0;
}
```

---

### 4. CppProxyType - No GC Issues ✅

**Analysis:**
```cpp
typedef struct {
    PyObject_HEAD
    // ✅ NO PYOBJECT* FIELDS - NO GC ISSUES
} CppProxyObject;
```

- No Python object references
- No circular reference risk
- No GC support needed

---

## Summary of All Changes Required

| Type | GC_New | Track | tp_traverse | tp_clear | GC Flag | Clear Func |
|------|--------|-------|-------------|----------|---------|-----------|
| **StructProxy** | ✅ Add | ✅ Add | ✅ Implement | ✅ Implement | ✅ Add | ✅ New |
| **VectorProxy** | ✅ Add | ✅ Add | ✅ Implement | ✅ Implement | ✅ Add | ✅ New |
| **VectorIterator** | ✅ Add | ✅ Add | ✅ Implement | ✅ Implement | ✅ Add | ✅ New |
| **CppProxy** | ❌ None | ❌ None | ❌ None | ❌ None | ❌ None | ❌ None |

---

## Known Cycle Scenarios

These cycles will be **automatically broken by GC implementation**:

### Scenario 1: Parent-Child Cycle
```python
struct_proxy.field_vector  # Creates VectorProxy with parent_proxy -> StructProxy
                          # StructProxy holds BoundStruct which holds field data
                          # When both go out of scope, GC will collect them
```

### Scenario 2: Deep Nesting Cycle
```python
for enemy_wave in cpp.waves:           # Creates VectorProxy(_wave)
    for enemy in enemy_wave:            # Creates VectorProxy(_enemy) with parent
        print(enemy.health)
        # Multiple nested proxies create reference chains
        # GC handles cleanup when iterations end
```

### Scenario 3: Iterator Cycle
```python
for enemy in cpp.enemies:  # Creates VectorIterator
    # Iterator holds reference to VectorProxy (cpp.enemies)
    # VectorProxy holds reference to parent CppProxy (if applicable)
    # If iteration is nested, creates cycles
    pass
    # GC cleans up all at once
```

---

## Implementation Priority

### Must Fix (For Issue 51 Completeness)
1. **StructProxyType** - Add GC-aware allocation, tracking, and functions
2. **VectorProxyType** - Add GC-aware allocation, tracking, and functions

### Additional (Not in Issue 51)
3. **VectorIteratorType** - Add GC support (same pattern)

### Optional
- Add `object.__repr__()` for debugging (helpful for GC inspection)
- Add `weakref.ref()` support with `tp_weaklistoffset` (allows external weak references)

---

## Testing GC Implementation

After implementing all three types:

```python
import gc
import weakref

def test_gc_collection():
    # Test 1: Simple cycle cleanup
    cpp.enemies.append(new_enemy)
    proxy = cpp.enemies[0]
    
    # Create a weak reference to track garbage collection
    weak_ref = weakref.ref(proxy)
    del proxy
    gc.collect()
    
    # Should be collected if GC working
    assert weak_ref() is None, "GC failed to collect proxy"
    
def test_nested_cycle():
    # Test 2: Deep nesting
    enemies = []
    for i in range(100):
        e = cpp.enemies.append_new()
        enemies.append(e)  # Hold references
    
    del enemies
    gc.collect()
    
    # Memory should be released

def test_iterator_cycle():
    # Test 3: Iterator cleanup
    def iterate():
        for enemy in cpp.enemies:
            for weapon in enemy.weapons:
                pass
    
    iterate()
    gc.collect()
    
    # All temporary iterators should be freed
```

---

## Notes for Implementation

1. **Order Matters:** Implement function pointers BEFORE updating type definitions
2. **Backward Compat:** `PyObject_GC_New` and `PyObject_New` are compatible (both return `PyObject*`)
3. **Test First:** Add GC tests before/after to verify collections are broken
4. **Documentation:** Update comments to explain GC cycle scenarios

---

**Recommendation:** Implement all three types together to ensure consistent GC behavior across the entire proxy system. VectorIteratorType is particularly important since iterators are frequently created in loops.
