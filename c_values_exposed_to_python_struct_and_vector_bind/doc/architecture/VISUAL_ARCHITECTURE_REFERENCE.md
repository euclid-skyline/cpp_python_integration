# Visual Architecture Summary and Reference Sheet

## The Three-Layer System at a Glance

```
PYTHON CODE
    ↓
    import cpp
    cpp.player.health = 100
    ↓ ↓
┌────────────────────────────────────────────────────────┐
│ LAYER 3: PYTHON INTEGRATION (#include<Python.h>)       │
│                                                          │
│  cpp_module.cpp          ← Module initialization       │
│  ├─ PyInit_cpp()         ← Exports module to Python    │
│  └─ cpp_module_getattr() ← Route cpp.* access          │
│                                                          │
│  python_proxy.cpp        ← Wrapper types               │
│  ├─ StructProxyType      ← Makes struct Pythonic       │
│  ├─ VectorProxyType      ← Makes vector Pythonic       │
│  └─ VectorIteratorType   ← Supports for loops          │
│                                                          │
│  python_bind.hpp         ← Type conversions            │
│  ├─ PyBoundInt           ← int* ↔ Python int          │
│  ├─ PyBoundFloat         ← float* ↔ Python float      │
│  └─ PyBoundString        ← std::string ↔ Python str   │
└────────────────────────────────────────────────────────┘
                       ↑ uses
                       │
┌────────────────────────────────────────────────────────┐
│ LAYER 2: TYPE BINDING (STL only, NO Python.h)         │
│                                                          │
│  value_interface.hpp     ← Type detection & registry   │
│  ├─ is_reflected_struct<T> ← Compile-time detection   │
│  ├─ is_std_vector<T>     ← Compile-time detection     │
│  ├─ PyInterface::bind()  ← Route to correct wrapper   │
│  └─ PyInterface::g_values ← Central registry           │
└────────────────────────────────────────────────────────┘
                       ↑ inspects
                       │
┌────────────────────────────────────────────────────────┐
│ LAYER 1: PURE C++ REFLECTION (STL objects only)        │
│                                                          │
│  reflection_value.hpp    ← Type system foundation      │
│  ├─ ValueType enum       ← {Int, Float, Bool, ...}    │
│  └─ BoundValue base      ← Generic wrapper             │
│                                                          │
│  reflection_struct.hpp   ← Struct metadata             │
│  ├─ StructInfo           ← Describes struct shape     │
│  ├─ FieldInfo            ← Field name, offset, type    │
│  └─ BoundStruct          ← Reflects struct instance    │
│                                                          │
│  reflection_vector.hpp   ← Vector metadata             │
│  ├─ VectorInfo           ← Describes element type      │
│  └─ BoundVector          ← Reflects vector instance    │
│                                                          │
│  Special: void* + function pointers = type erasure     │
└────────────────────────────────────────────────────────┘
                       ↑ describes
                       │
                    C++ DATA
                   (user code)
```

---

## Data Access Flow: `cpp.player.health = 50`

### Step 1: Module Attribute Access
```
Python: cpp.player
  ↓
cpp_module_getattr(module, "player")
  ├─ PyUnicode_AsUTF8("player") → "player"
  ├─ PyInterface::get_value_raw("player") → BoundStruct*
  ├─ StructProxy_New(BoundStruct*)
  └─ return <StructProxy>

Python has: <StructProxy> wrapping BoundStruct
```

### Step 2: Field Attribute Access
```
Python: <StructProxy>.health
  ↓
StructProxy_getattro(<StructProxy>, "health")
  ├─ BoundStruct* bound = proxy->bound
  ├─ FieldInfo* field = bound->get_field("health")
  │  └─ Returns: {name: "health", offset: 0, type: Int}
  ├─ void* field_ptr = m_instance + field->offset
  │  └─ Pointer arithmetic: &player + 0 = &player.health
  ├─ int value = *(int*)field_ptr
  └─ return PyLong_FromLong(100)

Python has: 100 (int object)
```

### Step 3: Assignment
```
Python: = 50
  ↓
StructProxy_setattro(<StructProxy>, "health", py_50)
  ├─ PyLong_AsLong(py_50) → 50
  ├─ void* field_ptr = m_instance + offset
  │  └─ &player + 0 = &player.health
  └─ *(int*)field_ptr = 50

C++ Memory: player.health is now 50
```

---

## Memory Architecture: Zero-Copy Design

```
C++ Memory (user controls lifetime):
┌─────────────────────────────────────┐
│ Player player                       │
├─────────────────────────────────────┤
│ int health = 50          [offset 0] │
├─────────────────────────────────────┤
│ std::string name = "Alice" [offset 4]
├─────────────────────────────────────┤
│ bool is_alive = true     [offset ~32]
└─────────────────────────────────────┘
         ↑
         │ (no copy, direct
         │  memory access)
         │
Reflection Metadata (static):
┌─────────────────────────────────────┐
│ StructInfo player_info {            │
│   "Player",                         │
│   {                                 │
│     {name: "health",  offset: 0},   │
│     {name: "name",    offset: 4},   │
│     {name: "is_alive", offset: 32}  │
│   }                                 │
│ }                                   │
└─────────────────────────────────────┘
         ↑
         │ (points to)
         │
Proxy Wrapper (Python creates):
┌─────────────────────────────────────┐
│ StructProxy {                       │
│   BoundStruct* bound = {            │
│     m_instance: &player             │
│     m_info: &player_info            │
│   }                                 │
│ }                                   │
└─────────────────────────────────────┘

KEY: Python never copies player data!
     Only holds pointers to original memory.
     Direct memory read/write via offset arithmetic.
```

---

## Type Detection: Compile-Time Branching

```
bind("player", player_instance)
  ↓
bind<Player>()  ← Type is Player
  ├─ is_reflected_struct<Player>::value? YES
  │  └─ Compile THIS branch:
  │     g_values["player"] = 
  │         std::make_unique<BoundStruct>(
  │             "player", &player, get_struct_info<Player>()
  │         )
  │
  └─ Delete other branches (dead code elimination)

Result: Only BoundStruct code in binary, no overhead

bind("scores", scores_vector)  ← Type is vector<int>
  ├─ is_reflected_struct<vector<int>>? NO
  ├─ is_std_vector<vector<int>>? YES
  │  └─ Compile THIS branch:
  │     g_values["scores"] = 
  │         std::make_unique<BoundVector>(
  │             "scores", &scores, get_vector_info<int>()
  │         )
  │
  └─ Delete other branches

Result: Only BoundVector code in binary, no overhead
```

---

## Python Proxy Protocol Methods

### StructProxy
```cpp
StructProxy_getattro()  ← Handles: struct.field_name
                          Returns: Python object
StructProxy_setattro()  ← Handles: struct.field_name = value
                          Validates type, writes to memory
StructProxy_len()       ← Handles: len(struct)
                          Returns: number of fields
```

### VectorProxy
```cpp
VectorProxy_getitem()   ← Handles: vector[index]
                          Supports negative indexing
VectorProxy_setitem()   ← Handles: vector[index] = value
                          Type-specific assignment
VectorProxy_len()       ← Handles: len(vector)
                          Returns: vector.size()
VectorProxy_append()    ← Handles: vector.append(value)
                          Type-dispatch to append
VectorProxy_append_new() ← Handles: vector.append_new()
                          Allocate & append default
VectorProxy_iter()      ← Handles: iter(vector)
                          Creates VectorIteratorObject
```

### VectorIterator
```cpp
VectorIterator_next()   ← Handles: next(iterator)
                          Returns next element or StopIteration
```

---

## Type Conversion Flow

### C++ to Python (read)
```
C++ Memory (int)  →  PyBoundInt::to_python()  →  PyObject (int)
  100             PyLong_FromLong(100)           <Python 100>

C++ Memory (string)  →  PyBoundString::to_python()  →  PyObject (str)
  "Alice"               PyUnicode_FromString(...)      <Python "Alice">
```

### Python to C++ (write)
```
PyObject (int)    →  PyBoundInt::from_python()  →  C++ Memory (int)
  <Python 50>      PyLong_AsLong() → 50             *(int*)ptr = 50

PyObject (str)    →  PyBoundString::from_python() → C++ Memory (string)
  <Python "Bob">   PyUnicode_AsUTF8() → "Bob"        *s = "Bob"
```

---

## Extension Pattern: Adding a New Struct Type

```
Before: Codebase only knows about Player

After Adding Enemy:
┌─────────────────────────────────────────┐
│ Enemy struct definition (user code)     │
│   int level;  std::string name;         │
└─────────────────────────────────────────┘
         ↓
┌─────────────────────────────────────────┐
│ Type detection specialization (2 lines) │
│   template<>                            │
│   struct is_reflected_struct<Enemy>...  │
└─────────────────────────────────────────┘
         ↓
┌─────────────────────────────────────────┐
│ Metadata provider (10 lines)            │
│   template<>                            │
│   const StructInfo *get_struct_info...  │
│     FieldInfo[] = {                     │
│         {"level", offset, Int},         │
│         {"name", offset, String}        │
│     };                                  │
└─────────────────────────────────────────┘
         ↓
┌─────────────────────────────────────────┐
│ Binding in main() (1 line)              │
│   PyInterface::bind("boss", boss_inst); │
└─────────────────────────────────────────┘
         ↓
Python now supports:
  cpp.boss.level ✓
  cpp.boss.name  ✓
  cpp.boss.level = 100  ✓
  len(cpp.boss)  ✓
```

---

## Extension Pattern: Adding Language Binding (Lua)

```
Current (Python):
  reflection_*.hpp (85 lines)  ← Pure C++
         ↑
  value_interface.hpp (120 lines, no Python)
         ↑
  {cpp_module, python_proxy, python_bind} (1500 lines, Python C-API)

Adding Lua (hypothetical):
  reflection_*.hpp (85 lines, UNCHANGED!)
         ↑
  value_interface.hpp (120 lines, UNCHANGED!)
         ↑
  {lua_module, lua_proxy, lua_bind} (600 lines, Lua C-API)

Benefits:
  • Reflection code shared (85 lines, ~30% reuse)
  • Binding code shared (120 lines, ~20% reuse)
  • Only 600 new lines for complete Lua support
  • Same BoundStruct, BoundVector mechanisms
  • Same offset arithmetic
  • Only conversion to Lua types differs

File Mapping:
  python_proxy.cpp  → lua_proxy.c
  cpp_module.cpp    → lua_module.c
  python_bind.hpp   → lua_bind.c
  value_interface.hpp → REUSE
  reflection_*.hpp  → REUSE
```

---

## Performance Characteristics

### Best Case (Direct Memory Access)
```
Operation:    cpp.player.health
Time:         ✓ O(5-10) – all on-stack operations
  • Module getattr: O(5) – unordered_map lookup
  • Field lookup: O(5) – linear search, 5 field
  • Offset calculation: O(1) – arithmetic
  • Dereference: O(1) – pointer read
  • Type conversion: O(1) – PyLong_FromLong

Cache:        ✓ Good – pointer arithmetic is cache-friendly
```

### Worst Case (Nested Access)
```
Operation:    cpp.team.leader.health
Time:         ✓ O(15) – three getattr chains, each O(5)
  • First getattr: O(5)
  • Second getattr: O(5)
  • Third getattr: O(5)

Cache:        ✓ Still good – each step is independent
```

### Vector Iteration
```
Operation:    for enemy in cpp.enemies:
Time:         ✓ O(n*m) where:
              n = number of elements
              m = per-element access cost (typically 10)
  • iter(): O(1) – create iterator
  • Loop n times:
    • __next__: O(1) – bounds check
    • Element access: O(1) – function pointer
    • Conversion (StructProxy): O(m) – if struct

Space:        ✓ O(1) – only one VectorIteratorObject allocated
```

---

## Registry Structure

```
PyInterface::g_values
┌────────────────────────────────────┐
│ std::unordered_map<string, unique_ptr<BoundValue>>
├────────────────────────────────────┤
│ "player" → BoundStruct*            │ size=32B
│   └─ struct Player { int, string } │
├────────────────────────────────────┤
│ "scores" → BoundVector*            │ size=24B
│   └─ std::vector<int>              │
├────────────────────────────────────┤
│ "team" → BoundStruct*              │ size=20B
│   └─ struct Team { ... }           │
├────────────────────────────────────┤
│ "enemies" → BoundVector*           │ size=24B
│   └─ std::vector<Enemy>            │
└────────────────────────────────────┘

Lookup: O(1) average unordered_map
Space:  ~600 bytes for 5 variables + overhead
```

---

## Key Concepts Quick Reference

| Concept | Where | Purpose | How |
|---------|-------|---------|-----|
| **Reflection** | Layer 1 | Describe C++ data | Metadata + void* |
| **Type Detection** | Layer 2 | Route to correct handler | Type traits |
| **Type Erasure** | Layer 1 | Store different types together | void* + enum |
| **Offset Access** | Layer 1 | Read field without copy | Pointer arithmetic |
| **Function Ptr** | Layer 1 | Type-specific operations | Lambda callbacks |
| **if constexpr** | Layer 2 | Compile-time dispatch | Dead code elimination |
| **Proxy** | Layer 3 | Make C++ Pythonic | PyObject wrapper |
| **Binding** | Layer 2 | Connect C++ ↔ Python | Registry lookup |
| **Conversion** | Layer 3 | Transform types | PyLong_*, PyUnicode_* |
| **Iterator** | Layer 3 | Support for loops | __next__ + StopIteration |

---

## Common Operations Performance

```
Operation              Time      Space   Notes
─────────────────────────────────────────
Bind variable          O(1)      O(1)    Once per app start
Read scalar            O(5-10)   O(0)    Stack operations
Write scalar           O(5-10)   O(0)    Stack operations
Read struct field      O(10-15)  O(24)   Creates StructProxy
Access vector[i]       O(10)     O(24)   Creates element proxy
Append to vector       O(1)      O(1)    type_depends, usually O(1) amortized
Iterate vector         O(n*10)   O(1)    per element, iterator overhead minimal
Modify nested.field    O(25)     O(48)   Two proxies created
```

---

## Decision Tree: Choose Your Pattern

```
Need to...?

├─ Expose C++ to Python
│  └─ Use three-layer architecture (done!)
│
├─ Support another language too
│  └─ Reuse reflection layer
│     └─ Write new binding layer
│
├─ Add a new scalar type
│  └─ Create PyBound* class (40 lines)
│     └─ Specialize in if constexpr (2 lines)
│
├─ Add a new struct type
│  └─ Specialize 2 templates (20 lines)
│     └─ Call bind() (1 line)
│
├─ Store many structs
│  └─ Use std::vector<T>
│     └─ Specialize get_vector_info (10 lines)
│
├─ Optimize hot path
│  └─ Check performance section
│     └─ Linear search → hash map
│     └─ Re-measure
│
├─ Reduce memory
│  └─ Use offsetof() macro (built-in)
│     └─ Zero-copy already!
│
└─ Extend without modifying core
   └─ Specialize templates
      └─ Add to existing bind() calls
      └─ Core code untouched
```

---

**This visual reference sheet covers the complete system at a glance.**
**For detailed explanations, refer to the 4 main documentation files.**
