# C++ Python Integration Framework - Complete Usage Guide

**Date:** February 21, 2026  
**Issue:** Resolving Issue 17 - Missing Usage Documentation

---

## Table of Contents

1. [Overview](#overview)
2. [Quick Start](#quick-start)
3. [Defining C++ Structures](#defining-cpp-structures)
4. [Defining Vectors](#defining-vectors)
5. [Binding Variables to Python](#binding-variables-to-python)
6. [Python API Reference](#python-api-reference)
7. [Supported Operations](#supported-operations)
8. [Unsupported Operations](#unsupported-operations)
9. [Complete Examples](#complete-examples)
10. [Performance Characteristics](#performance-characteristics)
11. [Memory Management](#memory-management)
12. [Thread Safety](#thread-safety)
13. [Troubleshooting](#troubleshooting)

---

## Overview

This framework allows you to expose C++ variables (scalars, structs, and vectors) to Python with full read/write access. The integration uses type-safe reflection metadata to enable seamless interaction between C++ and Python.

### Supported C++ Types
- **Scalar types:** `int`, `float`, `bool`, `std::string`
- **Composite types:** Custom structs with reflected fields
- **Container types:** `std::vector<T>` where T is any supported type
- **Nested types:** Vectors of structs, vectors of vectors, and more

### How It Works
1. Define metadata for your structs using field information
2. Create function pointers for vector operations
3. Bind C++ variables using `PyInterface::bind()`
4. Access from Python via the `cpp` module as proxies

---

## Quick Start

### Minimal C++ Example

```cpp
#include "value_interface.hpp"

// Define a simple struct
struct Player {
    int health;
    float speed;
};

// Define metadata
static StructInfo PlayerInfo = {
    "Player",
    {
        {"health", offsetof(Player, health), ValueType::Int, nullptr},
        {"speed", offsetof(Player, speed), ValueType::Float, nullptr},
    }};

// Mark as reflected
template <>
struct is_reflected_struct<Player> : std::true_type {};

template <>
inline const StructInfo *get_struct_info<Player>() {
    return &PlayerInfo;
}

// In main.cpp:
int main() {
    PyImport_AppendInittab("cpp", &PyInit_cpp);
    // ... Python initialization ...
    
    // Create and bind
    Player player = {100, 5.5f};
    PyInterface::bind("player", player);
    
    // Now accessible from Python as cpp.player
    Py_Finalize();
}
```

### Minimal Python Example

```python
import cpp

# Access struct fields
print(f"Health: {cpp.player.health}")
print(f"Speed: {cpp.player.speed}")

# Modify struct fields
cpp.player.health = 150
cpp.player.speed = 7.25

print(f"Updated health: {cpp.player.health}")
```

---

## Defining C++ Structures

### Step 1: Define the Struct

```cpp
struct Enemy {
    int health;
    float x;
    float y;
    std::string name;
};
```

### Step 2: Create Field Metadata

Field metadata describes each struct member using:
- **name:** Field name as string
- **offset:** Byte offset from struct start (use `offsetof()`)
- **type:** `ValueType` enum (Int, Float, Bool, String, Struct, Vector)
- **type_meta:** Optional metadata for nested types (StructInfo*, VectorInfo*)

```cpp
static StructInfo EnemyInfo = {
    "Enemy",  // Struct name
    {
        // name,      offset,               type,           meta
        {"health",  offsetof(Enemy, health),   ValueType::Int,     nullptr},
        {"x",       offsetof(Enemy, x),        ValueType::Float,   nullptr},
        {"y",       offsetof(Enemy, y),        ValueType::Float,   nullptr},
        {"name",    offsetof(Enemy, name),     ValueType::String,  nullptr},
    }
};
```

### Step 3: Register as Reflected Struct

```cpp
// Trait specialization
template <>
struct is_reflected_struct<Enemy> : std::true_type {};

// Metadata accessor
template <>
inline const StructInfo *get_struct_info<Enemy>() {
    return &EnemyInfo;
}
```

### Complete Struct Example

```cpp
// header file: enemy.hpp
struct Enemy {
    int health;
    float attack;
    bool isDead;
    std::string name;
};

// metadata file: enemy_meta.hpp
#include "value_interface.hpp"

static StructInfo EnemyInfo = {
    "Enemy",
    {
        {"health", offsetof(Enemy, health), ValueType::Int, nullptr},
        {"attack", offsetof(Enemy, attack), ValueType::Float, nullptr},
        {"isDead", offsetof(Enemy, isDead), ValueType::Bool, nullptr},
        {"name", offsetof(Enemy, name), ValueType::String, nullptr},
    }
};

template <>
struct is_reflected_struct<Enemy> : std::true_type {};

template <>
inline const StructInfo *get_struct_info<Enemy>() {
    return &EnemyInfo;
}
```

---

## Defining Vectors

### Vector of Scalars (int, float, bool, string)

**Step 1: Declare the vector**
```cpp
extern std::vector<int> scores;
extern std::vector<float> positions;
extern std::vector<bool> flags;
extern std::vector<std::string> names;
```

**Step 2: Implement vector operations**
```cpp
// In your implementation file
std::vector<int> scores;  // Global definition

// Vector operation functions
std::size_t int_vec_size(void *ptr) {
    return reinterpret_cast<std::vector<int> *>(ptr)->size();
}

void *int_vec_element_ptr(void *ptr, std::size_t idx) {
    return &(*reinterpret_cast<std::vector<int> *>(ptr))[idx];
}

bool int_vec_append(void *ptr, void *val) {
    reinterpret_cast<std::vector<int> *>(ptr)->push_back(
        *static_cast<int *>(val));
    return true;
}
```

**Step 3: Create metadata**
```cpp
static VectorInfo IntVectorInfo = {
    ValueType::Int,           // Element type
    nullptr,                  // No element metadata
    int_vec_size,             // Size function
    int_vec_element_ptr,      // Element access function
    int_vec_append            // Append function
};

template <>
inline const VectorInfo *get_vector_info<int>() {
    return &IntVectorInfo;
}
```

**Step 4: Bind in C++**
```cpp
scores = {10, 20, 30, 40, 50};
PyInterface::bind("scores", scores);
```

**Step 5: Use in Python**
```python
import cpp

# Access
print(len(cpp.scores))              # 5
print(cpp.scores[0])                # 10
print(cpp.scores[-1])               # 50 (negative indexing!)

# Modify
cpp.scores[0] = 99
print(cpp.scores[0])                # 99

# Append
cpp.scores.append(60)
print(len(cpp.scores))              # 6
```

### Vector of Structs

**Step 1: Ensure struct is reflected** (see "Defining C++ Structures")

**Step 2: Implement vector operations for the struct**
```cpp
extern std::vector<Enemy> enemies;

std::size_t enemy_vec_size(void *ptr) {
    return reinterpret_cast<std::vector<Enemy> *>(ptr)->size();
}

void *enemy_vec_element_ptr(void *ptr, std::size_t idx) {
    return &(*reinterpret_cast<std::vector<Enemy> *>(ptr))[idx];
}

bool enemy_vec_append(void *ptr, void *val) {
    reinterpret_cast<std::vector<Enemy> *>(ptr)->push_back(
        *static_cast<Enemy *>(val));
    return true;
}
```

**Step 3: Create metadata with struct reference**
```cpp
static VectorInfo EnemyVectorInfo = {
    ValueType::Struct,        // Element type is struct
    &EnemyInfo,               // Points to struct metadata
    enemy_vec_size,
    enemy_vec_element_ptr,
    enemy_vec_append
};

template <>
inline const VectorInfo *get_vector_info<Enemy>() {
    return &EnemyVectorInfo;
}
```

**Step 4: Bind in C++**
```cpp
enemies = {
    {100, 5.0f, false, "Goblin"},
    {150, 3.0f, false, "Orc"},
    {50, 8.0f, false, "Spider"}
};
PyInterface::bind("enemies", enemies);
```

**Step 5: Use in Python**
```python
import cpp

# Read struct in vector
print(cpp.enemies[0].name)           # "Goblin"
print(cpp.enemies[0].health)         # 100
print(cpp.enemies[-1].attack)        # 8.0 (last enemy)

# Modify struct in vector
cpp.enemies[0].health = 200
cpp.enemies[1].isDead = True

# Create new enemy and append
new_enemy = cpp.enemies.append_new()
new_enemy.health = 75
new_enemy.attack = 4.5
new_enemy.isDead = False
new_enemy.name = "Goblin2"

print(len(cpp.enemies))              # 4
print(cpp.enemies[-1].name)          # "Goblin2"
```

### Vector of Vectors

**Step 1: Define operations for outer vector**
```cpp
extern std::vector<std::vector<int>> grid;

std::size_t grid_vec_size(void *ptr) {
    return reinterpret_cast<std::vector<std::vector<int>> *>(ptr)->size();
}

void *grid_vec_element_ptr(void *ptr, std::size_t idx) {
    return &(*reinterpret_cast<std::vector<std::vector<int>> *>(ptr))[idx];
}

bool grid_vec_append(void *ptr, void *val) {
    reinterpret_cast<std::vector<std::vector<int>> *>(ptr)->push_back(
        *static_cast<std::vector<int> *>(val));
    return true;
}
```

**Step 2: Create metadata with inner vector reference**
```cpp
static VectorInfo VectorOfIntVectorInfo = {
    ValueType::Vector,        // Element type is vector
    &IntVectorInfo,           // Points to inner vector metadata
    grid_vec_size,
    grid_vec_element_ptr,
    grid_vec_append
};

template <>
inline const VectorInfo *get_vector_info<std::vector<int>>() {
    return &VectorOfIntVectorInfo;
}
```

**Step 3: Bind and use in Python**
```python
import cpp

# Read nested vectors
print(len(cpp.grid))                 # Outer size
print(len(cpp.grid[0]))              # Inner size
print(cpp.grid[0][1])                # Access element [0][1]
print(cpp.grid[-1][-1])              # Access last of last

# Modify nested element
cpp.grid[0][0] = 999

# Append inner vector
new_row = cpp.grid.append_new_vector()
new_row.append(10)
new_row.append(20)
new_row.append(30)

print(cpp.grid[-1][0])               # 10
```

### Vector of Vectors of Structs

**Step 1: Define operations for waves vector**
```cpp
extern std::vector<std::vector<Enemy>> enemy_waves;

std::size_t enemy_waves_vec_size(void *ptr) {
    return reinterpret_cast<std::vector<std::vector<Enemy>> *>(ptr)->size();
}

void *enemy_waves_vec_element_ptr(void *ptr, std::size_t idx) {
    return &(*reinterpret_cast<std::vector<std::vector<Enemy>> *>(ptr))[idx];
}

bool enemy_waves_vec_append(void *ptr, void *val) {
    reinterpret_cast<std::vector<std::vector<Enemy>> *>(ptr)->push_back(
        *static_cast<std::vector<Enemy> *>(val));
    return true;
}
```

**Step 2: Create metadata**
```cpp
static VectorInfo VectorOfEnemyVectorInfo = {
    ValueType::Vector,        // Outer is vector
    &EnemyVectorInfo,         // Inner is vector of Enemy
    enemy_waves_vec_size,
    enemy_waves_vec_element_ptr,
    enemy_waves_vec_append
};

template <>
inline const VectorInfo *get_vector_info<std::vector<Enemy>>() {
    return &VectorOfEnemyVectorInfo;
}
```

**Step 3: Use in Python**
```python
import cpp

# Read deeply nested structure
print(len(cpp.enemy_waves))                    # Number of waves
print(len(cpp.enemy_waves[0]))                 # Enemies in first wave
print(cpp.enemy_waves[0][0].name)              # Name of first enemy in first wave
print(cpp.enemy_waves[-1][-1].health)          # Health of last enemy in last wave

# Create new wave
new_wave = cpp.enemy_waves.append_new_vector()

# Add enemies to the wave
enemy1 = new_wave.append_new()
enemy1.health = 100
enemy1.attack = 5.0
enemy1.name = "Boss1"

enemy2 = new_wave.append_new()
enemy2.health = 150
enemy2.attack = 8.0
enemy2.name = "Boss2"

print(len(cpp.enemy_waves))                    # Increased by 1
print(len(cpp.enemy_waves[-1]))                # 2 enemies in last wave
```

---

## Binding Variables to Python

### Binding Scalar Types

```cpp
int health = 100;
float speed = 5.5f;
bool is_alive = true;
std::string name = "Player";

PyInterface::bind("health", health);
PyInterface::bind("speed", speed);
PyInterface::bind("is_alive", is_alive);
PyInterface::bind("name", name);
```

### Binding Struct Instances

```cpp
Player player = {100, 5.5f};
PyInterface::bind("player", player);
```

### Binding Vectors

```cpp
std::vector<int> scores = {1, 2, 3, 4, 5};
PyInterface::bind("scores", scores);

std::vector<Enemy> enemies = {{100, 5.0f, false, "Goblin"}};
PyInterface::bind("enemies", enemies);

std::vector<std::vector<int>> grid = {{1, 2}, {3, 4}};
PyInterface::bind("grid", grid);
```

### Binding Multiple Variables

```cpp
int main() {
    // ... Python initialization ...
    
    // Bind multiple variables at once
    int level = 1;
    float experience = 0.0f;
    std::vector<int> inventory = {0, 0, 0};
    
    PyInterface::bind("level", level);
    PyInterface::bind("experience", experience);
    PyInterface::bind("inventory", inventory);
    
    // All accessible from Python now
    Py_Finalize();
}
```

---

## Python API Reference

### Scalar Operations

```python
# Read
val = cpp.health           # int
spd = cpp.speed            # float
alive = cpp.is_alive       # bool
name = cpp.name            # str

# Write
cpp.health = 150
cpp.speed = 7.25
cpp.is_alive = False
cpp.name = "NewName"
```

### Struct Operations

```python
# Access fields
cpp.player.health          # Read int field
cpp.player.speed           # Read float field
cpp.player.name            # Read string field

# Modify fields
cpp.player.health = 200
cpp.player.speed = 10.0
cpp.player.name = "Warrior"

# Cannot reassign struct itself
# cpp.player = new_player  # ❌ ERROR
```

### Vector Operations - Reading

```python
# Length
len(cpp.scores)                    # Vector size

# Access by index
cpp.scores[0]                      # First element
cpp.scores[-1]                     # Last element
cpp.scores[-2]                     # Second-to-last

# Invalid access
cpp.scores[999]                    # ❌ IndexError
cpp.scores[-999]                   # ❌ IndexError
```

### Vector Operations - Modifying

```python
# Modify element
cpp.scores[0] = 99
cpp.scores[-1] = 88

# Append scalar
cpp.scores.append(60)

# Append struct
new_enemy = cpp.enemies.append_new()
new_enemy.health = 100
new_enemy.attack = 5.0
new_enemy.name = "NewEnemy"

# Append vector
new_row = cpp.grid.append_new_vector()
new_row.append(10)
new_row.append(20)

# Cannot reassign vector
# cpp.scores = [1, 2, 3]  # ❌ ERROR
```

### Struct-in-Vector Operations

```python
# Read struct fields from vector
cpp.enemies[0].health              # Access field
cpp.enemies[-1].name               # Last enemy's name

# Modify struct fields in vector
cpp.enemies[0].health = 200
cpp.enemies[1].name = "Modified"

# Create and modify new struct
new = cpp.enemies.append_new()
new.health = 75
new.attack = 4.5
new.isDead = True
new.name = "NewEnemy"

# Chain operations
cpp.enemies.append_new().health = 100  # Create and modify
cpp.enemies[-1].attack = 5.5           # Modify the one we just added
```

### Vector-of-Vectors Operations

```python
# Read
cpp.grid[0][0]                     # Element [0][0]
cpp.grid[-1][-1]                   # Last of last
len(cpp.grid)                      # Outer vector size
len(cpp.grid[0])                   # First inner vector size

# Modify
cpp.grid[0][0] = 999               # Modify element

# Append inner vector
new_row = cpp.grid.append_new_vector()
new_row.append(10)
new_row.append(20)
```

### Deeply Nested Structures

```python
# Read
cpp.enemy_waves[0][0].health       # Health of first in first wave
cpp.enemy_waves[-1][-1].name       # Name of last in last wave

# Modify
cpp.enemy_waves[0][0].health = 500

# Create new wave and add enemies
new_wave = cpp.enemy_waves.append_new_vector()
enemy = new_wave.append_new()
enemy.health = 100
enemy.attack = 5.0
enemy.name = "WaveEnemy"

# Verify
print(len(cpp.enemy_waves))        # Total waves
print(len(cpp.enemy_waves[-1]))    # Enemies in last wave
print(cpp.enemy_waves[-1][-1].name) # Last enemy name
```

---

## Supported Operations

### ✅ Fully Supported

#### Scalar Types
- Reading: `val = cpp.var`
- Writing: `cpp.var = val`
- All types: `int`, `float`, `bool`, `std::string`

#### Structs
- Field access: `cpp.struct.field`
- Field modification: `cpp.struct.field = value`
- Nested access: `cpp.struct1.struct2.field`
- Multiple types: int, float, bool, string, vector fields

#### Vectors of Scalars
- Length: `len(cpp.vector)`
- Read: `cpp.vector[i]`, `cpp.vector[-i]`
- Write: `cpp.vector[i] = val`
- Append: `cpp.vector.append(val)`
- Negative indexing: `cpp.vector[-1]` (last element)
- Element types: int, float, bool, string

#### Vectors of Structs
- All vector operations above
- Read struct: `cpp.enemies[0].health`
- Modify struct: `cpp.enemies[0].health = 100`
- Create and append: `new = cpp.enemies.append_new()`
- Modify appended: `new.health = 75`

#### Nested Vectors
- Outer vector operations: `len()`, indexing
- Inner vector operations: same
- Read nested: `cpp.grid[0][1]`
- Modify nested: `cpp.grid[0][1] = 99`
- Append inner vector: `cpp.grid.append_new_vector()`
- Populate appended: `new_row.append(val)`

#### Deeply Nested (vector of vector of struct)
- All combinations work: read, modify, append
- Read: `cpp.enemy_waves[0][1].health`
- Modify: `cpp.enemy_waves[0][1].health = 200`
- Append wave: `cpp.enemy_waves.append_new_vector()`
- Add enemy: `new_enemy = wave.append_new()`
- Modify enemy: `new_enemy.health = 100`

### ❌ Unsupported Operations

#### Vector-Level Operations
- **Slicing:** `cpp.scores[1:3]` ❌
- **Iteration:** `for x in cpp.scores:` ❌
- **Comprehensions:** `[x*2 for x in cpp.scores]` ❌
- **len() on struct:** `len(cpp.player)` ❌
- **Reassignment:** `cpp.scores = [1,2,3]` ❌
- **Reassignment:** `cpp.player = new_player` ❌

#### Type Conversions
- **String representation:** `str(cpp.player)` shows raw object ❌
- **Repr:** `repr(cpp.enemies)` shows raw object ❌

#### Custom Indexing
- **NumPy int:** `cpp.enemies[np.int64(0)]` ❌
- **Custom indices:** Must use Python `int`

#### Advanced Vector Operations
- **Insert:** `cpp.scores.insert(0, 100)` ❌
- **Remove:** `cpp.scores.remove(10)` ❌
- **Pop:** `cpp.scores.pop()` ❌
- **Clear:** `cpp.scores.clear()` ❌
- **Reverse:** `cpp.scores.reverse()` ❌
- **Sort:** `cpp.scores.sort()` ❌

#### Unsupported Field Types
- **Complex structs:** Nested struct fields (workaround: define them separately)
- **Pointers:** `int*`, `Enemy*` ❌
- **Arrays:** `int[10]` ❌
- **Maps:** `std::map`, `std::unordered_map` ❌
- **Custom types:** Only built-in and reflected structs

---

## Complete Examples

### Example 1: Simple Game Character

**C++ Definition:**
```cpp
struct Character {
    std::string name;
    int level;
    float experience;
    int health;
    bool is_alive;
};

static StructInfo CharacterInfo = {
    "Character",
    {
        {"name", offsetof(Character, name), ValueType::String, nullptr},
        {"level", offsetof(Character, level), ValueType::Int, nullptr},
        {"experience", offsetof(Character, experience), ValueType::Float, nullptr},
        {"health", offsetof(Character, health), ValueType::Int, nullptr},
        {"is_alive", offsetof(Character, is_alive), ValueType::Bool, nullptr},
    }
};

template <>
struct is_reflected_struct<Character> : std::true_type {};

template <>
inline const StructInfo *get_struct_info<Character>() {
    return &CharacterInfo;
}

int main() {
    Character hero = {"Legolas", 10, 5000.0f, 100, true};
    PyInterface::bind("hero", hero);
    // ... Python execution ...
}
```

**Python Usage:**
```python
import cpp

# Read character info
print(f"Name: {cpp.hero.name}")
print(f"Level: {cpp.hero.level}")
print(f"Health: {cpp.hero.health}")

# Modify
cpp.hero.experience += 1000.0
cpp.hero.level = 11
cpp.hero.health = 150

# Check status
if cpp.hero.is_alive:
    print(f"{cpp.hero.name} is alive with {cpp.hero.health} HP")
```

### Example 2: Inventory System

**C++ Definition:**
```cpp
struct Item {
    std::string name;
    int quantity;
    float weight;
};

// ... Item metadata setup ...

// Vector operations for Item
extern std::vector<Item> inventory;
// ... implement item_vec_size, item_vec_element_ptr, item_vec_append ...

static VectorInfo ItemVectorInfo = {
    ValueType::Struct,
    &ItemInfo,
    item_vec_size,
    item_vec_element_ptr,
    item_vec_append
};

template <>
inline const VectorInfo *get_vector_info<Item>() {
    return &ItemVectorInfo;
}

int main() {
    inventory = {
        {"Sword", 1, 2.5f},
        {"Gold", 100, 0.1f},
        {"Potion", 5, 0.5f}
    };
    PyInterface::bind("inventory", inventory);
}
```

**Python Usage:**
```python
import cpp

# View inventory
print("=== Inventory ===")
for i in range(len(cpp.inventory)):
    item = cpp.inventory[i]
    print(f"{item.name}: {item.quantity} (weight: {item.weight})")

# Add item
new_item = cpp.inventory.append_new()
new_item.name = "Shield"
new_item.quantity = 1
new_item.weight = 3.0

# Modify item
cpp.inventory[0].quantity = 2

# Total weight
total_weight = 0
for i in range(len(cpp.inventory)):
    total_weight += cpp.inventory[i].weight * cpp.inventory[i].quantity

print(f"Total weight: {total_weight}")
```

### Example 3: Battle Grid

**C++ Definition:**
```cpp
extern std::vector<std::vector<int>> battleGrid;

// Vector operations...
static VectorInfo VectorOfIntVectorInfo = {
    ValueType::Vector,
    &IntVectorInfo,
    grid_vec_size,
    grid_vec_element_ptr,
    grid_vec_append
};

template <>
inline const VectorInfo *get_vector_info<std::vector<int>>() {
    return &VectorOfIntVectorInfo;
}

int main() {
    // 5x5 grid
    battleGrid = {
        {0, 1, 0, 1, 0},
        {1, 0, 1, 0, 1},
        {0, 1, 0, 1, 0},
        {1, 0, 1, 0, 1},
        {0, 1, 0, 1, 0}
    };
    PyInterface::bind("grid", battleGrid);
}
```

**Python Usage:**
```python
import cpp

# Display grid
print("=== Battle Grid ===")
for row_idx in range(len(cpp.grid)):
    row = cpp.grid[row_idx]
    for col_idx in range(len(row)):
        print(f"{row[col_idx]}", end=" ")
    print()

# Modify position
cpp.grid[2][2] = 2  # Place player at [2][2]

# Add new row
new_row = cpp.grid.append_new_vector()
new_row.append(1)
new_row.append(0)
new_row.append(1)
new_row.append(0)
new_row.append(1)

print(f"New grid size: {len(cpp.grid)}x{len(cpp.grid[0])}")
```

### Example 4: Enemy Waves System

**C++ Definition:**
```cpp
struct Enemy {
    int health;
    float x;
    std::string name;
};

// Enemy metadata...

extern std::vector<std::vector<Enemy>> enemy_waves;

// Vector of vector of Enemy operations...
static VectorInfo VectorOfEnemyVectorInfo = {
    ValueType::Vector,
    &EnemyVectorInfo,
    enemy_waves_vec_size,
    enemy_waves_vec_element_ptr,
    enemy_waves_vec_append
};

template <>
inline const VectorInfo *get_vector_info<std::vector<Enemy>>() {
    return &VectorOfEnemyVectorInfo;
}

int main() {
    enemy_waves = {
        {{100, 1.0f, "Goblin1"}, {100, 2.0f, "Goblin2"}},
        {{150, 0.5f, "Orc"}, {120, 1.5f, "Orc2"}, {130, 2.5f, "Orc3"}},
        {{50, 3.0f, "Spider"}}
    };
    PyInterface::bind("enemy_waves", enemy_waves);
}
```

**Python Usage:**
```python
import cpp

# Display all waves
print("=== Enemy Waves ===")
for wave_idx in range(len(cpp.enemy_waves)):
    wave = cpp.enemy_waves[wave_idx]
    print(f"\nWave {wave_idx + 1}: {len(wave)} enemies")
    for enemy_idx in range(len(wave)):
        enemy = wave[enemy_idx]
        print(f"  {enemy.name}: HP={enemy.health}, Position={enemy.x}")

# Create new wave
new_wave = cpp.enemy_waves.append_new_vector()
e1 = new_wave.append_new()
e1.name = "BossA"
e1.health = 500
e1.x = 10.0

e2 = new_wave.append_new()
e2.name = "BossB"
e2.health = 500
e2.x = 20.0

# Modify existing enemies
cpp.enemy_waves[0][0].health = 50  # Damage first enemy in first wave

# Count total enemies
total = 0
for wave_idx in range(len(cpp.enemy_waves)):
    total += len(cpp.enemy_waves[wave_idx])
print(f"Total enemies: {total}")

# Find first boss
for wave_idx in range(len(cpp.enemy_waves)):
    for enemy_idx in range(len(cpp.enemy_waves[wave_idx])):
        if cpp.enemy_waves[wave_idx][enemy_idx].name == "BossA":
            print(f"Found boss at wave {wave_idx}, position {enemy_idx}")
```

---

## Performance Characteristics

### Time Complexity

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| Read scalar | O(1) | Direct memory access |
| Write scalar | O(1) | Direct memory write |
| Read struct field | O(1) | Offset-based access |
| Write struct field | O(1) | Offset-based write |
| Vector length | O(1) | Cached size |
| Vector index access | O(1) | Direct indexing |
| Vector append | O(1) average | Amortized, may reallocate |
| Struct append | O(1) average | Copy struct to vector |

### Memory Usage

- **Scalar binding:** Minimal overhead (pointer to C++ variable)
- **Struct binding:** Metadata only (no copy)
- **Vector binding:** Metadata only (references existing vector)
- **Python proxies:** Small object (~1-2 KB per proxy)

### String Conversion

- **Read:** O(1) - direct pointer to Python string
- **Write:** O(n) - where n is string length (copies content)

---

## Memory Management

### Ownership Model

1. **Scalars & Structs:** Owned by C++
   - Bound variables referenced, not copied
   - C++ responsible for lifetime
   - Python cannot extend lifetime

2. **Vectors:** Owned by C++
   - Python can modify (append, set elements)
   - Python cannot resize beyond append
   - Underlying vector lifetime tied to C++

3. **Created Objects:** Temporary Python proxies
   - Auto-destroyed when out of scope
   - `append_new()` creates and returns proxy
   - Proxy references vector element

### Memory Safety

✅ Safe operations:
- Reading any value
- Modifying struct/vector fields within bounds
- Appending to vectors
- Negative indexing

❌ Unsafe operations:
- Out of bounds access (raises IndexError)
- Accessing after C++ destruction (undefined)
- Multi-threaded access (see Thread Safety)

### Resource Cleanup

C++ handles all cleanup:
```cpp
Py_Finalize();  // Finalizes Python and proxies
// C++ vectors cleaned up automatically
```

---

## Thread Safety

### Current Implementation: NOT THREAD SAFE

⚠️ Important limitations:

1. **Global State**: Shared `PyInterface::g_values` map is not protected
2. **No Locks**: No mutual exclusion on operations
3. **Python GIL**: Not used for protection
4. **Vector Operations**: Not atomic

### Recommended Use

- **Single-threaded only** (Python thread OR C++ thread, not both)
- Run Python in main thread
- Use thread pool in C++ if needed, but:
  - Don't modify bound variables from worker threads
  - Synchronize modifications with main thread

### For Multi-Threading

If you need thread safety:
1. **Add mutex protection** to `PyInterface` operations
2. **Use thread pool carefully**: only C++ compute, no variable modification
3. **Consider message passing**: thread → main thread → Python
4. **Lock Python GIL** if needed: use Python's threading module

Example (single-threaded safe):
```cpp
// OK: Compute in thread, modify in main
std::thread([&](){ 
    // Compute results
    int result = compute();
    // Don't modify cpp.var here!
}).join();

// Update Python variables in main thread only
cpp.health = result;
```

---

## Troubleshooting

### Issue: "AttributeError: module 'cpp' has no attribute 'variable_name'"

**Cause:** Variable not bound with `PyInterface::bind()`

**Solution:**
```cpp
// Ensure bind() is called before Python script
PyInterface::bind("player", player);
```

### Issue: "TypeError: Expected int" when assigning

**Cause:** Type mismatch between Python value and C++ field

**Solution:**
```python
# ❌ Wrong type
cpp.player.health = "100"      # String

# ✓ Correct type
cpp.player.health = 100        # Integer
```

### Issue: "IndexError: Vector index out of range"

**Cause:** Index beyond vector size

**Solution:**
```python
# Check bounds first
if index >= 0 and index < len(cpp.scores):
    cpp.scores[index] = value

# Use negative indexing safely
cpp.scores[-1]  # Last element (safe if not empty)
```

### Issue: "TypeError: Cannot reassign struct or vector"

**Cause:** Attempting to reassign entire struct/vector

**Solution:**
```python
# ❌ Cannot do this
cpp.player = new_player        # ERROR
cpp.scores = [1, 2, 3]         # ERROR

# ✓ Modify fields instead
cpp.player.health = 100        # OK
cpp.scores[0] = 100            # OK
cpp.scores.append(100)         # OK
```

### Issue: "TypeError: append_new() only works for vectors of structs"

**Cause:** Called `append_new()` on vector of scalars

**Solution:**
```python
# For vectors of scalars - use append()
cpp.scores.append(100)         # Correct for int vector

# For vectors of structs - use append_new()
new = cpp.enemies.append_new() # Correct for Enemy vector
```

### Issue: Changes from Python not visible in C++

**Cause:** Python running in different thread or Python not finalized

**Solution:**
```cpp
// Run Python and read changes BEFORE Py_Finalize()
PyObject_CallObject(updateFunc, nullptr);

// Read changes while Python is still running
int health = player.health;    // C++ can read updated value

Py_Finalize();  // Now cleanup
```

### Issue: Memory leaks or crashes

**Cause:** Accessing after Py_Finalize() or invalid metadata

**Solution:**
1. Verify metadata offsets are correct
2. Check metadata for NULL pointers
3. Ensure C++ objects outlive Python execution
4. Use AddressSanitizer to find leaks:
   ```bash
   cmake -DCMAKE_CXX_FLAGS="-fsanitize=address" ..
   ```

---

## Summary

This framework provides seamless C++ ↔ Python integration for:
- ✅ Scalars, structs, and vectors
- ✅ Nested structures (vectors of vectors, vectors of structs)
- ✅ Full read/write access from Python
- ✅ Type-safe reflection
- ✅ Zero-copy access to C++ data

Use this guide to integrate C++ game logic with Python scripting safely and efficiently!
