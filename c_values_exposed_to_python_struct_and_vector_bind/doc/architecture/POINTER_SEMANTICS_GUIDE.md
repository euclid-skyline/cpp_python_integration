# Pointer Semantics Guide: Metadata Pointers vs Data Pointers

## Table of Contents

- [Overview](#overview)
- [Core Concept: Two Memory Regions](#core-concept-two-memory-regions)
- [Metadata Pointers (Static, Never Move)](#metadata-pointers-static-never-move)
  - [What Is Metadata?](#what-is-metadata)
  - [Memory Location: Static Storage](#memory-location-static-storage)
  - [Metadata Examples](#metadata-examples)
- [Data Pointers (Dynamic, Can Move)](#data-pointers-dynamic-can-move)
  - [What Is Data?](#what-is-data)
  - [Memory Location: Heap (Dynamic)](#memory-location-heap-dynamic)
  - [Vector Reallocation Example](#vector-reallocation-example)
- [Visual Comparison: What Moves vs What Stays](#visual-comparison-what-moves-vs-what-stays)
- [Complete Lifecycle Example](#complete-lifecycle-example)
- [Why This Distinction Matters](#why-this-distinction-matters)
- [Design Implications](#design-implications)
  - [Why Metadata Can Be const void*](#why-metadata-can-be-const-void)
  - [Why Data Pointers Need Dynamic Resolution](#why-data-pointers-need-dynamic-resolution)
- [Common Misconceptions](#common-misconceptions)
- [Summary Table](#summary-table)
- [Further Reading](#further-reading)

---

## Overview

This guide clarifies the critical distinction between **metadata pointers** and **data pointers** in the reflection system. Understanding this difference is essential for:
- Making safe design decisions about pointer mutability
- Understanding why `element_meta` can be `const void*`
- Understanding why element pointers must be resolved dynamically
- Avoiding dangling pointer bugs in vector-based structures

**Target Audience:** Developers working with the reflection system, implementing new type support, or debugging memory issues.

**Key Insight:** The system uses two separate pointer systems that live in different memory regions and have completely different lifetimes and mutability requirements.

---

[Back to Table of Contents](#table-of-contents)

## Core Concept: Two Memory Regions

Think of your program's memory as having two distinct zones:

```
┌─────────────────────────────────────────────────────────────┐
│  STATIC MEMORY (Program Code + Static Data)                 │
│  • Compiled functions                                       │
│  • Static/global variables                                  │
│  • METADATA (StructInfo, VectorInfo, etc.)                  │
│  • String literals                                          │
│                                                             │
│  Characteristics:                                           │
│    ✓ Fixed addresses (set by linker)                        │
│    ✓ Lifetime = entire program                              │
│    ✓ NEVER moves or reallocates                             │
│    ✓ Read-only or initialized once                          │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│  HEAP MEMORY (Dynamic Allocation)                           │
│  • Objects created with new/malloc                          │
│  • Vector internal arrays                                   │
│  • Dynamic strings                                          │
│  • Resizable containers                                     │
│                                                             │
│  Characteristics:                                           │
│    ✓ Addresses assigned at runtime                          │
│    ✓ Lifetime = explicit deallocation                       │
│    ✓ CAN move (realloc, vector growth)                      │
│    ✓ Frequently modified                                    │
└─────────────────────────────────────────────────────────────┘
```

**Key Insight:**
- **Metadata** lives in **static memory** (fixed addresses)
- **Data** lives in **heap** (addresses can change)

---

[Back to Table of Contents](#table-of-contents)

## Metadata Pointers (Static, Never Move)

### What Is Metadata?

**Metadata** is information that describes **types** and their structure—it tells you what a type looks like, not what its values are.

```cpp
// This is METADATA (type description, not actual values)
static StructInfo EnemyInfo = {
    "Enemy",                                                     // Type name
    {
        {"health", offsetof(Enemy, health), ValueType::Int, nullptr},
        {"x", offsetof(Enemy, x), ValueType::Float, nullptr},
    }
};
```

**What metadata tells us:**
- A type called "Enemy" exists
- It has 2 fields: "health" (int at offset 0) and "x" (float at offset 4)
- How to interpret bytes as Enemy objects

**What metadata does NOT tell us:**
- Where any actual Enemy objects are stored in memory
- What specific health/x values any enemy has
- How many enemies exist

**Analogy:** Metadata is like a **recipe** in a cookbook—it describes how to make a dish (type structure), but doesn't contain actual food (values).

---

### Memory Location: Static Storage

Static metadata is embedded in the executable's data segment with fixed addresses:

```
Memory Address    Content
────────────────────────────────────────────────
0x00400000        [Program Machine Code]
                  (executable instructions)

0x00401000        [Static Data Segment]:
                  
0x00401100          EnemyInfo struct (METADATA)
                      .name = "Enemy"
                      .fields[0].name = "health"
                      .fields[0].offset = 0
                      .fields[0].type = Int
                      .fields[1].name = "x"
                      .fields[1].offset = 4
                      .fields[1].type = Float

0x00402000          IntVectorInfo struct (METADATA)
                      .element_type = Int
                      .element_meta = nullptr
                      .size_fn = 0x00403100
                      .element_ptr_fn = 0x00403200
                      ...

0x00403000        [Function Pointer Table]
0x00403100          generic_vec_size<int>
0x00403200          generic_vec_element_ptr<int>
                  ...
```

**Critical Properties:**
- ✅ Addresses assigned by **linker** (not runtime allocator)
- ✅ `&EnemyInfo` is **always `0x00401100`** (never changes)
- ✅ Lifetime = **entire program** (created before `main()`, destroyed after `main()`)
- ✅ Accessing metadata is **always safe** (fixed, valid addresses)

---

### Metadata Examples

#### Example 1: StructInfo Reference

```cpp
static StructInfo EnemyInfo = { "Enemy", { /* fields */ } };

// These pointers NEVER change:
const StructInfo* ptr1 = &EnemyInfo;  // 0x00401100 (forever)

// ... 1 million vector operations later ...

const StructInfo* ptr2 = &EnemyInfo;  // Still 0x00401100

// ptr1 == ptr2  ✓ Always true
```

#### Example 2: VectorInfo.element_meta Reference

```cpp
static StructInfo EnemyInfo = { "Enemy", { /* fields */ } };

static VectorInfo EnemyVectorInfo = {
    ValueType::Struct,
    &EnemyInfo,  // ← METADATA POINTER = 0x00401100 (fixed)
    // ...
};

// This pointer in the struct never changes:
void* meta = EnemyVectorInfo.element_meta;  // 0x00401100

// Even after creating/destroying thousands of vectors:
std::vector<Enemy> v1;  // Create
v1.push_back(...);      // Grow
v1.clear();             // Clear
// ... repeat 1000 times ...

// element_meta still points to same address:
void* meta_later = EnemyVectorInfo.element_meta;  // Still 0x00401100
// meta == meta_later  ✓ Always true
```

---

[Back to Table of Contents](#table-of-contents)

## Data Pointers (Dynamic, Can Move)

### What Is Data?

**Data** is the **actual values**—real objects with their content stored in memory.

```cpp
// This is DATA (actual values with real content)
std::vector<Enemy> enemies;
enemies.push_back(Enemy{100, 1.0f});  // Real enemy: health=100, x=1.0
enemies.push_back(Enemy{80, 2.0f});   // Real enemy: health=80, x=2.0

// Accessing actual values:
int health = enemies[0].health;  // Reads from heap memory
enemies[1].x = 3.0f;             // Writes to heap memory
```

**What data contains:**
- Actual integer value `100` for health
- Actual float value `1.0f` for x
- The real bytes representing the Enemy object

**Analogy:** Data is like **actual ingredients** in your refrigerator—the real carrots, onions, and meat (values).

---

### Memory Location: Heap (Dynamic)

Data is allocated on the heap at runtime with addresses determined by the allocator:

#### Initial State (Empty Vector):

```
Memory Address    Content
────────────────────────────────────────────────
STACK (fixed location):
─────────────────────────────────────────
0x7FFF0000        enemies (std::vector object)
                    .data = nullptr
                    .size = 0
                    .capacity = 0

HEAP (allocated memory):
─────────────────────────────────────────
[Empty - no allocation yet]
```

#### After First `push_back(Enemy{100, 1.0f})`:

```
Memory Address    Content
────────────────────────────────────────────────
STACK:
─────────────────────────────────────────
0x7FFF0000        enemies
                    .data = 0x20000000  ← DATA POINTER (heap)
                    .size = 1
                    .capacity = 1

HEAP (NEW allocation):
─────────────────────────────────────────
0x20000000        Enemy[0] { health=100, x=1.0 }  ← ACTUAL DATA
```

#### After Second `push_back(Enemy{80, 2.0f})` — REALLOCATION!

```
Memory Address    Content
────────────────────────────────────────────────
STACK:
─────────────────────────────────────────
0x7FFF0000        enemies
                    .data = 0x30000000  ← DATA POINTER CHANGED!
                    .size = 2
                    .capacity = 2

HEAP:
─────────────────────────────────────────
0x20000000        [FREED/INVALID MEMORY] ← Old array deleted!
                  ⚠️ Don't access this anymore!

0x30000000        Enemy[0] { health=100, x=1.0 }  ← Copied from old
0x30000008        Enemy[1] { health=80, x=2.0 }   ← New element
```

**Critical Properties:**
- ⚠️ Addresses assigned by **runtime allocator** (not fixed)
- ⚠️ `enemies.data` changed: `0x20000000` → `0x30000000`
- ⚠️ Old address `0x20000000` is now **invalid** (freed memory)
- ⚠️ Any cached pointer to old address is **dangling** (crash if used)

---

### Vector Reallocation Example

```cpp
std::vector<Enemy> enemies;

// Step 1: Add first enemy
enemies.push_back(Enemy{100, 1.0f});
Enemy* ptr1 = &enemies[0];  // ptr1 = 0x20000000 (valid now)

std::cout << "Address before: " << ptr1 << std::endl;
// Output: "Address before: 0x20000000"

// Step 2: Add second enemy (triggers reallocation)
enemies.push_back(Enemy{80, 2.0f});  // Capacity exceeded!
                                     // Vector allocates new array
                                     // Copies old elements
                                     // Frees old array

Enemy* ptr2 = &enemies[0];  // ptr2 = 0x30000000 (NEW address)

std::cout << "Address after: " << ptr2 << std::endl;
// Output: "Address after: 0x30000000"

// ptr1 is now DANGLING (points to freed memory)
// int health = ptr1->health;  // ⚠️ UNDEFINED BEHAVIOR (crash/corruption)

// ptr2 is VALID (points to current location)
int health = ptr2->health;  // ✓ Safe: 100
```

**Why reallocation happens:**
1. Vector starts with capacity=1 (space for 1 element)
2. First `push_back()` fills the array (size=1, capacity=1)
3. Second `push_back()` needs more space (size=2, but capacity=1)
4. Vector allocates new array with capacity=2
5. Copies old elements to new array
6. Frees old array (now invalid)
7. Updates internal pointer to new array

---

[Back to Table of Contents](#table-of-contents)

## Visual Comparison: What Moves vs What Stays

### Scenario: Add Elements to Vector

```cpp
// Setup
static StructInfo EnemyInfo = { ... };  // METADATA (static)
std::vector<Enemy> enemies;              // DATA (dynamic)

Enemy* data_ptr = &enemies[0];           // Points to data (heap)
const StructInfo* meta_ptr = &EnemyInfo; // Points to metadata (static)

// Trigger reallocation
enemies.push_back(...);
enemies.push_back(...);

// Check after reallocation
Enemy* data_ptr_new = &enemies[0];           // NEW address
const StructInfo* meta_ptr_new = &EnemyInfo; // SAME address
```

### Comparison Table

| Pointer Type | Before Reallocation | After Reallocation | Changed? | Safe to Use Old Pointer? |
|--------------|---------------------|--------------------|-----------|-----------------------|
| `data_ptr` (to actual enemy) | `0x20000000` | Still `0x20000000` | Value unchanged | ❌ **NO** (freed memory) |
| `data_ptr_new` (fresh pointer) | Did not exist | `0x30000000` | New pointer | ✅ YES (valid) |
| `meta_ptr` (to EnemyInfo) | `0x00401100` | `0x00401100` | Unchanged | ✅ YES (always valid) |
| `meta_ptr_new` (same metadata) | `0x00401100` | `0x00401100` | Unchanged | ✅ YES (always valid) |

**Key Observation:**
- ✅ Metadata pointer **never changes** (`0x00401100` forever)
- ⚠️ Data pointer **becomes invalid** after reallocation

---

[Back to Table of Contents](#table-of-contents)

## Complete Lifecycle Example

Let's trace both pointer types through a complete program lifecycle.

### T=0: Program Startup (Before main())

```
STATIC MEMORY (Fixed addresses, set by linker):
═════════════════════════════════════════════════════════
Address      Variable              Content
─────────────────────────────────────────────────────────
0x00401100   EnemyInfo             METADATA:
                                     .name = "Enemy"
                                     .fields[0] = {"health", 0, Int, nullptr}
                                     .fields[1] = {"x", 4, Float, nullptr}

0x00402000   EnemyVectorInfo       METADATA:
                                     .element_type = Struct
                                     .element_meta = 0x00401100 ← Points to EnemyInfo
                                     .size_fn = 0x00403100
                                     ...

STACK (when main() starts):
═════════════════════════════════════════════════════════
0x7FFF0000   enemies (vector obj)  DATA CONTAINER:
                                     .data = nullptr
                                     .size = 0
                                     .capacity = 0

HEAP:
═════════════════════════════════════════════════════════
[Empty - no allocations yet]
```

**Metadata pointers at T=0:**
- `&EnemyInfo` = `0x00401100` ✓
- `EnemyVectorInfo.element_meta` = `0x00401100` ✓

---

### T=1: First push_back(Enemy{100, 1.0f})

```
STATIC MEMORY (UNCHANGED):
═════════════════════════════════════════════════════════
0x00401100   EnemyInfo             METADATA (same as before)
0x00402000   EnemyVectorInfo       .element_meta = 0x00401100 ← UNCHANGED

STACK:
═════════════════════════════════════════════════════════
0x7FFF0000   enemies               DATA CONTAINER:
                                     .data = 0x20000000 ← NEW DATA POINTER
                                     .size = 1
                                     .capacity = 1

HEAP (NEW allocation):
═════════════════════════════════════════════════════════
0x20000000   Enemy[0]              DATA:
                                     .health = 100
                                     .x = 1.0
```

**What changed:**
- ✅ `enemies.data` changed: `nullptr` → `0x20000000`
- ❌ `EnemyVectorInfo.element_meta` unchanged: still `0x00401100`

**Pointer values at T=1:**
- Metadata: `&EnemyInfo` = `0x00401100` (same)
- Data: `&enemies[0]` = `0x20000000` (new)

---

### T=2: Second push_back(Enemy{80, 2.0f}) — REALLOCATION EVENT

**What happens internally:**
1. Vector checks: size=1, capacity=1 (no space!)
2. Allocate new array: capacity=2, address=`0x30000000`
3. Copy old elements: `Enemy{100, 1.0}` from `0x20000000` to `0x30000000`
4. Add new element: `Enemy{80, 2.0}` at `0x30000008`
5. Free old array: `0x20000000` deleted
6. Update pointer: `enemies.data` = `0x30000000`

```
STATIC MEMORY (STILL UNCHANGED):
═════════════════════════════════════════════════════════
0x00401100   EnemyInfo             METADATA (unchanged)
0x00402000   EnemyVectorInfo       .element_meta = 0x00401100 ← STILL UNCHANGED

STACK:
═════════════════════════════════════════════════════════
0x7FFF0000   enemies               DATA CONTAINER:
                                     .data = 0x30000000 ← DATA POINTER MOVED!
                                     .size = 2
                                     .capacity = 2

HEAP (OLD - FREED):
═════════════════════════════════════════════════════════
0x20000000   [INVALID MEMORY]      ⚠️ Freed! Don't access!

HEAP (NEW):
═════════════════════════════════════════════════════════
0x30000000   Enemy[0]              DATA (copied):
                                     .health = 100
                                     .x = 1.0

0x30000008   Enemy[1]              DATA (new):
                                     .health = 80
                                     .x = 2.0
```

**What changed:**
- ✅ `enemies.data` changed: `0x20000000` → `0x30000000`
- ✅ Old array freed: `0x20000000` is invalid
- ✅ New array allocated: `0x30000000` with data
- ❌ `EnemyVectorInfo.element_meta` unchanged: **STILL `0x00401100`**

**Pointer values at T=2:**
- Metadata: `&EnemyInfo` = `0x00401100` (never changed)
- Data: `&enemies[0]` = `0x30000000` (moved!)

---

[Back to Table of Contents](#table-of-contents)

## Why This Distinction Matters

### The Dangling Pointer Problem

```cpp
// ❌ BAD: Caching data pointer
class BadBoundStruct {
    Enemy* m_cached_ptr;  // Stores pointer to enemies[0]
};

BadBoundStruct bad_proxy;
bad_proxy.m_cached_ptr = &enemies[0];  // = 0x20000000 (valid now)

std::cout << bad_proxy.m_cached_ptr->health << std::endl;  // 100 ✓ Works

enemies.push_back(Enemy{80, 2.0f});  // Reallocation! Array moves to 0x30000000

// bad_proxy.m_cached_ptr still points to 0x20000000 (freed memory!)
std::cout << bad_proxy.m_cached_ptr->health << std::endl;  // ⚠️ CRASH or garbage!
//        ^^^^^^^^^^^^^^^^^^^^^^^^^^
//        Accessing freed memory = undefined behavior
```

**Why it crashes:**
1. Cached pointer stored old address (`0x20000000`)
2. Vector reallocated to new address (`0x30000000`)
3. Old address freed by allocator (no longer valid)
4. Accessing freed memory = crash, corruption, or security vulnerability

---

### The Safe Solution: Dynamic Resolution

```cpp
// ✅ GOOD: Don't cache data pointers, resolve fresh every time
class BoundStruct {
    BoundVector* m_parent_vector;   // Reference to container
    std::size_t m_element_index;    // Which element (index)
    
    void* instance() const {
        // Resolve pointer FRESH every time we need it
        return m_parent_vector->element_ptr(m_element_index);
    }
};

class BoundVector {
    void* m_vec_ptr;                // For top-level vectors
    BoundVector* m_parent_vector;   // For nested vectors
    std::size_t m_element_index;    // Element index if nested
    
    void* raw_vector() const {
        if (m_parent_vector) {
            // Nested: resolve from parent (in case parent reallocated)
            return m_parent_vector->element_ptr(m_element_index);
        }
        return m_vec_ptr;  // Top-level: address stored in variable
    }
};
```

**Why this works:**
```cpp
BoundStruct proxy(parent_vector, 0, &EnemyInfo);  // Stores: parent + index 0

enemies.push_back(...);  // Reallocation happens! Array moves.

// Later access:
void* ptr = proxy.instance();  // Calls parent_vector->element_ptr(0)
                               // Returns CURRENT address (0x30000000)
                               // Not old cached address (0x20000000)

int health = static_cast<Enemy*>(ptr)->health;  // ✓ Safe: uses fresh pointer
```

---

### Metadata Never Has This Problem

```cpp
// Metadata pointer NEVER becomes invalid
const StructInfo* meta = &EnemyInfo;  // 0x00401100

// ... 1 million vector operations ...

// Still valid (metadata never moved):
const char* type_name = meta->name.c_str();  // ✓ Always safe
size_t offset = meta->fields[0].offset;       // ✓ Always safe
```

**Why?** Static storage never reallocates or moves.

---

[Back to Table of Contents](#table-of-contents)

## Design Implications

### Why Metadata Can Be const void*

Since metadata pointers:
- ✅ Point to static storage (never moves)
- ✅ Are assigned once at initialization
- ✅ Never need to be modified at runtime

**They should be `const void*`:**

```cpp
// Current design:
struct VectorInfo {
    void *element_meta;  // Mutable (but never actually modified)
};

// Better design:
struct VectorInfo {
    const void *element_meta;  // Immutable (matches actual usage)
};
```

**Benefits of making it const:**
1. ✅ **Documents intent:** Metadata is read-only type information
2. ✅ **Type safety:** Compiler prevents accidental modification
3. ✅ **Simpler code:** Eliminates need for `const_cast` helpers like `get_vector_info_ptr()`
4. ✅ **Zero runtime cost:** `const` is a compile-time constraint only

**Side effects:** ❌ None—metadata is never modified in practice

---

### Why Data Pointers Need Dynamic Resolution

Since data pointers:
- ⚠️ Point to heap memory (can move during reallocation)
- ⚠️ Become invalid when vectors grow
- ⚠️ Must be refreshed on every access

**They MUST use dynamic resolution:**

```cpp
// ❌ DON'T cache data pointers:
struct BadProxy {
    Enemy* cached_ptr;  // Becomes invalid after reallocation
};

// ✅ DO resolve fresh every time:
struct GoodProxy {
    BoundVector* parent;      // Reference to container
    std::size_t index;        // Element index
    
    void* get_ptr() {
        return parent->element_ptr(index);  // Fresh pointer every call
    }
};
```

**This is why:**
- `BoundStruct::instance()` is a **function** (not a cached member)
- `BoundVector::raw_vector()` is a **function** (not a cached member)

---

[Back to Table of Contents](#table-of-contents)

## Common Misconceptions

### Misconception 1: "All void* pointers need to be mutable"

**Truth:** Only pointers to **heap data** change. Pointers to **static metadata** never change.

```cpp
// Metadata pointer (static):
void* meta = &EnemyInfo;  // Never changes → can be const void*

// Data pointer (heap):
void* data = &enemies[0]; // Changes after reallocation → needs dynamic resolution
```

---

### Misconception 2: "Making element_meta const breaks vector reallocation"

**Truth:** `element_meta` points to **metadata** (type description), not actual data. Vector reallocation moves **data**, not metadata.

```cpp
// Metadata (type description) - never moves:
static VectorInfo info = {
    .element_meta = &EnemyInfo  // Always 0x00401100
};

// Data (actual values) - moves during reallocation:
std::vector<Enemy> enemies;     // .data pointer changes
```

---

### Misconception 3: "Metadata and data pointers are the same thing"

**Truth:** They serve completely different purposes:

| Aspect | Metadata Pointer | Data Pointer |
|--------|-----------------|--------------|
| **Points to** | Type description (StructInfo, VectorInfo) | Actual values (Enemy objects, int values) |
| **Lives in** | Static memory (fixed by linker) | Heap memory (allocated at runtime) |
| **Lifetime** | Entire program | Until deleted or reallocated |
| **Can move?** | ❌ Never | ✅ Yes (vector growth) |
| **Used for** | Type introspection (what fields exist?) | Value access (what is the health value?) |

---

[Back to Table of Contents](#table-of-contents)

## Summary Table

### Metadata Pointers vs Data Pointers

| Characteristic | Metadata Pointers | Data Pointers |
|----------------|-------------------|---------------|
| **Example fields** | `VectorInfo.element_meta`, `FieldInfo.type_meta` | `BoundStruct.m_instance`, `BoundVector.m_vec_ptr` |
| **Points to** | StructInfo, VectorInfo (type descriptions) | std::vector<T>, struct instances (actual values) |
| **Memory region** | Static/global (data segment) | Heap (dynamically allocated) |
| **Allocated by** | Linker (compile/link time) | Runtime allocator (new/malloc) |
| **Address determined** | Compile/link time | Runtime |
| **Address stability** | ✅ Fixed forever | ⚠️ Can change (reallocation) |
| **Lifetime** | Entire program (static storage duration) | Until explicitly deleted |
| **Can be modified?** | ❌ No (type info is immutable) | ✅ Yes (values change) |
| **Can pointer move?** | ❌ Never | ✅ Yes (vector growth, realloc) |
| **Safe to cache?** | ✅ Yes (always valid) | ❌ No (invalidated by reallocation) |
| **Should be const?** | ✅ Yes (`const void*`) | ❌ No (needs dynamic resolution) |
| **Used for** | Type introspection, reflection | Accessing/modifying values |
| **Example usage** | `meta->fields[0].name` (what fields exist?) | `data->health = 50` (set value) |

---

### Pointer Behavior During Vector Operations

| Event | Metadata Pointer Behavior | Data Pointer Behavior |
|-------|--------------------------|----------------------|
| **Create vector** | ✅ Unchanged (still points to static metadata) | ✅ Set to heap allocation |
| **Add element (no realloc)** | ✅ Unchanged | ✅ Still valid (same address) |
| **Add element (triggers realloc)** | ✅ Unchanged | ⚠️ INVALIDATED (points to freed memory) |
| **Access element** | ✅ Unchanged | ⚠️ Must resolve fresh (don't use cached) |
| **Clear vector** | ✅ Unchanged | ⚠️ Elements freed |
| **Destroy vector** | ✅ Unchanged | ⚠️ All memory freed |

---

[Back to Table of Contents](#table-of-contents)

## Design Implications

### Safe Design Pattern: Separation of Concerns

```cpp
// ✅ CORRECT: Metadata stored statically, data resolved dynamically

// 1. Metadata lives in STATIC storage (never changes)
static StructInfo EnemyInfo = { "Enemy", { /* fields */ } };
static VectorInfo EnemyVectorInfo = {
    .element_meta = &EnemyInfo  // ← Points to static metadata (safe to cache)
};

// 2. Data lives in HEAP (can move)
std::vector<Enemy> enemies;

// 3. Proxies DON'T cache data pointers
class BoundStruct {
    BoundVector* m_parent;      // ✓ Reference to container
    std::size_t m_element_index; // ✓ Index (not pointer)
    
    void* instance() const {
        return m_parent->element_ptr(m_element_index);  // ✓ Fresh resolution
    }
};
```

**Result:**
- Metadata access: Always safe (fixed addresses)
- Data access: Always safe (fresh pointers)
- Zero dangling pointer bugs

---

### Unsafe Anti-Pattern: Mixing Concerns

```cpp
// ❌ INCORRECT: Caching data pointers leads to bugs

class BadProxy {
    Enemy* m_cached_data_ptr;  // Points to heap memory
};

BadProxy bad;
bad.m_cached_data_ptr = &enemies[0];  // 0x20000000 (valid now)

enemies.push_back(...);  // Reallocation! Array moves to 0x30000000

// Cached pointer still points to old address (freed memory)
int health = bad.m_cached_data_ptr->health;  // ⚠️ CRASH!
```

---

[Back to Table of Contents](#table-of-contents)

## Real-World Analogy: Recipe Book vs Ingredients

### Metadata = Recipe Book

- **Physical book on bookshelf** (static storage)
- Page 42 contains "Enemy Recipe"
- Page number **never changes**
- Recipe describes **structure** (what ingredients, how to combine)
- Recipe does NOT contain actual food

```cpp
static StructInfo EnemyInfo = { "Enemy", { /* structure */ } };
// Address of EnemyInfo = "Page 42" (never moves)
```

### Data = Actual Ingredients in Refrigerator

- **Carrots, onions in fridge** (heap storage)
- Location **can change** (reorganize fridge)
- Ingredients are **actual values** (real food)
- Ingredients can spoil, be consumed, replaced

```cpp
std::vector<Enemy> enemies = {{ 100, 1.0f }, { 80, 2.0f }};
// addresses of enemies[0], enemies[1] = ingredient locations (can move)
```

### Using Both Together

```cpp
// Look up recipe (metadata - page 42, never changes):
const StructInfo* recipe = &EnemyInfo;  // Always valid

// Get ingredients from fridge (data - can move):
void* ingredient = enemies.raw_vector();  // Must check current location

// Follow recipe with current ingredients:
char* field_ptr = static_cast<char*>(ingredient) + recipe->fields[0].offset;
int* health = reinterpret_cast<int*>(field_ptr);
```

**When you reorganize your fridge (vector reallocation):**
- ❌ Recipe page number doesn't change (metadata pointer stays valid)
- ✅ Ingredient location changes (data pointer must be updated)

---

[Back to Table of Contents](#table-of-contents)

## Further Reading

### In This Project

- **ARCHITECTURE_DEEP_DIVE.md** — Section II: Type-Erasure Pattern (discusses void* design)
- **PARENT_TRACKING_IMPLEMENTATION_GUIDE.md** — Section: Dynamic Resolution Pattern (explains why instance() is a function)
- **VECTOR_ELEMENT_PROXY_INVALIDATION.md** — Complete analysis of the reallocation problem
- **OWNERSHIP_MODELS_GUIDE.md** — Section 4: Complex Type Ownership (proxy lifetime management)
- **INTRODUCTORY_CONCEPTS.md** — Type Erasure Pattern section

### Related Concepts

- **Static vs Dynamic Memory:** C++ memory model fundamentals
- **Vector Reallocation:** How `std::vector` manages capacity
- **Type Erasure:** Hiding type information behind void*
- **Parent Tracking:** Lazy pointer resolution pattern

### External References

- C++ Standard — Static storage duration: https://en.cppreference.com/w/cpp/language/storage_duration
- C++ Standard — std::vector capacity and reallocation: https://en.cppreference.com/w/cpp/container/vector
- "Effective C++" by Scott Meyers — Item 8: Prevent exceptions from leaving destructors
- C++ Core Guidelines — R.3: A raw pointer (a T*) is non-owning

---

[Back to Table of Contents](#table-of-contents)
