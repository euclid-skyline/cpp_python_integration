# Wrapper Ownership Pattern: Non-Owning Pointers in Python Proxies

## Table of Contents

- [Overview](#overview)
- [The Problem We Solve](#the-problem-we-solve)
- [The Solution: Wrapper Copies](#the-solution-wrapper-copies)
- [Architecture Diagram](#architecture-diagram)
- [Code Example: Wrapper Creation Pattern](#code-example-wrapper-creation-pattern)
  - [Safe Pattern (Both Paths Use This)](#safe-pattern-both-paths-use-this)
- [Ownership Timeline](#ownership-timeline)
  - [Scenario: Python code accesses multiple proxies](#scenario-python-code-accesses-multiple-proxies)
- [Key Design Principles](#key-design-principles)
- [Why This Matters](#why-this-matters)
  - [Without wrapper pattern (Issue 18 before fix)](#without-wrapper-pattern-issue-18-before-fix)
  - [With wrapper pattern (After fix)](#with-wrapper-pattern-after-fix)
- [Related Issues](#related-issues)
- [Best Practices](#best-practices)
- [See Also](#see-also)

## Overview

This document explains the **wrapper ownership pattern** used to safely bridge C++ data ownership with Python proxy lifetime management.

**Key Principle:** Python proxies must never directly own C++ objects that are managed by C++. Instead, proxies own **lightweight wrapper copies** that hold non-owning pointers to the C++ data.

---

[Back to Table of Contents](#table-of-contents)


## The Problem We Solve

Without the wrapper pattern, we get **double-free/use-after-free** crashes:

```cpp
// ❌ UNSAFE: Proxy owns g_values entry directly
BoundStruct *val = g_values["player"].get();  // Raw pointer to g_values entry
StructProxy_New(val);  // Proxy thinks it owns this!

// When proxy is GC'd:
StructProxy_dealloc() {
    delete proxy->bound;  // ❌ Deletes g_values entry!
}

// Later: g_values tries to delete same object
// ❌ DOUBLE-FREE CRASH!
```

---

[Back to Table of Contents](#table-of-contents)


## The Solution: Wrapper Copies

```cpp
// ✅ SAFE: Proxy owns wrapper copy only
BoundStruct *original = g_values["player"].get();  // From g_values
BoundStruct *wrapper = new BoundStruct(
    original->name,
    original->instance(),  // Same C++ pointer, but...
    original->info()       // ...wrapper is separate object
);
StructProxy_New(wrapper);  // Proxy owns wrapper

// When proxy is GC'd:
StructProxy_dealloc() {
    delete proxy->bound;  // ✅ Deletes wrapper only!
}

// Later: g_values deletes original safely
// ✅ NO CRASH - separate objects
```

---

[Back to Table of Contents](#table-of-contents)


## Architecture Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│ C++ Heap (main.cpp)                                              │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Player player = {100, 5.5f};    ◄─── Owned by C++ code          │
│  vector<Enemy> enemies = {...};  ◄─── Stack/global lifetime      │
│  vector<vector<int>> grid = {...};                               │
│                                                                  │
│ (These objects live as long as the program runs)                 │
└──────────────────────────────────────────────────────────────────┘
                              ▲
                              │ Raw pointers to C++ data
                              │
┌─────────────────────────────┴────────────────────────────────────┐
│ PyInterface::g_values() (Singleton Registry, Program Lifetime)   │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│ "player" → unique_ptr<BoundStruct>                               │
│    ├─ name: "player"                                             │
│    ├─ m_instance: (pointer to &player C++ object) ────┐          │
│    └─ m_info: PlayerInfo metadata                     │          │
│                                                       │          │
│ "enemies" → unique_ptr<BoundVector>                   │          │
│    ├─ name: "enemies"                                 │          │
│    ├─ m_vec_ptr: (pointer to &enemies C++ object)─  ──┼─┐        │
│    └─ m_info: VectorInfo metadata                     │ │        │
│                                                       │ │        │
│ (Owned by unique_ptr for entire program)              │ │        │
└───────────────────────────────────┬─────────────────  ┼─┼────────┘
                                    │                   │ │
                  ┌─────────────────┼─┐     ┌───────────┘ │
                  │                 │ │     │             │
                  │ Wrapper Policy: │ │     │             │
                  │ Copy the        │ │     │             │
                  │ BoundValue,     │ │     │             │
                  │ NOT direct      │ │     │             │
                  │ pointer         │ │     │             │
                  │                 │ │     │             │
    ┌─────────────▼───┐  ┌──────────▼─▼───┐ │
    │ Wrapper Copy 1  │  │ Wrapper Copy 2 │ │     (Multiple copies
    │ (Stack locals)  │  │ (Stack locals) │ │      can exist safely)
    ├─────────────────┤  ├────────────────┤ │
    │ BoundStruct {   │  │ BoundStruct {  │ │
    │  name: "player" │  │  name: "player"│ │
    │  instance: ─────┼──┼─► &player ◄────┼─┘     All point to SAME
    │  info: PlayerInfo  │  info: PlayerInfo       C++ player
    │ }               │  │ }              │
    └────────┬────────┘  └────────┬───────┘
             │                    │
    ┌────────▼──────────┐ ┌───────▼──────────┐
    │ Python Proxy 1    │ │ Python Proxy 2   │
    │ (Owns wrapper 1)  │ │ (Owns wrapper 2) │
    ├───────────────────┤ ├──────────────────┤
    │ bound: wrapper1 ──┼─┘ bound: wrapper2 ─┼─┐
    └───────────────────┘                      │
                                               │
                            When GC'd:         │
                            delete wrapper2 ─┤─┘
                            (wrapper dies,
                             C++ data lives)
```

---

[Back to Table of Contents](#table-of-contents)


## Code Example: Wrapper Creation Pattern

### Safe Pattern (Both Paths Use This):

**cpp_module.cpp:**
```cpp
static PyObject *cpp_module_getattr(PyObject *module, PyObject *name)
{
    BoundValue *val = PyInterface::get_value_raw(attr_name);
    
    switch (val->type)
    {
    case ValueType::Struct:
    {
        auto *bs = static_cast<BoundStruct *>(val);
        // ✅ Create wrapper copy
        BoundStruct *wrapper = new BoundStruct(bs->name, bs->instance(), bs->info());
        return StructProxy_New(wrapper);  // Proxy owns wrapper
    }
    case ValueType::Vector:
    {
        auto *bv = static_cast<BoundVector *>(val);
        // ✅ Create wrapper copy
        BoundVector *wrapper = new BoundVector(bv->name, bv->raw_vector(), bv->info());
        return VectorProxy_New(wrapper);  // Proxy owns wrapper
    }
    // ...
    }
}
```

**python_proxy.cpp (Root Proxy):**
```cpp
static PyObject *cppproxy_getattro(PyObject *, PyObject *attr)
{
    BoundValue *val = PyInterface::get_value_raw(name);
    
    switch (val->type)
    {
    case ValueType::Struct:
    {
        auto *bs = static_cast<BoundStruct *>(val);
        // ✅ Create wrapper copy (Issue 18 fix)
        BoundStruct *wrapper = new BoundStruct(bs->name, bs->instance(), bs->info());
        return StructProxy_New(wrapper);  // Proxy owns wrapper
    }
    case ValueType::Vector:
    {
        auto *bv = static_cast<BoundVector *>(val);
        // ✅ Create wrapper copy (Issue 18 fix)
        BoundVector *wrapper = new BoundVector(bv->name, bv->raw_vector(), bv->info());
        return VectorProxy_New(wrapper);  // Proxy owns wrapper
    }
    // ...
    }
}
```

---

[Back to Table of Contents](#table-of-contents)


## Ownership Timeline

### Scenario: Python code accesses multiple proxies

```
1. Python: p1 = cpp.player
   → cppproxy_getattro creates wrapper1
   → StructProxy_New(wrapper1)
   → Proxy p1 owns wrapper1 ✅
   
2. Python: p2 = cpp.player
   → cppproxy_getattro creates wrapper2
   → StructProxy_New(wrapper2)
   → Proxy p2 owns wrapper2 ✅
   → Both p1 and p2 reference same C++ player
   
3. Python: del p1
   → StructProxy_dealloc(p1)
   → delete wrapper1 ✅
   → p2 still works (has wrapper2)
   → g_values still owns original ✅
   
4. Python: del p2
   → StructProxy_dealloc(p2)
   → delete wrapper2 ✅
   
5. Program exit:
   → g_values destructor runs
   → unique_ptr<BoundStruct> deletes original ✅
   → NO DOUBLE-FREE!
```

---

[Back to Table of Contents](#table-of-contents)


## Key Design Principles

| Principle | Implementation | Benefit |
|-----------|-----------------|---------|
| **Non-owning references** | Wrapper holds pointers to C++ data, not copy of data | No redundant data, shared by Python proxy |
| **Clear ownership** | g_values owns BoundValue, Proxy owns Wrapper | No ambiguity, predictable cleanup |
| **Separate lifetimes** | C++ data lifetime independent of Python proxy lifetime | Python can't destroy C++ managed data |
| **Multiple wrappers OK** | Each proxy gets independent wrapper copy | Proxies can be created/destroyed freely |
| **Consistent pattern** | Both module path and root proxy use same approach | Maintainable, understandable code |

---

[Back to Table of Contents](#table-of-contents)


## Why This Matters

### Without wrapper pattern (Issue 18 before fix):
```
g_values owns X
├─ Proxy 1 gets pointer to X
├─ Proxy 2 gets pointer to X
├─ Proxy 1 deleted → deletes X ❌
├─ Proxy 2 tries to access X → CRASH (use-after-free)
└─ g_values destructor tries to delete X → DOUBLE-FREE CRASH
```

### With wrapper pattern (After fix):
```
g_values owns X
├─ Proxy 1 owns wrapper1 (points to X)
├─ Proxy 2 owns wrapper2 (points to X)
├─ Proxy 1 deleted → deletes wrapper1 only ✅ (X unchanged)
├─ Proxy 2 deleted → deletes wrapper2 only ✅ (X unchanged)
└─ g_values destructor deletes X safely ✅ (only owner)
```

---

[Back to Table of Contents](#table-of-contents)


## Related Issues

- **Issue 18:** Double-Free in Root Proxy (FIXED by this pattern)
- **Issue 4:** Memory Leak in PyBoundString (Used non-owning Python buffer)
- **Issue 1-3:** Nested Vector Memory Management (Used proper destructors)

---

[Back to Table of Contents](#table-of-contents)


## Best Practices

1. ✅ **Always create wrappers** when returning proxies from accessor functions
2. ✅ **Never pass raw g_values pointers** directly to Python proxy constructors
3. ✅ **Document ownership** with comments at proxy creation sites
4. ✅ **Test multiple proxy access** to same object
5. ❌ **Never store g_values pointers** in Python-facing structures

---

[Back to Table of Contents](#table-of-contents)


## See Also

- `ARCHITECTURE_DEEP_DIVE.md` - Three-layer architecture overview
- `python_proxy.cpp` - Proxy implementation (lines 40-90 for wrapper pattern)
- `cpp_module.cpp` - Module attribute access (lines 38-63 for safe example)
- `value_interface.hpp` - g_values definition and PyInterface::bind()

[Back to Table of Contents](#table-of-contents)

