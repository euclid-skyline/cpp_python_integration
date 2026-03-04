# Semantic Rules: C++ ↔ Python Integration Guide

**Document Date:** March 4, 2026  
**Status:** Active  
**Relevance:** Critical for C++ Python binding design and implementation

---

## Executive Summary

This document addresses the **fundamental semantic differences between C++ and Python** that must be bridged in any C++/Python integration layer. The core principle is:

> **"A Python binding to C++ should behave like a Python object, not like a C++ object."**

When users interact with your binding, they invoke Python semantics. Exposing raw C++ semantics creates confusion, bugs, and violations of Python's design contract.

**Key Finding:** C++ proxies must follow **Python's semantic rules** for safe, predictable, and correct integration. This document covers **30 comprehensive rules** organized by type:
- **15 Core Rules** - Fundamental differences between Python and C++
- **8 Struct-Specific Rules** - Design decisions for exposing C++ structs
- **7 Scalar-Specific Rules** - Type handling for primitive values

Each rule explains the semantic difference, its criticality, implementation approach, and status in your binding.

---

## Table of Contents

1. [Design Philosophy](#design-philosophy)
2. [Criticality Levels](#criticality-levels)
3. [Core Semantic Rules](#core-semantic-rules) (15 rules)
4. [Struct-Specific Rules](#struct-specific-semantic-rules-rules-s1-s8) (8 rules)
5. [Scalar-Specific Rules](#scalar-variable-specific-semantic-rules-rules-sc1-sc7) (7 rules)
6. [Case Study: Issue 55 - Iterator Modification](#case-study-issue-55)
7. [Decision Framework](#decision-framework)
8. [Implementation Checklist](#implementation-checklist)

---

## Design Philosophy

### The Binding Layer Philosophy

A C++ Python binding exists in **two worlds**:

```
C++ World                     Python World
─────────────────────────────────────────────

std::vector<int>              list
  - Value copy semantics      - Reference semantics
  - Size can change anytime   - Modification during iteration = error
  - Multiple integer types    - Single int type
  - Iterators are pointers    - Iterators follow a protocol
  - Manual memory mgmt        - Automatic garbage collection

         ↓ (Binding Layer)

    VectorProxy
    (Implements PYTHON semantics,
     backed by C++ vector)

         ↓

    Python User sees:
    - list-like object
    - Python iteration protocol
    - Python truthiness
    - Python exceptions
```

### Core Principle

**The binding is responsible for translating C++ reality into Python expectations.**

Failing to do this creates:
- 🔴 **Crashes** (accessing freed memory, segfaults)
- 🟠 **Data corruption** (skipped elements, modified values)
- 🟡 **Confusion** (unexpected behavior, violations of Python contract)
- 🟢 **Dead code** (users avoid your binding because it's unpredictable)

---

## Criticality Levels

| Level | Meaning | Consequence of Ignoring |
|-------|---------|------------------------|
| 🔴 **CRITICAL** | Must fix immediately | **Crashes**, **data loss**, **security** |
| 🟠 **HIGH** | Fix ASAP | **Undefined behavior**, **memory corruption**, **leaks** |
| 🟡 **MEDIUM** | Fix when possible | **Confusing**, **wrong results**, **violations of contract** |
| 🟢 **LOW** | Nice to have | **Inconvenience**, **workarounds exist** |

---

## Core Semantic Rules

### Rule 1: Reference Semantics vs Value Semantics

**Criticality:** 🔴 **CRITICAL**

#### Python Semantics
Everything in Python is a **reference to an object**. Assignment binds a name to an object, not a copy.

```python
# Python: Reference semantics
player1 = cpp.get_player()           # player1 → Player object in memory
player2 = player1                    # player2 → SAME object
player1.health = 50
print(player2.health)                # 50 - both names see same object!

# A new proxy to the same C++ object should be the same object
player3 = cpp.get_player()
player1 is player3                   # Should be True if same underlying C++ object
```

#### C++ Reality
C++ assignment creates **copies** by default. References require `&` syntax.

```cpp
// C++: Value semantics
Player p1 = get_player();            // p1 is a COPY
Player p2 = p1;                      // p2 is another COPY
p1.health = 50;                      // p2.health is unchanged

// References require explicit syntax
Player& ref = p1;                    // ref points to same p1
```

#### Impact on Binding
If your proxy returns a **copy** each time, Python users get confusing behavior:

```python
# WRONG: Proxy returns a copy
player = cpp.get_player()
player.name = "Alice"
player2 = cpp.get_player()
print(player2.name)                  # "Bob" - different object! Confusing!

# CORRECT: Proxy returns reference to same object
player = cpp.get_player()
player.name = "Alice"
player2 = cpp.get_player()
print(player2.name)                  # "Alice" - same object, expected!
```

#### How to Resolve in Binding

**Solution:** Use **proxy pooling** or **weak references**.

```cpp
// Approach 1: Cache proxies so same C++ object → same Python proxy
static std::unordered_map<void*, PyObject*> proxy_cache;

PyObject* get_player_proxy(Player* cpp_player) {
    // Check if we already have a proxy for this C++ object
    auto it = proxy_cache.find(cpp_player);
    if (it != proxy_cache.end()) {
        Py_INCREF(it->second);        // Return existing proxy
        return it->second;
    }
    
    // Create new proxy and cache it
    PyObject* proxy = create_proxy(cpp_player);
    Py_INCREF(proxy);
    proxy_cache[cpp_player] = proxy;
    return proxy;
}
```

**Or:** Use Python's `weakref` to let Python manage proxy instances.

#### Status in Your Code
✅ Your current implementation uses **singleton pattern** for cpp.proxy (Issue 54 fixed).
✅ For per-object proxies, you should consider proxy caching.

---

### Rule 2: Immutability vs Mutability

**Criticality:** 🟡 **MEDIUM**

#### Python Semantics
Some Python types are **immutable** (cannot be changed):

```python
# Immutable types
s = "hello"
s[0] = "H"                           # ❌ TypeError: str does not support item assignment

t = (1, 2, 3)
t[0] = 10                            # ❌ TypeError: tuple does not support item assignment

# Mutable types
lst = [1, 2, 3]
lst[0] = 10                          # ✅ OK

d = {"key": "value"}
d["key"] = "new value"               # ✅ OK
```

**Contract:** Users expect to know whether an object is mutable or immutable.

#### C++ Reality
Most C++ objects are **mutable**. Immutability requires `const` and is not enforced as strictly.

```cpp
std::string s = "hello";
s[0] = 'H';                          // ✅ OK - C++ strings are mutable

std::vector<int> v = {1, 2, 3};
v[0] = 10;                           // ✅ OK - all vectors are mutable
```

#### Impact on Binding
If you expose a C++ object through Python, should Python users be able to modify it?

```python
# Example: Global C++ variable
cpp.game_title = "Game"              # Stored in C++
cpp.game_title = "New Game"          # Modifies C++ state
```

**Decision:** Should this be allowed?

- **Allow it** (C++ semantics): Proxies are mutable, changes affect C++ state
- **Forbid it** (Python semantics): Some bindings make proxies read-only

#### How to Resolve in Binding

**Option A: Allow Mutation (Current Approach)**
```cpp
// Implement tp_setattr in your proxy type
static int StructProxy_setattr(PyObject *self, char *name, PyObject *value) {
    // Extract C++ object
    StructProxyObject *proxy = (StructProxyObject *)self;
    
    // Find the field
    const FieldInfo* field = find_field(proxy->bound->info(), name);
    if (!field) {
        PyErr_Format(PyExc_AttributeError, "Unknown field '%s'", name);
        return -1;
    }
    
    // Update C++ object
    return set_field_value(proxy->bound, field, value);
}
```

**Option B: Forbid Mutation (Read-Only)**
```cpp
static int StructProxy_setattr(PyObject *self, char *name, PyObject *value) {
    PyErr_SetString(PyExc_AttributeError, "Proxy is read-only");
    return -1;
}
```

**Opinion:** Allow mutation **for bound variables**, forbid for **temporary proxies** (those created on-the-fly for sub-objects). This prevents confusion about persistence of changes.

#### Status in Your Code
✅ Your binding allows mutation of fields.
⚠️ Consider clarifying documentation about what changes persist to C++.

---

### Rule 3: None vs nullptr - Null Representation

**Criticality:** 🔴 **CRITICAL**

#### Python Semantics
Python has a single **None object** that represents "absence of value".

```python
def get_player(id):
    if player_not_found:
        return None              # Python's null representation
    return player

result = get_player(999)
if result is None:
    print("Not found")
```

Python code **expects** None. Returning `nullptr` from C++ crashes the interpreter.

#### C++ Reality
C++ has `nullptr`, a **non-object sentinel value**. No Python equivalent exists at C level.

```cpp
int* find_player(int id) {
    if (player_not_found) {
        return nullptr;          // C++ null pointer
    }
    return &players[id];
}
```

#### Impact on Binding
**If you return nullptr directly: Interpreter crashes.**

```cpp
// ❌ WRONG - crashes Python
PyObject* get_player(int id) {
    Player* p = find_player_in_cpp(id);
    if (!p) {
        return nullptr;          // ❌ CRASH! This tells Python interpreter an error occurred
    }
    return create_proxy(p);
}

// ✅ CORRECT - returns Python None
PyObject* get_player(int id) {
    Player* p = find_player_in_cpp(id);
    if (!p) {
        Py_RETURN_NONE;          // ✅ Returns Python None object
    }
    return create_proxy(p);
}
```

**Rule:** **ALWAYS return `Py_None` for null values. NEVER return C `nullptr`.**

#### How to Resolve in Binding

Simple: Use `Py_RETURN_NONE` macro or `Py_None` with `Py_INCREF()`.

```cpp
// Option 1: Macro (preferred)
if (!result) {
    Py_RETURN_NONE;              // Increfs and returns Py_None
}

// Option 2: Manual
if (!result) {
    Py_INCREF(Py_None);
    return Py_None;
}

// Option 3: Return Py_None directly (already has refcount 1 due to GIL)
return Py_None;
```

#### Status in Your Code
✅ Your binding correctly uses `Py_RETURN_NONE` in appropriate places.

---

### Rule 4: Negative Indexing

**Criticality:** 🟡 **MEDIUM**

#### Python Semantics
Python supports **negative indices** to access from the end:

```python
items = ['a', 'b', 'c', 'd']
items[0]                             # 'a' - first
items[-1]                            # 'd' - last
items[-2]                            # 'c' - second to last
items[-4]                            # 'a' - first (wraps around)
items[-5]                            # ❌ IndexError - out of range
```

**Formula:** `negative_index = size + negative_index`

#### C++ Reality
C++ doesn't support negative indexing. `v[-1]` accesses memory before the array (undefined behavior).

```cpp
std::vector<char> v = {'a', 'b', 'c', 'd'};
v[-1]                                // ❌ Undefined! Accesses random memory
v[-2]                                // ❌ Undefined!
```

#### Impact on Binding
Python users expect negative indexing to work on your vectors:

```python
enemies = cpp.all_enemies()
last_enemy = enemies[-1]             # Should work
second_last = enemies[-2]            # Should work
```

Without support, users get confusing errors or crashes.

#### How to Resolve in Binding

Convert negative indices to positive before accessing C++ vector:

```cpp
// In VectorProxy_getitem or __getitem__
static PyObject *VectorProxy_getitem(PyObject *self, Py_ssize_t index) {
    VectorProxyObject *proxy = (VectorProxyObject *)self;
    std::size_t size = proxy->bound->size();
    
    // Python-style negative index wrapping
    if (index < 0) {
        index = (Py_ssize_t)size + index;     // Wrap negative to positive
    }
    
    // Bounds check
    if (index < 0 || index >= (Py_ssize_t)size) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return nullptr;
    }
    
    // Safe: index is now positive and in bounds
    return get_element_at(proxy->bound, index);
}
```

Example conversion:
```
size=4, index=-1  →  index = 4 + (-1) = 3    → v[3] (last)
size=4, index=-2  →  index = 4 + (-2) = 2    → v[2] (second to last)
size=4, index=-4  →  index = 4 + (-4) = 0    → v[0] (first)
size=4, index=-5  →  index = 4 + (-5) = -1   → out of range ❌
```

**Also handle in `__setitem__` and `__delitem__` if supported.**

#### Status in Your Code
⚠️ **Check if negative indexing is implemented in `VectorProxy_getitem()`**. If not, add it.

---

### Rule 5: Slicing

**Criticality:** 🟡 **MEDIUM**

#### Python Semantics
Python supports **slice notation**:

```python
items = ['a', 'b', 'c', 'd', 'e']
items[1:3]                           # ['b', 'c'] - from 1 to 3 (3 exclusive)
items[::2]                           # ['a', 'c', 'e'] - every other
items[::-1]                          # ['e', 'd', 'c', 'b', 'a'] - reversed
items[1:]                            # ['b', 'c', 'd', 'e'] - from 1 to end
items[:-1]                           # ['a', 'b', 'c', 'd'] - up to last
```

#### C++ Reality
C++ doesn't support slice syntax natively. You get/set individual elements only.

```cpp
std::vector<char> v = {'a', 'b', 'c', 'd', 'e'};
v[1:3]                               // ❌ Syntax error
```

#### Impact on Binding
Python users expect slicing to work:

```python
enemies = cpp.all_enemies()
first_three = enemies[0:3]           # Should return list of first 3
every_other = enemies[::2]           # Should return every other
```

#### How to Resolve in Binding

Implement `VectorProxy_subscript` with slice support:

```cpp
static PyObject *VectorProxy_subscript(PyObject *self, PyObject *key) {
    VectorProxyObject *proxy = (VectorProxyObject *)self;
    
    // Check if it's a slice
    if (PySlice_Check(key)) {
        Py_ssize_t start, stop, step;
        if (PySlice_GetIndices(key, proxy->bound->size(), &start, &stop, &step) < 0) {
            return nullptr;
        }
        
        // Create list of sliced elements
        PyObject *result = PyList_New(0);
        for (Py_ssize_t i = start; (step > 0 && i < stop) || (step < 0 && i > stop); i += step) {
            PyObject *item = get_element_at(proxy->bound, i);
            PyList_Append(result, item);
            Py_DECREF(item);
        }
        return result;
    }
    
    // Single index
    if (PyLong_Check(key)) {
        Py_ssize_t index = PyLong_AsSsize_t(key);
        return VectorProxy_getitem(self, index);
    }
    
    PyErr_SetString(PyExc_TypeError, "indices must be integers or slices");
    return nullptr;
}

// In type definition:
// .tp_as_mapping->mp_subscript = VectorProxy_subscript;
```

#### Status in Your Code
⚠️ **Check if slicing is implemented**. If not, consider adding it for better Python compatibility.

---

### Rule 6: Iterator Modification Detection

**Criticality:** 🔴 **CRITICAL**

#### Python Semantics
Modifying a container **during iteration** is forbidden:

```python
items = [1, 2, 3]
for item in items:
    print(item)
    items.append(4)               # ❌ RuntimeError: list changed size during iteration
    # Iteration stops here with error
```

**Why Python enforces this:**
1. **Addition** - Iterator would include newly-added items (unexpected)
2. **Deletion** - Iterator would skip elements (data loss)
3. **Reallocation** - Iterator might access freed memory (crash)

Python's contract: **If you modify, you get an error immediately.**

#### C++ Reality
C++ allows modifications during iteration. Iterator invalidation is undefined behavior:

```cpp
std::vector<int> v = {1, 2, 3};
for (auto it = v.begin(); it != v.end(); ++it) {
    std::cout << *it << std::endl;
    v.push_back(4);                // ✅ Allowed in C++
    // But iterator may be invalid now (undefined behavior)
}
```

**C++ contract:** "It's your responsibility to not invalidate iterators."

#### Impact on Binding
Python users expect Python behavior, not C++ behavior:

```python
# User writes natural Python code
for enemy in cpp.enemies:
    if enemy.type == "boss":
        cpp.enemies.append(minion)  # Should raise RuntimeError


# Without fix: Iterator silently continues (wrong!)
# - Might iterate over newly added minion (unexpected)
# - Might skip remaining enemies (data loss)
# - May crash if reallocation occurs

# With fix: RuntimeError immediately (correct!)
# - User sees clear error message
# - Code is forced to follow Python patterns
```

---

### Case Study: Issue 55 - Iterator Modification

#### The Problem (Before Fix)

Your `VectorIterator` tracked only:
- `vector` - reference to proxy
- `index` - current position
- (NO size tracking)

```cpp
// BROKEN: No way to detect modifications
static PyObject *VectorIterator_next(PyObject *self) {
    VectorIteratorObject *it = (VectorIteratorObject *)self;
    VectorProxyObject *proxy = (VectorProxyObject *)it->vector;
    
    // What if vector size changed here?
    if (it->index >= proxy->bound->size()) {   // Size may have changed!
        PyErr_SetNone(PyExc_StopIteration);
        return nullptr;
    }
    // ... get element and return
}
```

**What could go wrong:**

```python
# Scenario 1: Addition (silently wrong)
cpp.enemies = [E1, E2, E3]
for enemy in cpp.enemies:
    cpp.enemies.append(E4)         # VectorIterator_next doesn't know!
    # Iterator continues, includes E4 (unexpected!)

# Scenario 2: Deletion (data loss)
cpp.enemies = [E1, E2, E3]
for enemy in cpp.enemies:
    cpp.enemies.remove(enemy)      # Vector shrinks
    # Iterator skips elements due to index shift!

# Scenario 3: Reallocation (crash)
cpp.enemies = [E1, E2, E3]         # capacity=3
for enemy in cpp.enemies:
    cpp.enemies.append(E4)         # Reallocates to capacity=6
    cpp.enemies.append(E5)         # New address in memory
    # Iterator still points to old memory address
    # 💥 SEGMENTATION FAULT
```

#### The Solution (Issue 55 Fix)

**Add `cached_size` to track the vector size at iterator creation:**

```cpp
// FIXED: Track size for modification detection
typedef struct {
    PyObject_HEAD
    PyObject *vector;          // Reference to proxy
    std::size_t index;         // Current position
    std::size_t cached_size;   // Size at iterator creation (NEW!)
} VectorIteratorObject;

// Capture size at iterator creation
static PyObject *VectorProxy_iter(PyObject *self) {
    VectorIteratorObject *it = PyObject_GC_New(VectorIteratorObject, &VectorIteratorType);
    if (!it) return nullptr;
    
    Py_INCREF(self);
    it->vector = self;
    it->index = 0;
    
    // Issue 55: Cache current size
    VectorProxyObject *proxy = (VectorProxyObject *)self;
    it->cached_size = proxy->bound->size();   // Remember size at creation
    
    PyObject_GC_Track((PyObject *)it);
    return (PyObject *)it;
}

// Detect modifications before each iteration step
static PyObject *VectorIterator_next(PyObject *self) {
    VectorIteratorObject *it = (VectorIteratorObject *)self;
    VectorProxyObject *proxy = (VectorProxyObject *)it->vector;
    
    if (!proxy || !proxy->bound) {
        PyErr_SetString(PyExc_RuntimeError, "Internal error");
        return nullptr;
    }
    
    // Issue 55: Check if vector was modified
    // If size changed, something was added or removed
    std::size_t current_size = proxy->bound->size();
    if (current_size != it->cached_size) {
        // Size mismatch = modification detected
        PyErr_SetString(PyExc_RuntimeError, "vector modified during iteration");
        return nullptr;  // Stop iteration immediately
    }
    
    // Safe to continue
    if (it->index >= current_size) {
        PyErr_SetNone(PyExc_StopIteration);
        return nullptr;
    }
    
    PyObject *item = VectorProxy_getitem((PyObject *)proxy, (Py_ssize_t)it->index);
    if (item) it->index++;
    return item;
}
```

#### Why This Follows Python Semantics

**Python's contract:**
```
"If a container changes size during iteration, RuntimeError is raised."
```

**Our implementation:**
```
"If vector->size() != cached_size, raise RuntimeError."
```

This is **exact semantic matching**. If the user modifies the vector during iteration:
- **Addition:** size increases → `current_size > cached_size` → Error ✅
- **Deletion:** size decreases → `current_size < cached_size` → Error ✅
- **Reallocation:** size reflects change → Error prevents crash ✅

#### Correctness Proof

| Operation | Before Fix | After Fix | Python Semantic |
|-----------|-----------|-----------|-----------------|
| Add element | Iteration includes new element | RuntimeError | ✅ Matches |
| Delete element | Iterator skips elements | RuntimeError | ✅ Matches |
| Reallocate | Potential segfault | RuntimeError | ✅ Matches |
| No modification | Iterations works | Works | ✅ Matches |

---

### Rule 7: Truthiness (bool Conversion)

**Criticality:** 🟡 **MEDIUM**

#### Python Semantics
Objects are "truthy" or "falsy" based on `__bool__()` or `__len__()`:

```python
player = cpp.get_player()
if player:                           # Calls player.__bool__()
    print("Player is valid")

items = cpp.get_items()
if items:                            # Calls len(items) if __bool__ not defined
    print("Has items")
```

**Rules:**
- Result of `__bool__()` is True or False
- If `__len__()` defined: `len(obj) != 0` → True
- Otherwise: Objects are True by default

#### C++ Reality
C++ uses `operator bool()` which is not standard:

```cpp
struct VectorProxy {
    operator bool() const {
        return !vec.empty();         // Non-standard
    }
};
```

#### Impact on Binding
Python code relies on truthiness:

```python
items = cpp.get_items()
if not items:                        # Should work
    print("Empty")

for item in cpp.items_or_empty():
    process(item)                    # Works only if truthy
```

#### How to Resolve in Binding

Implement `tp_as_number->nb_bool`:

```cpp
static int VectorProxy_bool(PyObject *self) {
    VectorProxyObject *proxy = (VectorProxyObject *)self;
    // Return 1 (true) if non-empty, 0 (false) if empty
    return proxy->bound->empty() ? 0 : 1;
}

// In VectorProxyType:
PyNumberMethods vector_as_number = {
    0,                       // nb_add
    // ... other numbers
    VectorProxy_bool,        // nb_bool
};

PyTypeObject VectorProxyType = {
    // ...
    &vector_as_number,       // tp_as_number
};
```

#### Status in Your Code
⚠️ **Check if `__bool__` is implemented for proxies** (both Struct and Vector).

---

### Rule 8: Equality vs Identity

**Criticality:** 🟡 **MEDIUM**

#### Python Semantics
Two operators with different meanings:

```python
a = [1, 2, 3]
b = [1, 2, 3]
c = a

a == b                               # True (same contents)
a is b                               # False (different objects)

a is c                               # True (same object)
```

#### C++ Reality
Equality usually means content equality:

```cpp
std::vector<int> a = {1, 2, 3};
std::vector<int> b = {1, 2, 3};
a == b                               // True (content equal)
// No "is" operator for identity
```

#### Impact on Binding
Decide: What does `proxy1 == proxy2` mean?

```python
# Option A: Identity (same C++ object)
player1 = cpp.get_player(1)
player2 = cpp.get_player(1)
player1 == player2                   # True or False?
                                     # A) True: same C++ object
                                     # B) False: different Python proxies

# Option B: Equality (same values)
player1 = cpp.get_player(1)
player2 = cpp.get_player(2)
player1.health = 50
player2.health = 50
player1 == player2                   # True: same health/name
```

#### Recommendation
**Use identity semantics** (same C++ object → True). This prevents confusion:
- If users want value equality, they can compare specific fields
- Identity is simpler to implement and understand

```cpp
static PyObject *StructProxy_richcmp(PyObject *self, PyObject *other, int op) {
    if (op == Py_EQ) {
        if (!PyObject_TypeCheck(other, &StructProxyType)) {
            Py_RETURN_FALSE;
        }
        
        StructProxyObject *a = (StructProxyObject *)self;
        StructProxyObject *b = (StructProxyObject *)other;
        
        // Compare underlying C++ pointers (identity)
        bool equal = (a->bound == b->bound);
        return PyBool_FromLong(equal);
    }
    Py_RETURN_NOTIMPLEMENTED;
}
```

#### Status in Your Code
⚠️ **Check if `__eq__` is implemented and what it compares**.

---

### Rule 9: Arbitrary Precision Integers

**Criticality:** 🟠 **HIGH**

#### Python Semantics
Integers have unlimited precision:

```python
x = 999999999999999999999999999999999
y = x + 1
print(y)                             # 1000000000000000000000000000000000
# No overflow!
```

#### C++ Reality
Fixed-size integers overflow:

```cpp
int x = INT_MAX;                     // 2147483647
int y = x + 1;                       // ❌ Wraps to -2147483648 (overflow)

unsigned long long x = ULLONG_MAX;
unsigned long long y = x + 1;        // ❌ Wraps to 0
```

#### Impact on Binding
What happens when Python passes a large integer to C++?

```python
cpp.player.score = 9999999999999    # Doesn't fit in C++ int
                                    # What happens?
```

#### How to Resolve in Binding

Check range before converting:

```cpp
static int set_int_field(void *obj, const FieldInfo *field, PyObject *value) {
    // Get Python int value
    long value_long = PyLong_AsLong(value);
    if (value_long == -1 && PyErr_Occurred()) {
        // Conversion failed (might be out of range)
        PyErr_SetString(PyExc_OverflowError, "Integer out of range for field");
        return -1;
    }
    
    // Check bounds
    if (value_long > INT_MAX || value_long < INT_MIN) {
        PyErr_Format(PyExc_OverflowError, 
            "Integer %ld out of range for int field", value_long);
        return -1;
    }
    
    // Safe to convert
    int *int_field = (int *)((char *)obj + field->offset);
    *int_field = (int)value_long;
    return 0;
}
```

**Or:** Automatically coerce to appropriate C++ type:
```cpp
// If Python int too large for C++ int, silently take modulo
int value_int = (int)value_long;     // Silently wraps (NOT RECOMMENDED)
```

**Best practice:** Raise error rather than silently truncate.

#### Status in Your Code
✅ Your binding includes overflow checks (see Issue 53).

---

### Rule 10: String Encoding (UTF-8)

**Criticality:** 🟠 **HIGH**

#### Python Semantics
All strings are Unicode in Python 3:

```python
s = "Hello 世界"
type(s)                              # <class 'str'>
len(s)                               # 8 (characters, not bytes)
s.encode("utf-8")                    # b'Hello \xe4\xb8\x96\xe7\x95\x8c' (bytes)
```

#### C++ Reality
C++ strings are byte arrays. Encoding is implicit:

```cpp
std::string s = "Hello 世界";        // UTF-8? Latin-1? Unknown!
s.length();                          // Bytes, not characters
```

#### Impact on Binding
When C++ returns a `std::string` to Python:

```python
name = cpp.player.name               # What encoding is it?
                                     # Valid UTF-8?
                                     # Contains NUL bytes?
```

#### How to Resolve in Binding

Convert `std::string` to Python `str` (Unicode):

```cpp
static PyObject *string_to_python(const std::string& str) {
    // Assume C++ string is UTF-8
    PyObject *py_str = PyUnicode_FromStringAndSize(
        str.c_str(),
        str.length()
    );
    
    if (!py_str) {
        // Invalid UTF-8 in string
        PyErr_SetString(PyExc_ValueError, "String contains invalid UTF-8");
        return nullptr;
    }
    
    return py_str;
}

// And reverse direction
static bool python_to_string(PyObject *py_str, std::string &out) {
    const char *utf8 = PyUnicode_AsUTF8(py_str);
    if (!utf8) {
        return false;  // Already set error
    }
    
    out = std::string(utf8);
    return true;
}
```

**Document:** "All C++ strings are assumed to be UTF-8 encoded."

#### Status in Your Code
✅ Your binding converts strings to/from Python Unicode strings.

---

### Rule 11: Implicit Type Coercion

**Criticality:** 🟡 **MEDIUM**

#### Python Semantics
Python performs some automatic type conversions:

```python
x = 5 + 2.5              # int + float → float (5.0 + 2.5 = 7.5)
s = "value: " + str(10)  # str + int → coerce to str
result = True + 5        # bool + int → True treated as 1, result = 6
```

#### C++ Reality
C++ is more strict about type conversions. Coercions are limited and require explicit casts in many contexts.

```cpp
int x = 5;
double y = x + 2.5;     // int + double → implicit conversion to double
// std::string s = "value: " + std::to_string(10);  // Must explicitly convert
```

#### Impact on Binding
Decision: When Python passes a value of wrong type, what happens?

```python
# Example: int field expects int, user passes float
cpp.player.level = 5.7   # Should this be:
                         # A) Coerced to 5? (Python style)
                         # B) Raise TypeError? (C++ strict style)
```

**Scenario 1: Permissive (Python-like)**
```python
cpp.player.health = 50.9  # Accepts float, coerces to 50
cpp.player.active = 1     # Accepts int, coerces to True
```

**Scenario 2: Strict (C++ defensive)**
```python
cpp.player.health = 50.9  # ❌ TypeError: Expected int
cpp.player.active = 1     # ❌ TypeError: Expected bool
```

#### How to Resolve in Binding

**Option A: Strict (Recommended for safety)**
```cpp
static int set_int_field(void *obj, const FieldInfo *field, PyObject *value) {
    // Strict: Require exact type
    if (!PyLong_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "Expected int");
        return -1;
    }
    
    long val = PyLong_AsLong(value);
    // ... rest of implementation
}
```

**Option B: Permissive (Python-like)**
```cpp
static int set_int_field(void *obj, const FieldInfo *field, PyObject *value) {
    long val = 0;
    
    // Try int first
    if (PyLong_Check(value)) {
        val = PyLong_AsLong(value);
    }
    // Try float conversion
    else if (PyFloat_Check(value)) {
        val = (long)PyFloat_AsDouble(value);  // Truncate float
    }
    // Try bool conversion
    else if (PyBool_Check(value)) {
        val = PyObject_IsTrue(value);
    }
    else {
        PyErr_SetString(PyExc_TypeError, "Cannot convert to int");
        return -1;
    }
    
    // ... rest of implementation
}
```

**Opinion:** Use **strict** by default. Users can explicitly convert if needed (`int(value)`). This prevents silent data loss from float truncation.

#### Status in Your Code
⚠️ **Verify your binding's approach**: Is it strict or permissive?

---

### Rule 12: Weak References (`weakref`)

**Criticality:** 🟡 **MEDIUM** (Advanced feature, not critical for basic usage)

#### Python Semantics
Weak references allow keeping track of objects without preventing garbage collection:

```python
import weakref

class Player:
    pass

player = Player()
weak = weakref.ref(player)

# While player exists:
if weak() is not None:
    p = weak()
    print(p.name)

# Delete original
del player

# Now weak reference returns None:
if weak() is None:
    print("Player was garbage collected")
```

#### C++ Reality
C++ doesn't have built-in weak references. All references keep objects alive.

#### Impact on Binding
Without weak reference support, Python users cannot efficiently track proxy objects:

```python
# Can't do this effectively (strong reference keeps proxy alive)
enemies = []
for enemy in cpp.all_enemies():
    enemies.append(weakref.ref(enemy))

# All proxies are kept alive even if C++ object deleted
```

#### How to Resolve in Binding

Implement `tp_weaklistoffset` in your proxy types:

```cpp
typedef struct {
    PyObject_HEAD
    BoundStruct *bound;
    PyObject *parent_proxy;
    PyObject *weakref_list;  // YES: Add this field
} StructProxyObject;

PyTypeObject StructProxyType = {
    // ...
    offsetof(StructProxyObject, weakref_list),  // tp_weaklistoffset (NEW)
    // ...
};
```

Then Python automatically supports weak references:

```python
import weakref
player = cpp.get_player()
weak = weakref.ref(player)  # ✅ Works!
```

#### Status in Your Code
⚠️ **Check if `tp_weaklistoffset` is set** in your proxy type definitions.

---

### Rule 13: Container Lifetime and Views

**Criticality:** 🟠 **HIGH**

#### Python Semantics
Views into containers (like dictionary views) remain valid as long as the container exists:

```python
d = {"a": 1, "b": 2}
keys = d.keys()           # Dictionary view
len(keys)                 # 2

d["c"] = 3                # Modify original
len(keys)                 # 3 - view reflects changes
```

#### C++ Reality
Iterators and pointers into containers become invalid if the container is modified:

```cpp
std::vector<int> v = {1, 2, 3};
auto it = v.begin();
v.push_back(4);           // Iterator may be invalid now!
*it;                      // ❌ Undefined behavior
```

#### Impact on Binding
What if user keeps a reference to a container view/slice?

```python
enemies = cpp.all_enemies()        # Returns VectorProxy
enemies_list = list(enemies)       # Makes a snapshot
# Later:
cpp.all_enemies().append(new)      # Modifies original
# enemies_list doesn't change (it's a snapshot)
# enemies (if still held) is now in inconsistent state
```

#### How to Resolve in Binding

**Recommendation:** Always return **copies** of container contents when returning lists/sequences to Python, unless you explicitly support views.

```cpp
// BAD: Return view that can become invalid
PyObject *get_enemies() {
    return enemies_proxy;  // ❌ View becomes invalid if modified
}

// GOOD: Return snapshot
PyObject *get_enemies() {
    PyObject *list = PyList_New(0);
    for (const auto& enemy : enemies) {
        PyObject *proxy = create_proxy(enemy);
        PyList_Append(list, proxy);
        Py_DECREF(proxy);
    }
    return list;  // ✅ Snapshot is safe
}

// ALSO GOOD: Explicit proxy (user knows it can change)
PyObject *get_enemies_proxy() {
    return enemies_proxy;  // ✅ User explicitly requested proxy
}
```

**Document:** Clarify which returns are snapshots and which are live views.

#### Status in Your Code
✅ Your binding returns proxies that reflect live C++ state (documented behavior).

---

### Rule 14: Method Binding vs Function Binding

**Criticality:** 🟡 **MEDIUM**

#### Python Semantics
Methods are automatically bound to instances:

```python
class Player:
    def take_damage(self, amount):
        self.health -= amount

player = Player()
method = player.take_damage         # Bound method
method(10)                          # self=player is automatic
```

#### C++ Reality
Functions don't have automatic binding. Method calls are just function calls with explicit object parameter:

```cpp
struct Player {
    void take_damage(int amount) {  // No implicit 'self'
        health -= amount;
    }
};

Player p;
p.take_damage(10);                  // Explicit object
```

#### Impact on Binding
Your proxy needs to implement method binding correctly:

```python
player = cpp.get_player()
# These should be equivalent:
player.take_damage(10)              # Method call
Player.take_damage(player, 10)      # Unbound method call (if Python 2 style)
```

#### How to Resolve in Binding

Implement method in your proxy type and extract self automatically:

```cpp
// Method function - receives bound proxy as self
static PyObject *StructProxy_take_damage(PyObject *self, PyObject *args) {
    StructProxyObject *proxy = (StructProxyObject *)self;  // self = proxy
    int amount;
    
    if (!PyArg_ParseTuple(args, "i", &amount)) {
        return nullptr;
    }
    
    // Call C++ method with automatic self
    proxy->bound->take_damage(amount);
    Py_RETURN_NONE;
}

// Register in methods table
static PyMethodDef struct_methods[] = {
    {"take_damage", StructProxy_take_damage, METH_VARARGS, "Take damage"},
    {nullptr}
};

// In type definition:
PyTypeObject StructProxyType = {
    // ...
    struct_methods,  // tp_methods
    // ...
};
```

**This is already how your binding works** with `tp_methods`.

#### Status in Your Code
✅ Your binding correctly implements method binding through `tp_methods`.

---

### Rule 15: Copy Semantics in Assignment

**Criticality:** 🟡 **MEDIUM** (Related to Rule 1, but distinct aspect)

#### Python Semantics
Assignment creates a **reference** to the same object, not a copy:

```python
list1 = [1, 2, 3]
list2 = list1              # REFERENCE, NOT COPY
list2.append(4)
print(list1)               # [1, 2, 3, 4] - both see same object

list3 = list1.copy()       # EXPLICIT copy
list3.append(5)
print(list1)               # [1, 2, 3, 4] - unchanged
```

#### C++ Reality
Assignment creates a **copy** by default:

```cpp
std::vector<int> vec1 = {1, 2, 3};
std::vector<int> vec2 = vec1;      // COPY
vec2.push_back(4);
// vec1 is still {1, 2, 3}
```

#### Impact on Binding
When users assign proxies, what happens?

```python
# Scenario A: Assignment creates reference (Python semantics)
enemies1 = cpp.get_enemies()
enemies2 = enemies1                # Reference to same proxy
enemies2.append(new)
# enemies1 also sees the new enemy

# Scenario B: Assignment creates new proxy (C++ semantics)
enemies1 = cpp.get_enemies()
enemies2 = enemies1                # New proxy to same C++ object
enemies2.append(new)
# If proxies track state separately, unexpected behavior!
```

#### How to Resolve in Binding

**Recommendation:** Keep proxies as **lightweight references** to C++ objects. Assignment of proxy variables is assignment of references, not copies.

```cpp
// VectorProxy is lightweight (just holds pointer to C++ vector)
typedef struct {
    PyObject_HEAD
    BoundVector *bound;  // Pointer, not copy
    PyObject *parent_proxy;
} VectorProxyObject;

// When user does enemies2 = enemies1
// Python reference counting handles it
// Both proxy variables point to same C++ vector
```

**This matches Python semantics automatically** if your proxy is just a **reference** to the underlying C++ object, not a copy.

#### Status in Your Code
✅ Your binding correctly uses references (proxies point to C++ objects, don't copy them).

---

## Struct-Specific Semantic Rules (Rules S1-S8)

In addition to the 15 core rules, structs have unique semantic considerations. These rules address design decisions specific to how C++ struct semantics interact with Python expectations.

### Rule S1: Field Initialization and Default Values

**Criticality:** 🟡 **MEDIUM**

#### Problem
When a Python user accesses a struct's field, what happens if the C++ field was never explicitly initialized?

```python
player = cpp.create_player()
print(player.health)         # Uninitialized in C++?
                             # Return 0? Return None? Raise error?
```

#### Python Semantics
Python objects always have initialized attributes or raise `AttributeError`:

```python
class Player:
    def __init__(self):
        self.health = 100

p = Player()
print(p.health)              # Always initialized
print(p.undefined)           # ❌ AttributeError
```

#### C++ Reality
Uninitialized struct members contain garbage values:

```cpp
struct Player {
    int health;              // Could be garbage if not initialized
};

Player p;                    // Uninitialized!
```

#### Decision
**Assume all C++ struct fields are properly initialized** before exposure to Python. Document that uninitialized access is undefined behavior.

#### Status in Your Code
✅ Assumption: C++ structs are initialized by their constructors.

---

### Rule S2: Method Overloading Resolution

**Criticality:** 🟡 **MEDIUM**

#### Problem
C++ allows method overloading (same name, different signatures). Python doesn't.

```cpp
struct Player {
    void take_damage(int amount);
    void take_damage(double amount);
    void take_damage(std::string reason);
};
```

Python cannot have duplicate method names:

```python
class Player:
    def take_damage(self, amount):     # Only one version
        pass
```

#### How to Resolve in Binding

**Solution: Type-based dispatch in single Python method**

```cpp
static PyObject *Player_take_damage(PyObject *self, PyObject *arg) {
    StructProxyObject *proxy = (StructProxyObject *)self;
    
    // Dispatch based on argument type
    if (PyLong_Check(arg)) {
        int amount = PyLong_AsLong(arg);
        proxy->bound->take_damage(amount);
    } else if (PyFloat_Check(arg)) {
        double amount = PyFloat_AsDouble(arg);
        proxy->bound->take_damage(amount);
    } else if (PyUnicode_Check(arg)) {
        const char *reason = PyUnicode_AsUTF8(arg);
        proxy->bound->take_damage(reason);
    } else {
        PyErr_SetString(PyExc_TypeError, "Unexpected argument type");
        return nullptr;
    }
    
    Py_RETURN_NONE;
}
```

User experience:
```python
player.take_damage(10)       # Calls take_damage(int)
player.take_damage(3.5)      # Calls take_damage(double)
player.take_damage("poison") # Calls take_damage(str)
```

#### Status in Your Code
⚠️ **Check:** How are overloaded methods handled in your binding?

---

### Rule S3: Operator Overloading

**Criticality:** 🟡 **MEDIUM**

#### Problem
C++ operators (`+`, `-`, `==`, etc.) should map to Python special methods (`__add__`, `__sub__`, `__eq__`).

```cpp
struct Vector {
    Vector operator+(const Vector& other) const;
    Vector operator-(const Vector& other) const;
    bool operator==(const Vector& other) const;
};
```

#### How to Resolve in Binding

Expose operators as Python special methods:

```cpp
static PyObject *Vector_add(PyObject *self, PyObject *other) {
    StructProxyObject *a = (StructProxyObject *)self;
    StructProxyObject *b = (StructProxyObject *)other;
    
    Vector result = *a->bound + *b->bound;
    return create_struct_proxy(&result);
}

// In type definition:
PyNumberMethods vector_as_number = {
    Vector_add,              // nb_add (+)
    Vector_sub,              // nb_subtract (-)
    Vector_mul,              // nb_multiply (*)
    // ...
};

PyTypeObject VectorProxyType = {
    // ...
    &vector_as_number,       // tp_as_number
    // ...
};
```

#### Status in Your Code
⚠️ **Check:** Are C++ operators exposed as `__add__`, `__sub__`, etc.?

---

### Rule S4: Field Access Control (public/private)

**Criticality:** 🟡 **MEDIUM**

#### Problem
C++ has `public`, `private`, `protected`. Python has only convention (`_field` = private).

```cpp
struct Player {
public:
    string name;             // Public
private:
    float health;            // Private
};
```

#### Decision: Should Python respect C++ visibility?

**Recommendation:** Yes - enforce C++ access control in the binding.

```cpp
static PyObject *StructProxy_getattro(PyObject *self, PyObject *name) {
    StructProxyObject *proxy = (StructProxyObject *)self;
    const char *attr_name = PyUnicode_AsUTF8(name);
    
    const FieldInfo *field = find_field(proxy->bound->info(), attr_name);
    if (!field) {
        PyErr_Format(PyExc_AttributeError, "Unknown field '%s'", attr_name);
        return nullptr;
    }
    
    // Enforce C++ access control
    if (field->visibility == PRIVATE) {
        PyErr_Format(PyExc_AttributeError, 
            "Cannot access private field '%s'", attr_name);
        return nullptr;
    }
    
    return get_field_value(proxy->bound, field);
}
```

#### Status in Your Code
⚠️ **Check:** Are private/protected C++ fields hidden from Python?

---

### Rule S5: Nested Struct Access and Proxy Chaining

**Criticality:** 🟡 **MEDIUM**

#### Problem
When a struct contains another struct, accessing nested fields must return a proxy pointing into the parent's memory.

```cpp
struct Enemy {
    Position pos;            // Nested struct
};
```

```python
enemy = cpp.get_enemy()
pos = enemy.pos              # Should be proxy into enemy's memory
pos.x = 100                  # Must modify enemy.pos
```

#### How to Resolve in Binding

**Return proxy pointing to nested struct within parent:**

```cpp
static PyObject *StructProxy_getattro(PyObject *self, PyObject *name) {
    StructProxyObject *proxy = (StructProxyObject *)self;
    const FieldInfo *field = find_field(proxy->bound->info(), attr_name);
    
    if (field->type == ValueType::Struct) {
        // Calculate address of nested struct within parent
        void *nested_addr = (char*)proxy->bound->instance() + field->offset;
        
        // Create proxy into nested struct (not a copy)
        return create_nested_struct_proxy(proxy, nested_addr, field->info());
    }
    
    return get_field_value(proxy->bound, field);
}
```

**Key:** Both parent and nested proxies point to same C++ memory, so changes are visible everywhere.

#### Status in Your Code
✅ Your binding likely handles this correctly.

---

### Rule S6: Struct Comparison and Equality Semantics

**Criticality:** 🟡 **MEDIUM**

#### Problem
What should `struct1 == struct2` mean?

```python
enemy1 = cpp.get_enemy(1)
enemy2 = cpp.get_enemy(1)      # Same C++ object
enemy1 == enemy2               # True or False?
                               # A) True (same object)
                               # B) True (same values)
                               # C) False (different proxies)
```

#### Recommendation
**Use identity semantics** (option A):
- `==` returns True if both proxies point to same C++ object
- Simple, unambiguous, prevents confusion
- Users can compare specific fields if they want value equality

```cpp
static PyObject *StructProxy_richcmp(PyObject *self, PyObject *other, int op) {
    if (op == Py_EQ) {
        if (!PyObject_TypeCheck(other, &StructProxyType)) {
            Py_RETURN_FALSE;
        }
        
        StructProxyObject *a = (StructProxyObject *)self;
        StructProxyObject *b = (StructProxyObject *)other;
        
        // Identity: same C++ object
        bool equal = (a->bound == b->bound);
        return PyBool_FromLong(equal);
    }
    
    Py_RETURN_NOTIMPLEMENTED;
}
```

#### Status in Your Code
⚠️ **Verify:** Check what `__eq__` implementation does for struct proxies.

---

### Rule S7: Method Return Types (Reference vs Copy)

**Criticality:** 🟡 **MEDIUM**

#### Problem
When a struct method returns another struct, Python must understand if it's a reference or a copy.

```cpp
struct Game {
    Player get_current_player() const {
        return current_player;  // Return by value = copy
    }
    
    Player& get_current_player_ref() {
        return current_player;  // Return by reference
    }
};
```

#### Decision
- **Return by reference (`&`):** Create proxy pointing into original parent object
- **Return by value (copy):** Create proxy to independent copy (modifications don't affect parent)

```python
game = cpp.get_game()

# Reference return (modifications visible)
player = game.get_current_player_ref()
player.health = 50
assert game.get_current_player_ref().health == 50  # ✅ Changed in game

# Value return (modifications lost)
player = game.get_current_player()
player.health = 50
assert game.get_current_player().health != 50      # ❌ Copy is independent
```

#### Document
**Clear documentation of which returns are references vs copies.** This is critical for user understanding.

#### Status in Your Code
⚠️ **Check:** Is this distinction documented?

---

### Rule S8: Custom Constructors and Factory Methods

**Criticality:** 🟡 **MEDIUM**

#### Problem
How does Python create struct instances? What if C++ requires complex initialization?

```cpp
struct Player {
    Player();                              // Default constructor
    Player(const string& name);            // Custom constructor
    static Player CreateFromFile(const string& path);  // Factory
};
```

#### How to Resolve in Binding

**Option A: Expose constructors directly**
```python
player = cpp.Player()                     # Default constructor
player = cpp.Player("Alice")              # Custom constructor
```

**Option B: Use factory functions**
```python
player = cpp.create_player()
player = cpp.create_player_from_file("save.dat")
```

**Option C: Both**
```python
player = cpp.Player()                     # Constructor
player = cpp.Player.from_file("save.dat") # Class method
```

Implement with `tp_new` or factory functions in module:

```cpp
// Factory function approach (simpler)
static PyObject *create_player(PyObject *self, PyObject *args) {
    const char *name = nullptr;
    if (!PyArg_ParseTuple(args, "|s", &name)) {
        return nullptr;
    }
    
    Player *p = name ? new Player(name) : new Player();
    return create_struct_proxy(p);
}

// Register in module functions
static PyMethodDef module_functions[] = {
    {"create_player", create_player, METH_VARARGS, "Create a player"},
    {nullptr}
};
```

#### Status in Your Code
⚠️ **Check:** How are struct instances created from Python?

---

## Scalar Variable-Specific Semantic Rules (Rules Sc1-Sc7)

Scalar variables (int, float, bool, string) have different semantics than containers or complex objects. These rules address design decisions for binding scalar value types.

### Rule Sc1: Scalar Type Conversion and Coercion

**Criticality:** 🟡 **MEDIUM**

#### Problem
When Python passes a scalar of "wrong" type, what happens?

```python
cpp.player.level = 5.7       # int field, float value
cpp.player.active = 1        # bool field, int value
cpp.player.health = "100"    # int field, string value
```

#### Decision Options

**Option A: Strict type checking** (Recommended)
```cpp
if (!PyLong_Check(value)) {
    PyErr_SetString(PyExc_TypeError, "Expected int");
    return -1;
}
```

User must convert explicitly:
```python
cpp.player.level = int(5.7)
cpp.player.health = int("100")
```

**Option B: Permissive coercion** (Python-like)
```cpp
long val = 0;
if (PyLong_Check(value)) {
    val = PyLong_AsLong(value);
} else if (PyFloat_Check(value)) {
    val = (long)PyFloat_AsDouble(value);  // Truncate
} else if (PyUnicode_Check(value)) {
    val = strtol(PyUnicode_AsUTF8(value), nullptr, 10);  // Parse string
}
```

**Recommendation:** **Strict by default** (Option A) - prevents silent data loss from truncation.

#### Status in Your Code
✅ Your binding uses strict type checking (see Issue 11 discussion).

---

### Rule Sc2: Integer Range Validation

**Criticality:** 🟠 **HIGH**

#### Problem
Python integers have unlimited precision. C++ integers have fixed size.

```python
cpp.player.id = 9999999999999999999999  # Doesn't fit in C++ int!
```

#### How to Resolve in Binding

Check range before converting:

```cpp
long value_long = PyLong_AsLong(value);
if (value_long == -1 && PyErr_Occurred()) {
    // Out of range
    PyErr_SetString(PyExc_OverflowError, "Integer too large");
    return -1;
}

if (value_long > INT_MAX || value_long < INT_MIN) {
    PyErr_SetString(PyExc_OverflowError, "Integer out of range for field");
    return -1;
}

int *field = (int*)((char*)obj + offset);
*field = (int)value_long;
```

#### Status in Your Code
✅ Implemented (see Issue 53 - overflow checks).

---

### Rule Sc3: Float Precision and Representation

**Criticality:** 🟡 **MEDIUM**

#### Problem
Python floats are 64-bit (double). C++ may have 32-bit floats (float).

```python
cpp.enemy.accuracy = 99.999999999  # 64-bit precision
# Stored in C++ float (32-bit), loses precision
print(cpp.enemy.accuracy)          # May be 100.0
```

#### Decision
**Document that float precision may be lost.** C++ float fields will lose precision from Python 64-bit floats.

```python
# Document in binding:
"""
cpp.enemy.accuracy - float field (32-bit)
Reading/writing may lose precision due to C++ float representation.
Python float (64-bit) → C++ float (32-bit) truncation.
"""
```

#### Status in Your Code
⚠️ **Check:** Is precision loss documented for float fields?

---

### Rule Sc4: Boolean Interpretation

**Criticality:** 🟡 **MEDIUM**

#### Problem
Python booleans are `True`/`False`, but can be created from any value via truthiness.

```python
cpp.player.alive = 1         # Int converted to bool?
cpp.player.alive = 0         # Int converted to bool?
cpp.player.alive = "yes"     # String converted to bool?
```

#### How to Resolve in Binding

**Option A: Strict** (if not exactly bool, error)
```cpp
if (!PyBool_Check(value)) {
    PyErr_SetString(PyExc_TypeError, "Expected bool");
    return -1;
}
```

**Option B: Coerce from int** (common in C++)
```cpp
if (PyBool_Check(value)) {
    val = PyObject_IsTrue(value);
} else if (PyLong_Check(value)) {
    val = PyObject_IsTrue(value);  // 0=False, non-zero=True
} else {
    PyErr_SetString(PyExc_TypeError, "Cannot convert to bool");
    return -1;
}
```

**Recommendation:** **Option B** - allow int-to-bool conversion (common in C++).

```python
cpp.player.alive = True      # ✅ bool
cpp.player.alive = 1         # ✅ int converted (1=True)
cpp.player.alive = 0         # ✅ int converted (0=False)
cpp.player.alive = "yes"     # ❌ TypeError
```

#### Status in Your Code
⚠️ **Check:** How are boolean fields handled?

---

### Rule Sc5: String Mutability and Immutability

**Criticality:** 🟡 **MEDIUM**

#### Problem
Python strings are immutable. C++ strings are mutable. What should a string field support?

```python
cpp.player.name = "Alice"
cpp.player.name = "Bob"      # Replace entire string

# Or:
cpp.player.name[0] = "B"     # Modify first character?
                             # Support this?
```

#### Decision
**Strings are immutable at Python level.** The field can be replaced entirely, but individual characters cannot be modified.

```python
cpp.player.name = "Bob"      # ✅ Replace entire string

cpp.player.name[0] = "B"     # ❌ TypeError: string does not support item assignment
```

This is automatic if you treat string fields as simple getters/setters, not sequences.

#### Status in Your Code
✅ Likely correct (strings treated as atomic values, not sequences).

---

### Rule Sc6: Numeric Type Narrowing and Widening

**Criticality:** 🟡 **MEDIUM**

#### Problem
What if C++ field is `long` but Python passes `int`? Or vice versa?

```python
cpp.player.score = 100       # int → long (widening, safe)
cpp.player.id = 999999999999 # int → uint32 (narrowing, dangerous)
```

#### How to Resolve in Binding

**Use largest intermediate type for conversion:**

```cpp
static int set_int32_field(void *obj, const FieldInfo *field, PyObject *value) {
    // Use long (largest commonly supported) as intermediate
    long val = PyLong_AsLong(value);
    if (val == -1 && PyErr_Occurred()) {
        return -1;
    }
    
    // Check bounds for target type (int32)
    if (val > INT32_MAX || val < INT32_MIN) {
        PyErr_SetString(PyExc_OverflowError, "Value out of range for int32");
        return -1;
    }
    
    int32_t *field_ptr = (int32_t*)((char*)obj + field->offset);
    *field_ptr = (int32_t)val;
    return 0;
}
```

#### Status in Your Code
✅ Implemented (see Issue 53).

---

### Rule Sc7: Optional/Nullable Scalar Fields

**Criticality:** 🟡 **MEDIUM**

#### Problem
Some scalars can be "not set" or "missing". How to represent in Python?

```cpp
struct Player {
    int level;               // Always has a value
    int* optional_level;     // Can be nullptr
    std::optional<int> maybe_level;  // Can be empty
};
```

#### How to Resolve in Binding

**Pointer fields:** Return `None` if nullptr, otherwise value
```python
player = cpp.get_player()
player.optional_level        # None if C++ nullptr, else int

cpp.player.optional_level = 50   # Set to value
cpp.player.optional_level = None # Set to NULL/nullptr
```

**std::optional fields:** Similar behavior
```python
player.maybe_level           # None if empty, else value
```

Implementation:
```cpp
static PyObject *get_optional_int(void *obj, const FieldInfo *field) {
    int *ptr = *(int**)((char*)obj + field->offset);
    if (!ptr) {
        Py_RETURN_NONE;
    }
    return PyLong_FromLong(*ptr);
}

static int set_optional_int(void *obj, const FieldInfo *field, PyObject *value) {
    if (value == Py_None) {
        // Set to nullptr
        int **ptr_field = (int**)((char*)obj + field->offset);
        *ptr_field = nullptr;
        return 0;
    }
    
    long val = PyLong_AsLong(value);
    // ... set value
}
```

#### Status in Your Code
⚠️ **Check:** How are optional/nullable scalar fields handled?

---

## Summary: Coverage by Type

| Rule Category | Core Rules (15) | Struct Rules (8) | Scalar Rules (7) | Total |
|---------------|-----------------|-----------------|-----------------|-------|
| **Vector/Container Specific** | Rules 4, 5, 6 | - | - | 3 |
| **Struct Specific** | Rules 1, 2, 3, 8, 14, 15 | All (S1-S8) | - | 14 |
| **Scalar Specific** | Rules 1, 2, 3, 7, 9, 10, 11 | - | All (Sc1-Sc7) | 14 |
| **All Types** | Rules 1, 3, 12, 13 | Rule S5 | Rule Sc5 | 3 |

**Total semantic rules:** 15 core + 8 struct-specific + 7 scalar-specific = **30 rules** covering all aspects of C++/Python binding semantics.

---

## Decision Framework

When designing your binding, ask these questions for each attribute/method:

| Question | Answer A | Answer B | Answer C | Recommendation |
|----------|----------|----------|----------|-----------------|
| Should mutation persist to C++? | Yes | No | - | Allow; document clearly |
| Should proxy return None or raise? | Return None | Raise exception | - | Return None for optional, Raise for errors |
| Should negative indexing work? | Yes | Complicated | - | Yes - implement it |
| Should slicing work? | Yes | Too much work | - | Nice to have, not critical |
| What does == mean? | Identity | Equality | - | Identity is simpler |
| How large can integers be? | No limit (Python) | Limited (C++) | - | Check range, raise error |
| Type coercion on mismatch? | Strict (error) | Permissive (coerce) | - | Strict is safer |
| Support weak references? | Yes | Not needed now | - | Yes, add `tp_weaklistoffset` |
| Container returns snapshot or view? | Snapshot (copy) | Live view (proxy) | - | Doc both; prefer snapshot for safety |
| Assignment semantics? | Reference | Copy | - | Reference (via proxy is lightweight) |
| Method binding work correctly? | Yes, tested | Need to verify | - | Verify `tp_methods` working |

---

## Implementation Checklist

Use this checklist to audit your binding against Python semantics:

### Reference Semantics
- [ ] Same C++ object always returns same proxy (or at least behaves identically)
- [ ] Modifications through proxy affect C++ state
- [ ] Clearing Python reference doesn't crash C++

### Null Handling
- [ ] Use `Py_RETURN_NONE` for null (never `nullptr`)
- [ ] Document which functions can return None
- [ ] All None returns properly set in type definition

### Negative Indexing  
- [ ] `proxy[-1]` works correctly
- [ ] `proxy[-2]` works correctly
- [ ] `proxy[-size]` works correctly
- [ ] Out-of-range negative indices raise `IndexError`

### Slicing (Optional but Nice)
- [ ] `proxy[start:stop]` works
- [ ] `proxy[::step]` works  
- [ ] Returns list, not proxy

### Iterator Safety
- [ ] Modification during iteration raises `RuntimeError`
- [ ] All containers track cached size
- [ ] Any append/delete changes detected

### Truthiness
- [ ] `if proxy:` works
- [ ] Empty containers are falsy
- [ ] Null proxies are falsy

### String Encoding
- [ ] C++ strings converted to Python `str` (Unicode)
- [ ] Python `str` values assume UTF-8 in C++
- [ ] Invalid UTF-8 raises `ValueError`

### Integer Bounds
- [ ] Overflow checked before conversion
- [ ] Out-of-range raises `OverflowError`
- [ ] No silent truncation

### Type Coercion
- [ ] Decision made: strict or permissive?
- [ ] Documented clearly
- [ ] No silent data loss from coercion
- [ ] Explicit conversion available if needed

### Weak References
- [ ] `tp_weaklistoffset` set in types (if supported)
- [ ] Users can create `weakref.ref(proxy)`
- [ ] Weak refs return None after object deleted

### Container Lifetime
- [ ] Documented: Are returns snapshots or live views?
- [ ] If live views: changes visible to Python
- [ ] If snapshots: independent copies safe from C++ changes

### Method Binding
- [ ] Methods accessible as `proxy.method_name()`
- [ ] `self` binding automatic
- [ ] Unbound method calls work if needed

### Assignment Semantics
- [ ] Assignment creates reference, not copy
- [ ] Proxy variables point to same C++ object
- [ ] Clear documentation of reference vs copy behavior

### Exception Safety
- [ ] C++ exceptions never escape to Python
- [ ] All C++ operations wrapped in try-catch
- [ ] Python exceptions properly set on error

### Memory Management
- [ ] Reference counting correct
- [ ] No memory leaks on error paths
- [ ] GC support for proxies with PyObject members
- [ ] Circular references broken

### Type Initialization
- [ ] All types initialized exactly once
- [ ] Module cleanup registered (`m_free`)
- [ ] No partial initialization states

---

## My Opinion: Should C++ Follow Python Semantics?

**Yes, absolutely. Here's why:**

### 1. **User Expectation**
When a Python programmer uses a C++ binding, they think in Python. Violating Python semantics creates:
- Confusion ("Why doesn't slicing work?")
- Bugs ("Iterator crashed the whole program!")
- Distrust ("This binding is too dangerous")

### 2. **Python is the User Interface**
The binding language **is** Python. Your implementation language (C++) is an implementation detail. Users should never need to understand C++ semantics to use your binding correctly.

### 3. **Stability and Robustness**
Python's rules exist for good reasons:
- Iterator modification raises error → prevents data corruption
- None vs nullptr → prevents crashes
- Reference semantics → clearer ownership
- Type checking → fewer mysterious bugs

Implementing these rules makes your binding **stable and crash-resistant**.

### 4. **Principle: Principle of Least Surprise**
Code that violates Python conventions surprises users:

```python
# User's natural assumption
for item in my_list:
    my_list.append(new_item)   # Should raise error (Python contract)

# If your binding doesn't enforce this, users get confused
# They eventually discover the hard way (with bugs)
```

### 5. **Cost-Benefit Analysis**

**Cost of following Python semantics:**
- Implementation effort: Small to medium
- Performance impact: Negligible (a few checks)
- Code complexity: Low (well-documented patterns)

**Benefit:**
- Predictable behavior
- No crashes from undefined behavior
- Users can transfer their Python knowledge
- Maintainability: Easy to understand

**Verdict:** Virtually free. Always do it.

### 6. **Where C++ Semantics MIGHT Be Appropriate**

There are **rare cases** where exposing C++ semantics makes sense:

```python
# Example: High-performance numeric library
# User knows they're working with C++, not pure Python
import numpy as np
arr = np.array([1, 2, 3])           # Might use C++ semantics for performance
```

But for a **general-purpose binding** (like yours), always prefer Python semantics.

---

## Closing Principles

When C++ and Python disagree on semantics:

1. **Choose Python semantics** by default
2. **Document the choice** clearly
3. **Provide both** if practical (e.g., read-only flag)
4. **Error on the safe side** (raise exception > silent truncation)
5. **Make it fast** (add performance only when needed)

**Building a good C++ Python binding is about translating not just values, but intentions.**

When a Python user writes code, they have Python expectations. Your binding is successful if those expectations are met reliably, safely, and clearly.

---

## References

- Python Data Model: https://docs.python.org/3/reference/datamodel.html
- Python C API: https://docs.python.org/3/c-api/
- PEP 20 - The Zen of Python: "explicit is better than implicit"
- Issue 55: Iterator Invalidation Detection (this project)
- Issue 53: Integer Overflow Checks (this project)

---

---

**Document Status:** Comprehensive guide with all 30 semantic rules covering C++/Python integration.  
**Last Updated:** March 4, 2026  
**Rules Covered:**
- 15 Core Rules (fundamental differences)
- 8 Struct-Specific Rules (struct proxy semantics)
- 7 Scalar-Specific Rules (primitive type handling)

**Total:** 30 rules organized by data type with implementation guidance, code examples, and audit status.

**Completeness:** All aspects of C++/Python binding semantics documented.  
**Next Review:** After implementing Issues 50, 52
