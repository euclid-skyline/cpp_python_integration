# Comprehensive Function Reference: Data Flow and Implementation Details

## I. Entry Point and Initialization

### main.cpp: Application Lifecycle

#### `int main()`

**Purpose:** Orchestrate entire application: locate Python, configure interpreter, bind variables, run control script.

**Execution Flow:**

```
1. Signal handler registration
   └─→ signal(SIGINT/SIGTERM/...) maps to exit handler

2. Python location
   └─→ pyembed::locate_python()
       Tries: bundled → registry → environment variables → system
       Returns: Python installation path with version

3. Python configuration
   ├─→ PyConfig_InitPythonConfig(&config)
   ├─→ Set search paths for modules
   ├─→ Optionally disable SITE_PACKAGES (sandboxing)
   └─→ Py_InitializeFromConfig(&config)

4. Module registration
   └─→ PyImport_AppendInittab("cpp", PyInit_cpp)
       Makes our custom cpp module available to import

5. Variable binding
   ├─→ PyInterface::bind("player", player_instance)
   │   └─→ Creates BoundStruct, stores in g_values["player"]
   ├─→ PyInterface::bind("scores", scores_vector)
   │   └─→ Creates BoundVector, stores in g_values["scores"]
   ├─→ PyInterface::bind("enemies", enemies_vector)
   ├─→ PyInterface::bind("grid", nested_vector)
   └─→ PyInterface::bind("enemy_waves", nested_struct_vector)

6. Script execution
   ├─→ PyRun_SimpleString("import controller")
   ├─→ Controller calls cpp.update_values()
   └─→ Loop: update, render, sleep

7. Shutdown
   ├─→ Py_FinalizeEx()
   └─→ return 0;
```

**Memory Management:**
- `running` flag: `static bool` (stack)
- `player`, `team`: Stack objects (auto-freed at scope end)
- `scores`, `enemies`: Vector capacity on heap, managed by std::vector
- `PyInterface::g_values`: Global map, destroyed at program exit

**Key Decision: Why Explicit Configuration?**
- Supports multiple Python deployment models (bundled, system, zip)
- Enables sandboxing by disabling system site-packages
- Works across Windows, Linux, macOS without platform-specific code

---

## II. Module and Dynamic Attribute Resolution

### cpp_module.cpp: Python Module Interface

#### `static PyObject *cpp_module_getattr(PyObject *module, PyObject *name)`

**Purpose:** Intercept `cpp.variable_name` access and return appropriate wrapper.

**Execution Flow:**

```
Input:  module (the cpp module), name (PyUnicode object)

Step 1: Extract C string from PyUnicode
        ├─→ const char *name_str = PyUnicode_AsUTF8(name)
        ├─→ if (!name_str) return NULL  (silently fail to Python)
        └─→ utf-8 string ready for lookup

Step 2: Look up in global registry
        ├─→ BoundValue *bound = PyInterface::get_value_raw(name_str)
        └─→ if (!bound) PyErr_Format(AttributeError, ...)

Step 3: Dynamic dispatch based on type
        ├─→ case ValueType::Int/Float/Bool/String:
        │   ├─→ Cast to PyBoundValue*
        │   ├─→ Call pv->to_python()
        │   └─→ Return converted PyObject* directly
        │
        ├─→ case ValueType::Struct:
        │   ├─→ Cast to BoundStruct*
        │   ├─→ Call StructProxy_New(bs)
        │   └─→ Return PyObject* wrapping the struct
        │
        └─→ case ValueType::Vector:
            ├─→ Cast to BoundVector*
            ├─→ Call VectorProxy_New(bv)
            └─→ Return PyObject* wrapping the vector

Output: PyObject* (Python object, NULL = error)
```

**Example Execution:**
```python
# User code:
import cpp
cpp.player

# Call sequence:
cpp_module_getattr(cpp_module, "player");
├─→ PyUnicode_AsUTF8("player") = "player"
├─→ PyInterface::get_value_raw("player") = BoundStruct*
├─→ dynamic_cast<BoundVector*>(...) = nullptr (not a vector)
├─→ dynamic_cast<PyBoundValue*>(...) = nullptr (not a scalar)
├─→ StructProxy_New(bound_struct)
└─→ Return StructProxy object

# User now has:
player = <StructProxy object>
```

#### `static PyObject *cpp_module_setattr(PyObject *module, PyObject *name, PyObject *value)`

**Purpose:** Handle `cpp.variable_name = value` assignments.

**Key Restriction:** Only allows modification of scalars (int, float, bool, string).

**Execution Flow:**

```
Input:  module (cpp), name (variable name), value (new value)

Step 1: Extract name string
        const char *name_str = PyUnicode_AsUTF8(name)

Step 2: Look up variable
        BoundValue *bound = PyInterface::get_value_raw(name_str)

Step 3: Type check – permit only scalars
        ├─→ if (bound->type == Int/Float/Bool/String): OK
        └─→ else: PyErr_Format("can't assign to struct/vector")

Step 4: Convert Python value to C++
        ├─→ Cast to PyBoundValue*
        ├─→ Call pv->from_python(value)
        └─→ Writes directly to original variable

Output: 0 (success) or -1 (error)
```

**Why No Struct/Vector Assignment?**
- Structs/vectors are complex objects with memory management
- Assigning entire struct is rare (modify fields instead)
- Prevents accidental replacement of vector capacity/allocation
- Encourages:
  ```python
  cpp.player.health = 100  # Modify specific field
  cpp.enemies[0].name = "boss"  # Modify via proxy
  ```

#### `static PyObject *PyInit_cpp(void)`

**Purpose:** Module initialization (required by Python C-API for `import cpp`).

**Execution Flow:**

```
Python calls: PyInit_cpp()  (when `import cpp` executes)

Step 1: Register all proxy type objects
        ├─→ PyType_Ready(&CppProxyType)
        ├─→ PyType_Ready(&StructProxyType)
        ├─→ PyType_Ready(&VectorProxyType)
        └─→ PyType_Ready(&VectorIteratorType)

Step 2: Create module
        ├─→ PyModule_Create(&cppmodule)
        └─→ Returns PyObject* (generic module)

Step 3: Change module type to custom type
        ├─→ PyObject_SetAttr(m, "__class__", (PyObject*)&CppModuleType)
        └─→ Now module uses our tp_getattro/tp_setattro!

Step 4: Return module
        return m;

Output: PyObject* (module)
```

**Key Detail: The Type Change**
- Standard Python modules have type `PyModule_Type`
- We replace it with `CppModuleType` (inherits from PyModule_Type)
- `CppModuleType` overrides getattr/setattr methods
- Result: Dynamic attribute access works!

---

## III. Structure Reflection and Field Access

### reflection_struct.hpp: Metadata and Bound Structures

#### `const FieldInfo *StructInfo::get_field(const std::string &name)`

**Purpose:** Look up field metadata by name.

**Implementation:**
```cpp
const FieldInfo *StructInfo::get_field(const std::string &name) const {
    for (const auto &field : fields) {
        if (field.name == name) return &field;
    }
    return nullptr;
}
```

**Performance:** O(n) where n = number of fields (typically 5-20)

**Optimization Opportunity:** Could use unordered_map for O(1), but fields are small/linear

#### `void *BoundStruct::get_field_ptr(const FieldInfo *f)`

**Purpose:** Calculate memory address of a field within the struct instance.

**Implementation:**
```cpp
void *BoundStruct::get_field_ptr(const FieldInfo *f) {
    return static_cast<void*>(
        static_cast<char*>(m_instance) + f->offset
    );
}
```

**Why Casting to char*?**
- void* arithmetic is undefined (doesn't know element size)
- char* arithmetic is 1 byte per increment (well-defined)
- `+ f->offset` adds bytes
- Cast back to void* for generic return

**Example:**
```cpp
// Given:
struct Player { int health; std::string name; };  // health at offset 0, name at offset 4
Player player_instance;
BoundStruct bs(&player_instance, &player_struct_info);

// Call:
const FieldInfo *health_field = player_struct_info.get_field("health");
void *health_ptr = bs.get_field_ptr(health_field);

// Calculation:
static_cast<char*>(&player_instance) + 0 = (char*)&player_instance
cast back to void* = &player_instance (unchanged)

// Actual type:
(int*)health_ptr == &player_instance.health
```

---

### python_proxy.cpp: Structure Proxy Implementation

#### `PyObject *StructProxy_getattro(PyObject *self, PyObject *attr)`

**Purpose:** Handle `struct_proxy.field_name` read access.

**Execution Flow (Detailed):**

```
Input:  self (StructProxy instance), attr (field name as PyUnicode)

Step 1: Unpack container structure
        auto *proxy = (StructProxy *)self;
        const BoundStruct *bound = proxy->bound;  // Points to BoundStruct

Step 2: Extract and validate field name
        const char *attr_name = PyUnicode_AsUTF8(attr);
        if (!attr_name) return nullptr;  // Invalid Unicode

Step 3: Metadata lookup
        const FieldInfo *field = bound->get_field(attr_name);
        if (!field) {
            return PyErr_Format(PyExc_AttributeError,
                "'Player' object has no attribute '%s'", attr_name);
        }

Step 4: Calculate field memory address
        void *field_ptr = bound->get_field_ptr(field);
        // field_ptr = m_instance + field->offset

Step 5: Type-dispatch conversion
        Based on field->type:
        
        case ValueType::Int:
            return PyLong_FromLong(*(int*)field_ptr);
        
        case ValueType::Float:
            return PyFloat_FromDouble(*(float*)field_ptr);
        
        case ValueType::Bool: {
            auto byte_val = *(ByteBool*)field_ptr;
            return PyBool_FromLong(byte_val == TRUE_BYTE ? 1 : 0);
        }
        
        case ValueType::String: {
            auto *str_ptr = static_cast<std::string*>(field_ptr);
            return PyUnicode_FromString(str_ptr->c_str());
        }
        
        case ValueType::Struct: {
            auto *si = static_cast<StructInfo*>(field->type_meta);
            BoundStruct *inner = new BoundStruct("field", field_ptr, si);
            return StructProxy_New(inner);
        }
        
        case ValueType::Vector: {
            auto *vi = static_cast<VectorInfo*>(field->type_meta);
            BoundVector *inner = new BoundVector("field", field_ptr, vi);
            return VectorProxy_New(inner);
        }

Output: PyObject* (Python value or NULL if error)
```

**Example Data Flow:**
```cpp
// C++ memory layout:
Player player = { health: 100, name: "Alice" };

// Python execution:
py_player.health

// Execution:
StructProxy_getattro(py_player_proxy, "health")
├─→ bound = &py_player_proxy->bound  // Points to BoundStruct
├─→ field = bound->get_field("health")  // FieldInfo { offset: 0, type: Int }
├─→ field_ptr = bound->get_field_ptr(field)  // Pointer arithmetic: &player + 0
├─→ value = *(int*)field_ptr  // Dereference: 100
└─→ return PyLong_FromLong(100)

# Python result: 100 (int)
```

#### `PyObject *StructProxy_setattro(PyObject *self, PyObject *attr, PyObject *value)`

**Purpose:** Handle `struct_proxy.field_name = value` write access.

**Execution Flow:**

```
Input:  self (StructProxy), attr (field name), value (new value or NULL for delete)

Step 1: Check delete operation
        if (value == NULL) {
            return PyErr_Format(PyExc_TypeError, "can't delete attribute");
        }

Step 2-3: Same as getattro – lookup field
        [... field validation omitted for brevity ...]

Step 4: Type validation
        switch (field->type) {
            case ValueType::Int:
                Check: PyLong_Check(value)
            case ValueType::Float:
                Check: PyFloat_Check(value) || PyLong_Check(value)
            case ValueType::Bool:
                Check: PyBool_Check(value)
            case ValueType::String:
                Check: PyUnicode_Check(value)
            default:
                Reject structs and vectors (don't reassign complex objects)
        }

Step 5: Type-specific conversion and write
        case ValueType::Int: {
            long val = PyLong_AsLong(value);
            *(int*)field_ptr = (int)val;
        }
        
        case ValueType::String: {
            const char *str = PyUnicode_AsUTF8(value);
            auto *s = static_cast<std::string*>(field_ptr);
            *s = str;  // std::string copy
        }
        // ... similar for Float, Bool ...

Output: 0 (success) or -1 (error)
```

#### `Py_ssize_t StructProxy_len(PyObject *self)`

**Purpose:** Support `len(struct_proxy)` – returns number of fields.

**Implementation:**
```cpp
Py_ssize_t StructProxy_len(PyObject *self) {
    auto *proxy = (StructProxy *)self;
    return (Py_ssize_t)proxy->bound->info()->fields.size();
}
```

**Usage Example:**
```python
player_fields = len(cpp.player)  # Returns 3 if Player has 3 fields
```

---

## IV. Vector Reflection and Element Access

### reflection_vector.hpp: Vector Metadata

#### `std::size_t BoundVector::size()`

**Purpose:** Get element count in vector.

**Implementation:**
```cpp
std::size_t BoundVector::size() const {
    return m_info->size_fn ? m_info->size_fn(m_vec_ptr) : 0;
}
```

**Where size_fn Comes From:**
- Defined at compile-time for each vector type
- Example for `std::vector<int>`:
  ```cpp
  [](void *ptr) { return static_cast<std::vector<int>*>(ptr)->size(); }
  ```

#### `void *BoundVector::element_ptr(std::size_t index)`

**Purpose:** Get memory address of element at given index.

**Implementation:**
```cpp
void *BoundVector::element_ptr(std::size_t index) const {
    return m_info->element_ptr_fn ? m_info->element_ptr_fn(m_vec_ptr, index) : nullptr;
}
```

**Example:** For `std::vector<Player>`:
```cpp
std::vector<Player> *vec = ...;
element_ptr_fn = [](void *ptr, size_t i) {
    auto *v = static_cast<std::vector<Player>*>(ptr);
    return &(*v)[i];  // Return address of element
};
```

---

### python_proxy.cpp: Vector Proxy Implementation

#### `PyObject *VectorProxy_getitem(PyObject *self, Py_ssize_t index)`

**Purpose:** Handle `vector_proxy[i]` read access.

**Execution Flow:**

```
Input:  self (VectorProxy), index (integer index, may be negative)

Step 1: Get bounds information
        auto *proxy = (VectorProxy *)self;
        const BoundVector *bound = proxy->bound;
        Py_ssize_t size = (Py_ssize_t)bound->size();

Step 2: Handle negative indexing
        if (index < 0) index += size;
        // Example: index = -1, size = 5 → index = 4 (last element)

Step 3: Validate bounds
        if (index < 0 || index >= size) {
            return PyErr_Format(PyExc_IndexError, "index out of range");
        }

Step 4: Get element memory address
        void *elem_ptr = bound->element_ptr(index);

Step 5: Type-dispatch conversion
        Based on bound->info()->element_type:
        
        case ValueType::Int:
            return PyLong_FromLong(*(int*)elem_ptr);
        
        case ValueType::Float:
            return PyFloat_FromDouble(*(float*)elem_ptr);
        
        case ValueType::Struct: {
            auto *si = static_cast<StructInfo*>(bound->info()->element_meta);
            BoundStruct *bs = new BoundStruct("elem", elem_ptr, si);
            return StructProxy_New(bs);
        }
        
        case ValueType::Vector: {
            auto *vi = static_cast<VectorInfo*>(bound->info()->element_meta);
            BoundVector *bv = new BoundVector("elem", elem_ptr, vi);
            return VectorProxy_New(bv);
        }

Output: PyObject* (Python value representation)
```

**Example - Access Nested Vector:**
```python
# C++: std::vector<std::vector<int>> grid = { {1, 2}, {3, 4} }
# Python:

cpp.grid[0]          # Calls VectorProxy_getitem with index=0
├─→ elem_ptr = &grid[0]  // Address of first inner vector
├─→ element_type = Vector
├─→ Create new BoundVector wrapping &grid[0]
└─→ Return VectorProxy wrapping that

cpp.grid[0][1]       # Calls VectorProxy_getitem on inner proxy with index=1
├─→ elem_ptr = &grid[0][1]  // Address of second element: 2
├─→ element_type = Int
└─→ Return PyLong(2)

# Result: 2
```

#### `PyObject *VectorProxy_setitem(PyObject *self, Py_ssize_t index, PyObject *value)`

**Purpose:** Handle `vector_proxy[i] = value` write access.

**Execution Flow (Key Parts):**

```
Input:  self (VectorProxy), index, value

[... validation and indexing same as getitem ...]

Step 4: Type-specific assignment
        
        case ValueType::Int: {
            if (!PyLong_Check(value)) return error;
            long v = PyLong_AsLong(value);
            *(int*)elem_ptr = (int)v;
            return 0;
        }
        
        case ValueType::String: {
            if (!PyUnicode_Check(value)) return error;
            const char *str = PyUnicode_AsUTF8(value);
            auto *s = static_cast<std::string*>(elem_ptr);
            *s = str;  // Copy assign
            return 0;
        }
        
        case ValueType::Struct: {
            if (!PyObject_TypeCheck(value, &StructProxyType)) return error;
            auto *value_proxy = (StructProxy *)value;
            auto *elem_struct = static_cast<Player*>(elem_ptr);
            *elem_struct = *BoundStruct::instance(value_proxy->bound);
            return 0;
        }

Output: 0 (success) or -1 (error)
```

#### `Py_ssize_t VectorProxy_len(PyObject *self)`

**Purpose:** Support `len(vector_proxy)`.

**Implementation:**
```cpp
Py_ssize_t VectorProxy_len(PyObject *self) {
    auto *proxy = (VectorProxy *)self;
    return (Py_ssize_t)proxy->bound->size();
}
```

#### `PyObject *VectorProxy_append(PyObject *self, PyObject *args)`

**Purpose:** Support `vector_proxy.append(value)`.

**Execution Flow:**

```
Input:  self (VectorProxy), args (Python tuple of arguments, expect 1 element)

Step 1: Parse arguments
        PyObject *value = nullptr;
        if (!PyArg_ParseTuple(args, "O", &value)) return nullptr;

Step 2: Type-dispatch append based on element type
        const VectorInfo *vinfo = bound->info();
        
        case ValueType::Int: {
            if (!PyLong_Check(value)) raise TypeError;
            int v = PyLong_AsLong(value);
            bool ok = bound->append_from_cpp(&v);
        }
        
        case ValueType::Struct: {
            if (!PyObject_TypeCheck(value, &StructProxyType)) raise TypeError;
            auto *proxy = (StructProxy *)value;
            Player instance = *BoundStruct::instance(proxy->bound);
            bool ok = bound->append_from_cpp(&instance);
        }
        
        case ValueType::Vector: {
            if (!PyObject_TypeCheck(value, &VectorProxyType)) raise TypeError;
            auto *proxy = (VectorProxy *)value;
            [... complex nested logic ...]
            bool ok = bound->append_from_cpp(&inner_vector);
        }

Output: None (Python None object) or NULL (error)
```

#### `PyObject *VectorProxy_append_new(PyObject *self, PyObject *args)`

**Purpose:** Allocate default struct element and append.

**Example Use Case:**
```python
enemies.append_new()  # Creates Enemy() with default values and appends
```

**Execution Flow:**

```
Input:  self (VectorProxy)

Step 1: Verify element type is Struct
        if (vinfo->element_type != ValueType::Struct) raise TypeError;

Step 2: Allocate memory for new struct
        const StructInfo *si = static_cast<StructInfo*>(vinfo->element_meta);
        size_t elem_size = calculate_struct_size(si);
        void *new_elem = malloc(elem_size);

Step 3: Initialize with defaults
        new_elem should be zero-initialized for:
        - Int fields: 0
        - Float fields: 0.0
        - Bool fields: FALSE_BYTE
        - String fields: empty string (requires placement-new)
        - Struct fields: recursively initialized
        
        for each field in si->fields:
            if field.type == String:
                new (&field_at_offset) std::string();  // Placement-new!

Step 4: Append to vector
        bool ok = bound->append_from_cpp(new_elem);

Step 5: Clean up and return
        free(new_elem);  // Vector made its own copy
        return StructProxy_New(new BoundStruct(...));

Output: StructProxy (proxy wrapping newly appended element)
```

**Memory Flow:**
```
Stack buffer with default values
    ↓
Copy to vector (vector's copy-assignment)
    ↓
Free stack buffer
    ↓
Return proxy to newly-created vector element
```

#### `PyObject *VectorProxy_append_new_vector(PyObject *self, PyObject *args)`

**Purpose:** Allocate default vector element and append (for nested vectors).

**Example Use Case:**
```python
grid.append_new()  # Appends empty vector<int>()
```

**Key Insight:** Uses VectorInfo's function pointers to allocate inner vector

---

### Iterator Protocol Implementation

#### `PyObject *VectorProxy_iter(PyObject *self)`

**Purpose:** Support `iter(vector_proxy)` and `for item in vector_proxy:` loops.

**Implementation:**
```cpp
PyObject *VectorProxy_iter(PyObject *self) {
    auto *it = PyObject_New(VectorIteratorObject, &VectorIteratorType);
    it->vector = (VectorProxy*)self;
    it->index = 0;
    Py_INCREF((PyObject*)self);  // Keep vector alive for duration of iteration
    return (PyObject*)it;
}
```

**Python Behavior Enabled:**
```python
for enemy in cpp.enemies:
    print(enemy.name)

# Behind the scenes:
it = iter(cpp.enemies)          # Calls VectorProxy_iter
while True:
    try:
        enemy = next(it)         # Calls VectorIterator_next
        print(enemy.name)
    except StopIteration:
        break
```

#### `PyObject *VectorIterator_next(PyObject *self)`

**Purpose:** Return next element in iteration (called by `next(iterator)`).

**Execution Flow:**

```
Input:  self (VectorIteratorObject)

Step 1: Unpack iterator state
        auto *it = (VectorIteratorObject *)self;
        auto *proxy = it->vector;
        size_t current_size = proxy->bound->size();

Step 2: Check if iteration complete
        if ((size_t)it->index >= current_size) {
            PyErr_SetNone(PyExc_StopIteration);
            return nullptr;  // Signals end to Python for-loop
        }

Step 3: Get current element
        PyObject *result = VectorProxy_getitem(
            (PyObject*)proxy, 
            it->index
        );

Step 4: Advance iterator
        it->index++;

Output: PyObject* (next element) or NULL with StopIteration (end)
```

**State Machine:**
```
Initially:  it->index = 0
Loop 1:     Return element[0], index becomes 1
Loop 2:     Return element[1], index becomes 2
...
Loop N:     Return element[N-1], index becomes N
Loop N+1:   index (N) >= size (N) → StopIteration, end loop
```

---

## V. Type Conversion and Binding

### value_interface.hpp: Registry and Binding

#### `template <typename T> static void PyInterface::bind(const std::string &name, T &variable)`

**Purpose:** Register a C++ variable for Python access.

**Compile-Time Dispatch:**

```cpp
template <typename T>
static void bind(const std::string &name, T &variable) {
    if constexpr (is_reflected_struct<T>::value) {
        // Brand 1: User-defined struct → BoundStruct
        g_values[name] = std::make_unique<BoundStruct>(
            name, 
            &variable, 
            get_struct_info<T>()
        );
    } 
    else if constexpr (is_std_vector<T>::value) {
        // Branch 2: std::vector<Element>
        using Element = typename T::value_type;
        
        g_values[name] = std::make_unique<BoundVector>(
            name,
            &variable,
            get_vector_info<Element>()  // Dispatch on element type
        );
    }
    else if constexpr (std::is_same_v<T, int>) {
        // Branch 3: Scalar int
        g_values[name] = std::make_unique<PyBoundInt>(name, variable);
    }
    // ... more scalar branches ...
}
```

**Compile-Time Behavior:**

```
bind("player", player_instance)
├─→ is_reflected_struct<Player>::value = true
└─→ Only "Branch 1" code is compiled
    (Branches 2-4 completely eliminated)

bind("scores", scores_vector)
├─→ is_reflected_struct<decltype(scores_vector)> = false
├─→ is_std_vector<decltype(scores_vector)> = true
└─→ Only "Branch 2" code is compiled
    (Branches 1, 3-4 eliminated)

bind("mode", mode_int)
├─→ is_reflected_struct<int> = false
├─→ is_std_vector<int> = false
├─→ std::is_same_v<int, int> = true
└─→ Only "Branch 3" code is compiled
    (Branches 1-2, 4 eliminated)

Result: Zero runtime overhead!
Each call compiles to specific instantiation.
```

#### `static BoundValue *PyInterface::get_value_raw(const std::string &name)`

**Purpose:** Look up bound variable by name.

**Implementation:**
```cpp
static BoundValue *get_value_raw(const std::string &name) {
    auto it = g_values.find(name);
    return it != g_values.end() ? it->second.get() : nullptr;
}
```

**Performance:** O(1) average (unordered_map hash lookup).

---

### python_bind.hpp: Scalar Type Conversions

#### `PyObject *PyBoundInt::to_python()`

**Purpose:** Convert `int*` to Python int.

**Implementation:**
```cpp
PyObject *PyBoundInt::to_python() {
    return PyLong_FromLong(*m_ptr);
}
```

**Example:**
```cpp
int health = 100;
PyBoundInt bound_health("health", health);
PyObject *py_int = bound_health.to_python();
// py_int represents Python: 100 (int)
```

#### `bool PyBoundInt::from_python(PyObject *obj)`

**Purpose:** Convert Python int to `int*`.

**Implementation:**
```cpp
bool PyBoundInt::from_python(PyObject *obj) {
    if (!PyLong_Check(obj)) return false;
    
    long value = PyLong_AsLong(obj);
    if (PyErr_Occurred()) return false;  // Overflow/error
    
    *m_ptr = (int)value;  // Write to original memory
    return true;
}
```

**Example:**
```python
# Python code:
cpp.mode = 42

# Execution:
PyBoundInt::from_python(py_42);
├─→ PyLong_Check(py_42) = true
├─→ long value = 42
└─→ *m_ptr = 42  (writes to original int)
```

#### `PyObject *PyBoundString::to_python()`

**Purpose:** Convert `std::string*` to Python str.

**Implementation:**
```cpp
PyObject *PyBoundString::to_python() {
    return PyUnicode_FromString(m_ptr->c_str());
}
```

#### `bool PyBoundString::from_python(PyObject *obj)`

**Purpose:** Convert Python str to `std::string*`.

**Critical Safety Pattern:**
```cpp
bool PyBoundString::from_python(PyObject *obj) {
    if (!PyUnicode_Check(obj)) return false;
    
    // Get UTF-8 C string from Python Unicode object
    // THIS POINTER POINTS TO PYTHON'S INTERNAL BUFFER
    const char *str = PyUnicode_AsUTF8(obj);
    if (!str) return false;
    
    // ✓ SAFE: std::string constructor immediately copies
    *m_ptr = str;  // String copy assignment operator
    
    // At next Python operation, we no longer need the pointer
    // Python may move/reallocate the buffer
    return true;
}
```

**Why This Pattern Works:**
1. `PyUnicode_AsUTF8()` returns pointer to internal Python buffer
2. Immediately call `operator=()` which copies data
3. By the time we call any Python C-API function, we own a copy
4. Python is free to reallocate/move its buffer

**Wrong Pattern (Segmentation Fault):**
```cpp
const char *str = PyUnicode_AsUTF8(obj);
m_c_str = str;  // ← Just storing pointer!
// ...later...
PyObject *other = PyUnicode_FromString("X");  // Python reallocates!
// m_c_str now points to freed memory
```

---

## VI. Memory Management and Lifetime

### Ownership Model

**Scalars (int, float, bool, std::string):**
- Owned by: User code (main.cpp)
- Lifetime: Variable scope
- PyBoundInt/Float/Bool/String: Hold raw pointer, no responsibility

**Structs:**
- Owned by: User code
- Lifetime: Variable scope or container lifetime
- BoundStruct: Holds void* pointer (never deletes)
- StructProxy: Wraps BoundStruct, does not own

**Vectors:**
- Owned by: User code (main.cpp)
- Lifetime: Variable scope
- BoundVector: Holds void* to vector (never deletes)
- VectorProxy: Wraps BoundVector, does not own

**VectorIterator:**
- Owns: Reference to VectorProxy
- Ensures vector stays valid during iteration
- Uses `Py_INCREF`/`Py_DECREF` for reference counting

### No Heap Allocation in Reflection Layer

**Reflection has NO new/delete:**
```cpp
// reflection_struct.hpp
class BoundStruct {
    void *m_instance;           // Never deleted
    const StructInfo *m_info;   // Pointer to static metadata
};

// reflection_vector.hpp
class BoundVector {
    void *m_vec_ptr;            // Never deleted
    const VectorInfo *m_info;   // Pointer to static metadata
};
```

**Python Layer Allocates Proxies:**
```cpp
// python_proxy.cpp
StructProxy *proxy = PyObject_New(StructProxy, &StructProxyType);
proxy->bound = new BoundStruct(...);  // ← Allocated in Python extension

// When proxy is deleted:
StructProxy_dealloc(PyObject *self) {
    delete ((StructProxy*)self)->bound;
    PyObject_Del(self);
}
```

---

## VII. Summary: Data Flow Example

### Complete Example Walkthrough: `cpp.enemies[0].health = 50`

```
Python Source Code:
    cpp.enemies[0].health = 50

Step 1: Module Access (cpp_module_getattr)
    Python: cpp.enemies
    → cpp_module_getattr(cpp_module, "enemies")
    → BoundVector *bound = get_value_raw("enemies")
    → VectorProxy *proxy = VectorProxy_New(bound)
    → Return proxy to Python

Step 2: Indexing (VectorProxy_setitem)
    Python: proxy[0]
    → VectorProxy_getitem(proxy, 0)
    → void *elem_ptr = bound->element_ptr(0)
    → element_ptr returns address of enemies[0]
    → element_type = Struct
    → StructProxy *inner = StructProxy_New(BoundStruct(..., elem_ptr, ...))
    → Return inner proxy to Python

Step 3: Field Access (StructProxy_setattro)
    Python: inner.health = 50
    → StructProxy_setattro(inner, "health", py_50)
    → const FieldInfo *field = get_field("health")
    → void *field_ptr = get_field_ptr(field)
    → field_ptr = elem_ptr + field->offset
    → PyLong_AsLong(py_50) = 50
    → *(int*)field_ptr = 50
    → Return 0 (success)

Result:
    enemies[0].health in C++ is now 50
    Python and C++ share the same memory
```

### Memory Map at Execution

```
Main Memory (conceptual):
┌─────────────────────────────────────────┐
│ Arena: std::vector<Enemy>               │
├─────────────────────────────────────────┤
│ [0] Enemy { health: 50, name: "Goblin"} │  ← Modified by Python!
│ [1] Enemy { health: 120, name: "Orc"  } │
│ [2] Enemy { health: 80, name: "Troll" } │
└─────────────────────────────────────────┘
     ↑
bound->element_ptr(0) points here

Python Proxy Stack:
  StructProxy {
      bound: BoundStruct {
          m_instance: &enemies[0],
          m_info: &enemy_struct_info
      }
  }

When we read .health:
  health_ptr = m_instance + offset_of_health = &(enemies[0].health)
  value = *(int*)health_ptr = 50
```

This completes the comprehensive function reference.
