# Vector Element Proxy Invalidation (Issue 26)

## Problem Statement

When Python holds a proxy to a vector element and then appends to the same vector, the proxy can point to freed memory, causing crashes or data corruption.

---

## The Root Cause

### How Element Proxies Work (Current Implementation)

When you access a vector element in Python:
```python
enemy = cpp.enemies[0]
```

The proxy stores a **raw pointer** to the element:
```cpp
void *elemPtr = proxy->bound->element_ptr(index);  // Get pointer to enemies[0]
BoundStruct *bstruct = new BoundStruct(vec->name, elemPtr, sinfo);
return StructProxy_New(bstruct);  // Proxy stores elemPtr
```

### Why This Is Unsafe

`std::vector` may **reallocate** its internal buffer when it grows:
- Old buffer is freed
- Elements move to a new address
- **Existing pointers become invalid**

---

## Concrete Example with Memory Addresses

### Step 1: Initial State
```
C++ Memory:
┌────────────────────────────────────────────┐
│ std::vector<Enemy> enemies                 │
├────────────────────────────────────────────┤
│ capacity: 2                                │
│ size: 2                                    │
│ buffer: 0x1000                             │
│   [0] Enemy {health=50}  at 0x1000        │
│   [1] Enemy {health=75}  at 0x1008        │
└────────────────────────────────────────────┘
```

### Step 2: Python Gets Proxy
```python
enemy = cpp.enemies[0]
```

```
Python Proxy:
┌──────────────────────────┐
│ enemy (StructProxy)      │
├──────────────────────────┤
│ bound->m_instance = 0x1000  ← Points to enemies[0]
└──────────────────────────┘
```

### Step 3: Append Triggers Reallocation
```python
cpp.enemies.append_new()
```

Vector is full (size == capacity), so it reallocates:

```
BEFORE:
┌────────────────────────────────────────────┐
│ buffer at 0x1000                           │
│   [0] Enemy at 0x1000                      │
│   [1] Enemy at 0x1008                      │
└────────────────────────────────────────────┘

AFTER (reallocated to new buffer):
┌────────────────────────────────────────────┐
│ NEW buffer at 0x2000                       │
│   [0] Enemy at 0x2000  ← moved!           │
│   [1] Enemy at 0x2008                      │
│   [2] Enemy at 0x2010  ← new element      │
└────────────────────────────────────────────┘

OLD buffer at 0x1000:
┌────────────────────────────────────────────┐
│ ❌ FREED MEMORY                            │
└────────────────────────────────────────────┘
```

### Step 4: Using Old Proxy Causes Crash
```python
enemy.health = 999  # ❌ Writes to freed memory at 0x1000
```

```
Python Proxy (unchanged):
┌──────────────────────────┐
│ enemy (StructProxy)      │
├──────────────────────────┤
│ bound->m_instance = 0x1000  ← Still points to OLD address!
└──────────────────────────┘
         │
         ↓
    Writes to 0x1000
         │
         ↓
    ❌ FREED MEMORY
    (use-after-free → crash)
```

---

## Impact

### Possible Outcomes
1. **Hard crash** (segmentation fault / access violation)
2. **Silent data corruption** (writes go to wrong memory)
3. **Intermittent bugs** (works sometimes, crashes later)

### Risk Factors
- More likely with vectors that grow frequently
- Hard to debug (timing-dependent)
- Can corrupt unrelated data structures

---

## Solution Options

### Option A: Document Limitation (Quick Fix)
**What:** Add clear warning in documentation

**Pros:**
- No code changes
- Fast to implement

**Cons:**
- Still unsafe if users ignore warning
- Easy to misuse

**Usage Pattern:**
```python
# ✅ Safe: Don't keep element proxy after append
cpp.enemies[0].health = 100  # OK (temporary proxy)

# ❌ Unsafe: Keeping proxy across append
enemy = cpp.enemies[0]
cpp.enemies.append_new()
enemy.health = 100  # CRASH or corruption
```

---

### Option B: Store Index + Parent Vector (Safe Fix) ⭐ RECOMMENDED

**What:** Instead of storing raw element pointer, store:
- Pointer to parent `BoundVector`
- Element index

Then resolve pointer **on each access**.

**Architecture Change:**

**BEFORE (current - unsafe):**
```cpp
// StructProxy for vector element
struct StructProxyObject {
    PyObject_HEAD
    BoundStruct *bound;  // contains raw pointer to element
};
```

**AFTER (safe):**
```cpp
// Add parent tracking to BoundStruct
class BoundStruct {
    void *m_instance;           // Raw pointer (for non-vector elements)
    const StructInfo *m_info;
    
    // NEW: For vector elements
    BoundVector *m_parent_vector;  // nullptr if not from vector
    std::size_t m_element_index;   // valid only if m_parent_vector != nullptr
    
    void *get_instance() const {
        if (m_parent_vector) {
            // Resolve pointer dynamically from current vector state
            return m_parent_vector->element_ptr(m_element_index);
        }
        return m_instance;  // Static pointer for non-vector structs
    }
};
```

**Key Changes:**
1. `BoundStruct` tracks parent vector + index (if element came from vector)
2. `get_instance()` resolves pointer on-demand
3. All field access goes through `get_instance()` instead of raw pointer

**Pros:**
- Safe against vector reallocation
- Transparent to Python code
- Only resolves when accessed

**Cons:**
- Small overhead (one pointer lookup per access)
- Requires refactor of `BoundStruct` / field access

**Safety:**
```python
enemy = cpp.enemies[0]       # Stores index=0 + parent vector
cpp.enemies.append_new()     # Vector reallocates
enemy.health = 100           # ✅ Safe: resolves new address from index
```

---

### Option C: Prevent Append While Proxies Exist (Strict)

**What:** Track active element proxies and block `append()` if any are live

**Pros:**
- Prevents use-after-free completely

**Cons:**
- More complex (need reference counting)
- Less flexible (restrictive API)
- Harder to implement correctly

---

## Recommendation

**Implement Option B** for production safety.

It provides:
- ✅ Safety against reallocation
- ✅ No user training required
- ✅ Minimal performance impact
- ✅ Clean abstraction

**Short-term workaround:** Add Option A (documentation warning) until Option B is implemented.

---

## Implementation Guide (Option B)

See separate implementation plan in this document's appendix or in code comments.

---

## Related Issues

- Issue 18: Wrapper ownership pattern (similar pointer lifetime concerns)
- Issue 19: Type-punning in vector creation

---

## References

- C++ Standard: `std::vector` reallocation guarantee (§23.3.11.5)
- Python C API: Object lifetime management
- `python_proxy.cpp`: Current proxy implementation
