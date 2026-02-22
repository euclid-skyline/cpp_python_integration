# Design Patterns, Trade-offs, and Extensibility Guide

## I. Core Design Patterns Used

### Pattern 1: Type-Erasure with void* Pointers

**Definition:** Store opaque void* pointers to typed data, use enums/metadata to track actual types.

**Where Used:**
- `BoundStruct::m_instance` – void* to any user-defined struct
- `BoundVector::m_vec_ptr` – void* to any std::vector<T>
- `FieldInfo::type_meta` – void* to StructInfo or VectorInfo
- All field pointer calculations in reflection layer

**Why This Pattern:**

```cpp
// ❌ Alternative 1: C++ Templates
template<typename T>
class BoundValue {
    T *m_value;  // Requires template instantiation for each type
    // Problem: Can't store different types in same collection
    // std::vector<BoundValue<int>, BoundValue<Player>> won't compile
};

// ❌ Alternative 2: Virtual Methods
class BoundValue {
    virtual int *as_int() = 0;      // Overload for each type
    virtual Player *as_struct() = 0;
    // Problem: 2N virtual functions for N types
    // Violates Open/Closed Principle (adding type = modify base class)
};

// ✓ Chosen: enums + void* (Type-Erasure)
class BoundValue {
    ValueType type;    // Runtime indicator
    void *m_value;     // Opaque storage
    // Advantage: Single storage, type indicated separately
};
```

**Trade-offs:**

| Aspect | void* | Templates | Virtual |
|--------|-------|-----------|---------|
| Compile Time | Fast | Slow (code bloat) | Medium |
| Runtime Cost | Minimal | None | Vtable lookup |
| Extensibility | Easy (users add types) | Hard (recompilation) | Medium (virtual methods) |
| Type Safety | None (casts needed) | Full | Full |
| Collection | ✓ Works | ✗ Doesn't work | ✓ Works |

**Why void* is Right Here:**
- User code provides types we don't know at library compile-time
- Need to store different types together (PyInterface::g_values map)
- Fast compilation (no template bloat)
- Pattern used by: Linux kernel, GTK+, Qt (internally)

---

### Pattern 2: Function Pointers for Type-Specific Operations

**Definition:** Instead of virtual methods on containers, store function pointers encoding how to operate on a specific type.

**Where Used:**
```cpp
struct VectorInfo {
    std::size_t (*size_fn)(void *vec_ptr);
    void *(*element_ptr_fn)(void *vec_ptr, std::size_t index);
    bool (*append_fn)(void *vec_ptr, void *value_ptr);
};
```

**Example Implementation:**
```cpp
// For std::vector<int>
VectorInfo scores_info{
    ValueType::Int,
    nullptr,
    // size_fn: How to get size of std::vector<int>
    [](void *ptr) { return static_cast<std::vector<int>*>(ptr)->size(); },
    // element_ptr_fn: How to get element pointer
    [](void *ptr, size_t i) { 
        auto *v = static_cast<std::vector<int>*>(ptr);
        return &(*v)[i];  
    },
    // append_fn: How to append
    [](void *ptr, void *value) {
        auto *v = static_cast<std::vector<int>*>(ptr);
        v->push_back(*(int*)value);
        return true;
    }
};

// For std::vector<Player>
VectorInfo enemies_info{
    ValueType::Struct,
    &player_struct_info,  // Points to metadata about Player
    // size_fn: Different function!
    [](void *ptr) { return static_cast<std::vector<Player>*>(ptr)->size(); },
    // element_ptr_fn: Different function!
    [](void *ptr, size_t i) {
        auto *v = static_cast<std::vector<Player>*>(ptr);
        return &(*v)[i];
    },
    // append_fn: Different function!
    [](void *ptr, void *value) {
        auto *v = static_cast<std::vector<Player>*>(ptr);
        v->push_back(*(Player*)value);
        return true;
    }
};
```

**Why Function Pointers Instead of Virtual Methods?**

```cpp
// ❌ Virtual Method Approach
class Container {
    virtual size_t size() = 0;
    virtual void *element(size_t i) = 0;
};

class VectorInt : public Container { ... };
class VectorPlayer : public Container { ... };

// Problem: std::vector<int> is already a complete type!
// Can't add virtual methods without inheritance
// Would need a wrapper class: defeats purpose of using std::vector

// ✓ Function Pointers Approach
// Metadata stands beside std::vector<int>, doesn't modify it
// Works with any container (vector, deque, list, custom)
```

**Why This Pattern Scales:**

```cpp
// For Lua binding (hypothetical):
void *element_ptr = BoundVector::element_ptr(index);
lua_pushinteger(L, *(int*)element_ptr);  // Use same element_ptr!

// For Ruby binding (hypothetical):
void *element_ptr = BoundVector::element_ptr(index);
VALUE rbint = LONG2NUM(*(int*)element_ptr);  // Ruby C-API

// All use the same BoundVector::element_ptr()
// Only the conversion to language-specific type differs
```

---

### Pattern 3: Offset-Based Field Access (Memory Intrusion)

**Definition:** Calculate field memory addresses using compile-time offsets, enabling direct memory access without copying.

**Where Implemented:**
```cpp
// reflection_struct.hpp
void *BoundStruct::get_field_ptr(const FieldInfo *f) {
    return (void*)(static_cast<char*>(m_instance) + f->offset);
}
```

**How It Works:**
```cpp
struct Player {
    int health;        // offset 0
    float experience;  // offset 4
    std::string name;  // offset ~8-12 (depends on std::string size)
};

// At compile-time, user provides:
offsetof(Player, health) → 0
offsetof(Player, experience) → 4
offsetof(Player, name) → 8

// At runtime:
Player *instance = ...;
void *field_ptr = instance + offset;  // Pointer arithmetic
// field_ptr now points to the actual field in memory!
```

**Key Advantage: Zero-Copy**

```cpp
// Without offset-based access (copying):
Player player = {...};
// Copy to temporary for Python
PlayerDTO dto = { player.health, player.experience, ... };
// Send DTO to Python
PyObject *py_player = convert_dto_to_python(dto);
// Total: O(n) copy where n = struct size

// With offset-based access (direct):
Player player = {...};
// No copy!
void *health_ptr = &player + offset_health;
PyObject *py_int = PyLong_FromLong(*(int*)health_ptr);
// Total: O(1) – pointer arithmetic only
```

**Memory Layout Visualization:**

```
Actual Memory:
┌─────────────────────────────────────┐
│ Player instance                     │
├─────────────────────────────────────┤
│ int health = 100  │ offset 0        │
├───────────────────┤                 │
│ float exp = 99.5  │ offset 4        │
├───────────────────┤                 │
│ std::string name  │ offset 8        │
│   (string buffer) │                 │
│                   │                 │
└─────────────────────────────────────┘

Metadata (StructInfo):
├─ "health": offset=0, type=Int
├─ "experience": offset=4, type=Float
└─ "name": offset=8, type=String

Python access:
python> cpp.player.health
  → void *ptr = instance + 0
  → *(int*)ptr = 100 ✓

python> cpp.player.experience
  → void *ptr = instance + 4
  → *(float*)ptr = 99.5 ✓

python> cpp.player.name
  → void *ptr = instance + 8
  → static_cast<std::string*>(ptr)->c_str() = "Alice" ✓
```

**Requirement: Standard Layout**

```cpp
// ✓ This works (standard layout):
struct Player {
    int health;
    std::string name;
    bool is_alive;
};

// ❌ This breaks (non-standard layout):
struct BadPlayer {
private:
    int health;      // Private – offset may not match
public:
    virtual void method() { }  // Virtual function changes layout
};
```

---

### Pattern 4: Compile-Time Dispatch with if constexpr

**Definition:** Use C++17 `if constexpr` to make dispatch decisions at compile-time, eliminating runtime branching.

**Where Implemented:**
```cpp
template <typename T>
static void bind(const std::string &name, T &variable) {
    if constexpr (is_reflected_struct<T>::value) {
        // Only compiled if is_reflected_struct<T> = true
        g_values[name] = std::make_unique<BoundStruct>(...);
    }
    else if constexpr (is_std_vector<T>::value) {
        // Only compiled for vector types
        g_values[name] = std::make_unique<BoundVector>(...);
    }
    else if constexpr (std::is_same_v<T, int>) {
        // Only compiled for int
        g_values[name] = std::make_unique<PyBoundInt>(...);
    }
}
```

**Compile-Time Optimization:**

```cpp
// When instantiating bind<Player>:
template<> void bind<Player>(...) {
    // Branch 1: Compiled
    if (true) {  // ← constexpr evaluates to true at compile-time
        g_values[name] = std::make_unique<BoundStruct>(...);
    }
    else if (false) {  // ← Branches 2-4 eliminated
        // Dead code – completely removed by compiler
    }
    // Result: Just the BoundStruct code, no other branches
}

// When instantiating bind<std::vector<int>>:
// ← Different instantiation, different branches compiled
```

**Why This is Better Than Runtime if:**

```cpp
// ❌ Runtime approach (slow):
void bind(const std::string &name, BoundValue *bound) {
    if (bound->type == BoundValue::STRUCT) {
        // Runtime check, every call
    } else if (bound->type == BoundValue::VECTOR) {
        // Runtime check, every call
    }
    // All branches in binary, CPU cache pressure
}

// ✓ Compile-time approach (fast):
template <typename T> void bind(...) {
    if constexpr (...) {  // Evaluated at compile-time
        // Only this branch in generated code for this instantiation
        // Other branches don't exist in binary
        // Perfect inlining opportunity for compiler
    }
}
```

**Benefits:**

1. **Zero Runtime Cost:** No CPU branch prediction needed
2. **Smaller Binary:** Dead branches eliminated
3. **Better Inlining:** Single branch enables aggressive inlining
4. **Type Safety:** Type errors caught at compile-time

---

## II. Design Trade-offs and Decisions

### Trade-off 1: Pointer Arithmetic vs. Container Wrapper

**Decision:** Use raw pointers (void**) + offset arithmetic

**Alternative:** Wrap std::vector in custom class

```cpp
// ❌ Wrapper approach:
template<typename T>
class ManagedVector {
    std::vector<T> m_vec;
public:
    T &at(size_t i) { return m_vec[i]; }
};

// Problem: std::vector<ManagedVector<int>> won't work!
// Problem: C++ code can't use original std::vector<int>
// Problem: Doubles complexity, template bloat

// ✓ Chosen: Metadata alongside original std::vector
std::vector<int> scores;  // C++ code uses as normal
BoundVector bound(&scores, &scores_info);  // Python gets metadata
```

**Trade-off Table:**

| Aspect | Wrapper | Raw + Metadata |
|--------|---------|----------------|
| C++ Code Changes | Moderate | None |
| Template Bloat | High | Low |
| Runtime Speed | Medium | Fast |
| Binding Complexity | Low | Low |
| User Adoption | Hard | Easy |

**Decision Rationale:** Minimizes user code changes, maximizes compatibility.

---

### Trade-off 2: Linear Field Lookup vs. Hash Map

**Decision:** Linear search in FieldInfo vector

```cpp
const FieldInfo *StructInfo::get_field() {
    for (const auto &field : fields) {  // O(n) search
        if (field.name == name) return &field;
    }
    return nullptr;
}
```

**Alternative:** Use unordered_map

```cpp
struct StructInfo {
    std::unordered_map<std::string, FieldInfo> fields_by_name;  // O(1)
};
```

**Trade-off Analysis:**

| Aspect | Linear | HashMap |
|--------|--------|---------|
| Memory Overhead | None | ~16 bytes per entry |
| Lookup Time | O(n) | O(1) average |
| Compile Complexity | Simple | Requires map impl |
| Cache Locality | Good | Bad (hash function) |
| Typical n | 5-20 fields | 5-20 |

**Decision Rationale:** 
- Structs typically have < 20 fields
- Linear search in cache: ~20 comparisons = ~1 CPU cycle
- Hash lookup on small number might actually be slower (hash function cost)
- Simpler code, less memory

**When to Switch:** If profiles show field lookup is bottleneck and structs have 100+ fields, switch to unordered_map.

---

### Trade-off 3: Single Binding Registry vs. Per-Type Registries

**Decision:** Single `PyInterface::g_values` map for all variables

```cpp
static std::unordered_map<std::string, std::unique_ptr<BoundValue>> g_values;
// Stores both structs, vectors, and scalars mixed together
```

**Alternative:** Separate registries

```cpp
static std::unordered_map<std::string, BoundStruct*> g_structs;
static std::unordered_map<std::string, BoundVector*> g_vectors;
static std::unordered_map<std::string, BoundValue*> g_scalars;
```

**Trade-off Analysis:**

| Aspect | Single | Multiple |
|--------|--------|----------|
| Lookup Complexity | 1 lookup | 3 lookups (unknown type) |
| Memory Overhead | Minimal | 3x map overhead |
| Type Dispatch | After lookup | Before lookup option |
| Code Clarity | Simple | Complex |
| Flexibility | High | Medium |

**Decision Rationale:**
- Single registry simplifies Python module (doesn't know types upfront)
- Type determined after lookup via BoundValue::type enum
- Enables dynamic variable discovery without preprocessing
- Supports Lua/Ruby/Perl with same registry

---

## III. Extensibility Framework

### How to Add a New Scalar Type

**Example: Adding long long support**

**Step 1:** Create type trait (in value_interface.hpp)

```cpp
template<>
struct is_type_supported<long long> : std::true_type {};
```

**Step 2:** Create binding class (in python_bind.hpp)

```cpp
class PyBoundLongLong : public PyBoundValue {
    long long *m_ptr;
    
    PyObject *to_python() override {
        return PyLong_FromLongLong(*m_ptr);
    }
    
    bool from_python(PyObject *obj) override {
        if (!PyLong_Check(obj)) return false;
        long long v = PyLong_AsLongLong(obj);
        if (PyErr_Occurred()) return false;
        *m_ptr = v;
        return true;
    }
};
```

**Step 3:** Add dispatch branch (in value_interface.hpp)

```cpp
else if constexpr (std::is_same_v<T, long long>) {
    g_values[name] = std::make_unique<PyBoundLongLong>(name, variable);
}
```

**Step 4:** Update Python proxy if needed (in cpp_module.cpp)

```cpp
case ValueType::LongLong:  // Add to enum
    // Handled by to_python() → PyLong_FromLongLong
    break;
```

**Total Changes:** ~40 lines across 3 files, no core logic changes.

---

### How to Add a New Struct Type

**Example: Adding Enemy struct support**

**Step 1:** Define struct with metadata (in main.cpp or separate header)

```cpp
struct Enemy {
    int level;
    std::string name;
};
```

**Step 2:** Create struct info (in main.cpp or separate file)

```cpp
template<>
struct is_reflected_struct<Enemy> : std::true_type {};

template<>
inline const StructInfo *get_struct_info<Enemy>() {
    static StructInfo info{
        "Enemy",
        {
            { "level", offsetof(Enemy, level), ValueType::Int, nullptr },
            { "name", offsetof(Enemy, name), ValueType::String, nullptr }
        }
    };
    return &info;
}
```

**Step 3:** Bind instance (in main.cpp main())

```cpp
Enemy boss{ 50, "Dragon" };
PyInterface::bind("boss", boss);
```

**That's It!** No other changes needed. Python automatically supports:
- `cpp.boss.level` (read/write)
- `cpp.boss.name` (read/write)
- Serialization to dict via getattro loop

**Total Changes:** ~20 lines, one file, no core logic changes.

---

### How to Add a New Vector Type

**Example: Adding vector of Weapon support**

**Step 1:** Define struct and metadata (same as above for Weapon)

**Step 2:** Create vector info (helper function in main.cpp)

```cpp
template<>
inline const VectorInfo *get_vector_info<Weapon>() {
    static VectorInfo info{
        ValueType::Struct,
        &weapon_struct_info,
        [](void *ptr) { return static_cast<std::vector<Weapon>*>(ptr)->size(); },
        [](void *ptr, size_t i) { 
            return &(*static_cast<std::vector<Weapon>*>(ptr))[i]; 
        },
        [](void *ptr, void *val) {
            static_cast<std::vector<Weapon>*>(ptr)->push_back(*(Weapon*)val);
            return true;
        }
    };
    return &info;
}
```

**Step 3:** Bind variable (in main.cpp main())

```cpp
std::vector<Weapon> inventory;
PyInterface::bind("inventory", inventory);
```

**Automatic Python Support:**
```python
cpp.inventory[0].name
cpp.inventory.append_new()
for weapon in cpp.inventory:
    print(weapon.damage)
```

**Total Changes:** ~10 lines (lambdas), one file.

---

### Adding Language Bindings (Lua, Ruby)

**Understanding the Three-Layer Extension:**

```
Current Code (Python binding):
┌──────────────────────────────┐
│ cpp_module.cpp (230 lines)   │  ← Python C-API
│ python_proxy.cpp (1080 lines)│  ← Python C-API
│ python_bind.hpp (180 lines)  │  ← Python C-API
└──────────────────────────────┘
        ↓ Uses
┌──────────────────────────────┐
│ value_interface.hpp (unchanged)
└──────────────────────────────┘
        ↓ Uses
┌──────────────────────────────┐
│ reflection_*.hpp (unchanged) │
└──────────────────────────────┘

To Add Lua:
┌──────────────────────────────┐
│ lua_module.c (~200 lines)    │  ← Lua C-API
│ lua_proxy.c (~500 lines)     │  ← Lua C-API
│ lua_bind.c (~100 lines)      │  ← Lua C-API
└──────────────────────────────┘
        ↓ Uses
┌──────────────────────────────┐
│ value_interface.hpp (reused!)
└──────────────────────────────┘
        ↓ Uses
┌──────────────────────────────┐
│ reflection_*.hpp (reused!)   │
└──────────────────────────────┘
```

**File Correspondence:**

| Python | Purpose | Lua Equivalent |
|--------|---------|----------------|
| cpp_module.cpp | Module registration, getattr | lua_module.c |
| python_proxy.cpp | StructProxy, VectorProxy | lua_proxy.c |
| python_bind.hpp | PyBoundInt, PyBoundString | lua_bind.c |
| value_interface.hpp | Registry, binding | REUSE |
| reflection_struct.hpp | StructInfo, BoundStruct | REUSE |
| reflection_vector.hpp | VectorInfo, BoundVector | REUSE |

**Key Insight:** lua_proxy.c would look structurally identical to python_proxy.cpp, just:
- Replace `PyObject *` with `int` (Lua stack index)
- Replace `PyLong_FromLong()` with `lua_pushinteger(L, ...)`
- Replace `Py_INCREF`/`DECREF` with `lua_pushvalue`/`lua_pop`
- **All BoundStruct, BoundVector logic stays the same!**

---

## IV. Performance Characteristics

### Memory Layout

**Stack Usage (main.cpp):**
```cpp
Player player;          // ~32 bytes typical (int + string + bool)
Team team;              // ~64 bytes
std::vector<int> scores; // 24 bytes (capacity, size, pointer)
std::vector<Enemy> enemies;  // 24 bytes
```

**Heap Usage:**
- `scores` data: 4 bytes × num_elements
- `enemies` data: ~40 bytes × num_elements (int + string + bool)
- `team.players`: 40 bytes × num_members
- `grid` nested vectors: cascading allocation

**Global Registry (g_values):**
- unordered_map overhead: ~256 bytes (typical)
- Per entry: ~80 bytes (unique_ptr + deleter, map node)
- For 5 variables: ~640 bytes total

### Time Complexity

**Binding a Variable:**
```cpp
PyInterface::bind("player", player_instance)
├─→ Compile-time dispatch: O(1) – code generation
├─→ g_values.insert(): O(1) average unordered_map
└─→ BoundStruct constructor: O(1)
Total: O(1) average
```

**Reading a Struct Field:**
```python
cpp.player.health
├─→ cpp_module_getattr(): O(5) – 5 registries, unordered_map lookup
├─→ StructProxy_getattro(): O(5) – linear field lookup, typically 5 fields
├─→ Offset calculation: O(1)
├─→ Value dereference: O(1)
└─→ PyLong_FromLong(): O(1)
Total: O(5-10) but all on-stack, cache-friendly
```

**Vector Indexing:**
```python
cpp.enemies[10]
├─→ cpp_module_getattr(): O(5)
├─→ VectorProxy_getitem(): O(1) – bounds check
├─→ element_ptr calculation: O(1) – function call
├─→ Value dereference: O(1)
└─→ Conversion (StructProxy_New): O(1)
Total: O(1) per lookup
```

**Vector Iteration:**
```python
for enemy in cpp.enemies:
├─→ iter(): O(1) – create iterator
├─→ __next__(): O(n) × element complexity, depends on element access
│   ├─→ Bounds check: O(1)
│   ├─→ Element access: O(1) via function pointer
│   └─→ Conversion to StructProxy: O(1)
└─→ Total for N elements: O(N)
```

### Cache Characteristics

**Good Cache Behavior:**
- Field lookup: Linear scan of FieldInfo vector (probably fits L1 cache)
- Offset arithmetic: Single arithmetic instruction
- Value dereference: Direct memory read (no indirection)

**Poor Cache Behavior:**
- Python object allocation: Scattered heap
- Module path lookup: Hash function (non-local)
- Type conversion: Function calls

**Overall Assessment:** Fast path (struct field read) is extremely cache-friendly. Hot loops should perform well.

---

## V. Common Pitfalls and Solutions

### Pitfall 1: Modifying Struct After Binding

**Problem:**
```cpp
Player player{100, "Alice"};
PyInterface::bind("player", player);

player.health = 50;  // Modified in C++
// Python still sees 100!
```

**Why:**
- BoundStruct holds void* pointer to memory
- Python gets proxy holding same pointer
- Both should see change... unless Python cached the value

**Solution:**
- Avoid caching scalar values in Python
- Always read through proxy
- For performance, cache StructProxy/VectorProxy objects, not their fields

---

### Pitfall 2: Using auto Instead of Explicit Types

**Problem:**
```cpp
auto player = Player{100, "Alice"};  // auto = Player, perfect
PyInterface::bind("player", player);  // ✓ Works

auto enemies = std::vector<Enemy>();  // auto = vector<Enemy>
PyInterface::bind("enemies", enemies);  // ✓ Also works

auto vec = { 1, 2, 3 };  // auto = std::initializer_list!
PyInterface::bind("vec", vec);  // ✗ FAILS – initializer_list not supported
```

**Solution:**
```cpp
std::vector<int> vec = { 1, 2, 3 };  // Explicit type
PyInterface::bind("vec", vec);  // ✓ Works
```

---

### Pitfall 3: String Memory Lifetime

**Problem:**
```cpp
class Controller {
    const char *name;  // Raw pointer!
};

// Later:
std::string temp = "Temporary";
PyUnicode_AsUTF8(...);  // Gets C string
name = str;  // Just storing pointer
// ...
temp is destroyed, name points to freed memory!
```

**Solution:**
```cpp
struct Controller {
    std::string name;  // Owned storage
};

// Python → C++:
PyUnicode_AsUTF8();  // Gets temporary pointer
controller.name = that_string;  // std::string copies immediately
```

---

### Pitfall 4: Nested Struct Modification

**Problem:**
```python
cpp.team.leader.health = 50  # Does this work?

# Execution:
team_proxy = cpp_module_getattr("team")              # OK
leader_proxy = StructProxy_getattro(team_proxy, "leader")  # Creates
leader_proxy.health = 50                             # Writes to new proxy

# But: New proxy's BoundStruct::m_instance points to temporary!
# Old Leader proxy is destroyed, modifications lost.
```

**Solution:** Nested structs work, but each level creates a temporary proxy. Modifications are correctly applied to memory, but don't hold intermediate proxies:

```python
# ✓ Do this (one statement):
cpp.team.leader.health = 50

# ✗ Don't do this (two statements):
leader = cpp.team.leader  # Creates temporary proxy
leader.health = 50         # Modifies via temporary
# leader may not point to same object anymore
```

---

### Pitfall 5: Vector Mutation During Iteration

**Problem:**
```python
for enemy in cpp.enemies:
    cpp.enemies.append_new()  # Modifies vector during iteration!
    # Iterator's bounds check now outdated
```

**Solution:** Never modify collection being iterated. Python's built-in collections have same limitation:

```python
lst = [1, 2, 3]
for x in lst:
    lst.append(x)  # ✗ Undefined behavior!

# Correct approach:
for x in lst[:]:  # Iterate over copy
    lst.append(x)
```

---

## VI. Extension API for User-Defined Types

### Compile-Time Registration Points

**Users extend the system by specializing these templates:**

```cpp
// 1. Mark a struct as "reflected"
namespace std {
    template<>
    struct hash<Player> { ... };  // If using in map

    template<>
    struct is_reflected_struct<Player> : std::true_type {};
}

// 2. Provide field metadata
template<>
inline const StructInfo *get_struct_info<Player>() { ... }

// 3. For vectors: provide element metadata
template<>
inline const VectorInfo *get_vector_info<Player>() { ... }

// 4. In main(): bind instances
PyInterface::bind("player", my_player_instance);
```

### Runtime Registration Points

None! The system is entirely compile-time and static. No dynamic type registration.

### Customization Points

**If you need custom conversion logic:**

```cpp
// User can specialize:
template <>
class PyBoundValue<MyCustomType> : public PyBoundValue {
    MyCustomType *m_ptr;
    
    PyObject *to_python() override {
        // Custom logic
        PyObject *dict = PyDict_New();
        // ... populate dict ...
        return dict;
    }
};
```

---

## VII. Summary: Design Decisions Hierarchy

```
┌─ Separation of Concerns ────────────────────────┐
│  └─ Layer 1: Pure C++ Reflection               │
│     └─ void* + enum (type-erasure)             │
│     └─ Function pointers (operation dispatch)  │
│     └─ Offset arithmetic (zero-copy access)    │
│  └─ Layer 2: Type Detection Bridge             │
│     └─ Type traits (compile-time introspection)│
│     └─ if constexpr (dead code elimination)    │
│     └─ Centralized registry (dynamic discovery)│
│  └─ Layer 3: Language-Specific Binding         │
│     └─ Python C-API (replaceable with Lua, etc.)
└────────────────────────────────────────────────┘

All decisions enable:
1. Multi-language support (Lua next)
2. Zero modification to user's C++ types
3. Fast execution (zero-copy, no indirection)
4. Simple mental model (metadata alongside data)
5. Easy extension (add struct = specialize 2 templates)
```

This design prioritizes **extensibility through composition** over inheritance, **static knowledge** over runtime polymorphism, and **user experience** over implementation complexity.
