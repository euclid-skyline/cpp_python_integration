# Complete Fix Documentation

## Fix 1: Root proxy cannot expose structs/vectors

**Problem:** `cppproxy_getattro`/`cppproxy_setattro` only use `PyInterface::get_value()` which returns `PyBoundValue*` via `dynamic_cast`. When `bind()` stores `BoundStruct` or `BoundVector` (not `PyBoundValue`), the cast returns `nullptr` → `AttributeError`.

**File:** `python_proxy.cpp` lines 25-61

**Current Code:**
```cpp
static PyObject *cppproxy_getattro(PyObject *, PyObject *attr)
{
    const char *name = PyUnicode_AsUTF8(attr);

    // [C++20 FIX] use PyInterface::get_value wrapper (added for compatibility)
    PyBoundValue *val = PyInterface::get_value(name);

    if (!val)
    {
        PyErr_Format(PyExc_AttributeError, "Unknown C++ variable '%s'", name);
        return nullptr;
    }

    return val->to_python();
}
```

**Fixed Code:**
```cpp
static PyObject *cppproxy_getattro(PyObject *, PyObject *attr)
{
    const char *name = PyUnicode_AsUTF8(attr);

    // Use get_value_raw() to get any BoundValue (scalar, struct, or vector)
    BoundValue *val = PyInterface::get_value_raw(name);

    if (!val)
    {
        PyErr_Format(PyExc_AttributeError, "Unknown C++ variable '%s'", name);
        return nullptr;
    }

    // Dispatch based on actual type
    switch (val->type)
    {
    case ValueType::Struct:
        return StructProxy_New(static_cast<BoundStruct *>(val));
    
    case ValueType::Vector:
        return VectorProxy_New(static_cast<BoundVector *>(val));
    
    default:
        // For scalar types, use PyBoundValue interface
        PyBoundValue *pyval = dynamic_cast<PyBoundValue *>(val);
        if (!pyval)
        {
            PyErr_Format(PyExc_RuntimeError, "Internal error: scalar type not PyBoundValue");
            return nullptr;
        }
        return pyval->to_python();
    }
}
```

**Current Code (setattro):**
```cpp
static int cppproxy_setattro(PyObject *, PyObject *attr, PyObject *value)
{
    const char *name = PyUnicode_AsUTF8(attr);

    // [C++20 FIX] use PyInterface::get_value wrapper
    PyBoundValue *val = PyInterface::get_value(name);

    if (!val)
    {
        PyErr_Format(PyExc_AttributeError, "Unknown C++ variable '%s'", name);
        return -1;
    }

    if (!val->from_python(value))
    {
        PyErr_Format(PyExc_TypeError, "Type mismatch for '%s'", name);
        return -1;
    }

    return 0;
}
```

**Fixed Code (setattro):**
```cpp
static int cppproxy_setattro(PyObject *, PyObject *attr, PyObject *value)
{
    const char *name = PyUnicode_AsUTF8(attr);

    BoundValue *val = PyInterface::get_value_raw(name);

    if (!val)
    {
        PyErr_Format(PyExc_AttributeError, "Unknown C++ variable '%s'", name);
        return -1;
    }

    // Structs and vectors cannot be reassigned (only modified via their proxy)
    if (val->type == ValueType::Struct || val->type == ValueType::Vector)
    {
        PyErr_Format(PyExc_TypeError, "Cannot reassign struct or vector '%s'", name);
        return -1;
    }

    // For scalar types
    PyBoundValue *pyval = dynamic_cast<PyBoundValue *>(val);
    if (!pyval)
    {
        PyErr_Format(PyExc_RuntimeError, "Internal error: scalar type not PyBoundValue");
        return -1;
    }

    if (!pyval->from_python(value))
    {
        PyErr_Format(PyExc_TypeError, "Type mismatch for '%s'", name);
        return -1;
    }

    return 0;
}
```

---

## Fix 2: Vector-of-struct and vector-of-vector storage mismatch

**Problem:** `BoundVector` assumes `std::vector<std::byte>` for structs and `std::vector<void*>` for nested vectors, but actual data are `std::vector<Enemy>` and `std::vector<std::vector<int>>`.

**File:** `reflection_vector.hpp` (entire class needs redesign)

**Solution Approach:** Use type-erased operations with proper casting OR make `BoundVector` templated and specialize for each case.

**Fixed Code (simplified type-correct approach):**

```cpp
// In reflection_vector.hpp - replace BoundVector::size()
std::size_t size() const
{
    switch (m_info->element_type)
    {
    case ValueType::Int:
        return reinterpret_cast<const std::vector<int> *>(m_vec_ptr)->size();
    case ValueType::Float:
        return reinterpret_cast<const std::vector<float> *>(m_vec_ptr)->size();
    case ValueType::Bool:
        return reinterpret_cast<const std::vector<ByteBool> *>(m_vec_ptr)->size();
    case ValueType::String:
        return reinterpret_cast<const std::vector<std::string> *>(m_vec_ptr)->size();

    case ValueType::Struct:
    {
        // CORRECTED: Actually interpret as std::vector of the struct type
        // We need to know the actual struct type at runtime
        // This requires storing element size in VectorInfo or using a callback
        const StructInfo *sinfo =
            static_cast<const StructInfo *>(m_info->element_meta);
        
        // Store element size in VectorInfo for correct interpretation
        // For now, we can use a function pointer approach:
        // Add to VectorInfo: size_t (*get_size_fn)(void*);
        // Set in data_game_traits.hpp for each vector type
        
        // Temporary workaround: store actual vector type info
        // This is a fundamental design issue - need type info at bind time
        
        // Proper fix requires changing VectorInfo to include size callback
        // For Enemy vector specifically:
        return reinterpret_cast<const std::vector<Enemy> *>(m_vec_ptr)->size();
        // But this won't work generically without knowing Enemy type here
        
        // BEST FIX: Add function pointer to VectorInfo
        return m_info->size_fn ? m_info->size_fn(m_vec_ptr) : 0;
    }

    case ValueType::Vector:
        // CORRECTED: std::vector<std::vector<int>> not std::vector<void*>
        return reinterpret_cast<const std::vector<std::vector<int>> *>(m_vec_ptr)->size();
        // Same issue - needs generic solution via function pointer

    default:
        return 0;
    }
}
```

**Better Solution - Add function pointers to VectorInfo:**

```cpp
// In reflection_vector.hpp - UPDATE VectorInfo struct
struct VectorInfo
{
    ValueType element_type;
    void *element_meta;
    
    // Function pointers for type-erased operations
    std::size_t (*size_fn)(void* vec_ptr);
    void* (*element_ptr_fn)(void* vec_ptr, std::size_t index);
    bool (*append_fn)(void* vec_ptr, void* value_ptr);
};

// Then in data_game_traits.hpp - provide implementations
// For std::vector<int>:
static std::size_t int_vec_size(void* ptr) {
    return reinterpret_cast<std::vector<int>*>(ptr)->size();
}
static void* int_vec_element_ptr(void* ptr, std::size_t idx) {
    return &(*reinterpret_cast<std::vector<int>*>(ptr))[idx];
}
static bool int_vec_append(void* ptr, void* val) {
    reinterpret_cast<std::vector<int>*>(ptr)->push_back(*static_cast<int*>(val));
    return true;
}

static VectorInfo IntVectorInfo = {
    ValueType::Int,
    nullptr,
    int_vec_size,
    int_vec_element_ptr,
    int_vec_append
};

// For std::vector<Enemy>:
static std::size_t enemy_vec_size(void* ptr) {
    return reinterpret_cast<std::vector<Enemy>*>(ptr)->size();
}
static void* enemy_vec_element_ptr(void* ptr, std::size_t idx) {
    return &(*reinterpret_cast<std::vector<Enemy>*>(ptr))[idx];
}
static bool enemy_vec_append(void* ptr, void* val) {
    reinterpret_cast<std::vector<Enemy>*>(ptr)->push_back(*static_cast<Enemy*>(val));
    return true;
}

static VectorInfo EnemyVectorInfo = {
    ValueType::Struct,
    &EnemyInfo,
    enemy_vec_size,
    enemy_vec_element_ptr,
    enemy_vec_append
};

// For std::vector<std::vector<int>>:
static std::size_t grid_vec_size(void* ptr) {
    return reinterpret_cast<std::vector<std::vector<int>>*>(ptr)->size();
}
static void* grid_vec_element_ptr(void* ptr, std::size_t idx) {
    return &(*reinterpret_cast<std::vector<std::vector<int>>*>(ptr))[idx];
}
static bool grid_vec_append(void* ptr, void* val) {
    reinterpret_cast<std::vector<std::vector<int>>*>(ptr)->push_back(
        *static_cast<std::vector<int>*>(val));
    return true;
}

static VectorInfo VectorOfIntVectorInfo = {
    ValueType::Vector,
    &IntVectorInfo,
    grid_vec_size,
    grid_vec_element_ptr,
    grid_vec_append
};
```

**Then simplify BoundVector to use function pointers:**

```cpp
// In reflection_vector.hpp
std::size_t size() const
{
    return m_info->size_fn ? m_info->size_fn(m_vec_ptr) : 0;
}

void *element_ptr(std::size_t index) const
{
    return m_info->element_ptr_fn ? m_info->element_ptr_fn(m_vec_ptr, index) : nullptr;
}

bool append_from_cpp(void *value_ptr)
{
    return m_info->append_fn ? m_info->append_fn(m_vec_ptr, value_ptr) : false;
}
```

---

## Fix 3: Nested vector append wrong pointer

**Problem:** In `VectorProxy_append` for `ValueType::Vector`, passes `&inner_raw` (address of pointer) instead of the pointer itself.

**File:** `python_proxy.cpp` lines 362-372

**Current Code:**
```cpp
case ValueType::Vector:
{
    if (!PyObject_TypeCheck(value, &VectorProxyType))
    {
        PyErr_SetString(PyExc_TypeError, "Expected VectorProxy");
        return nullptr;
    }
    auto *vp = reinterpret_cast<VectorProxyObject *>(value);
    BoundVector *inner = vp->bound;
    void *inner_raw = inner->raw_vector();
    vec->append_from_cpp(&inner_raw); // WRONG: passing void**
    break;
}
```

**Fixed Code:**
```cpp
case ValueType::Vector:
{
    if (!PyObject_TypeCheck(value, &VectorProxyType))
    {
        PyErr_SetString(PyExc_TypeError, "Expected VectorProxy");
        return nullptr;
    }
    auto *vp = reinterpret_cast<VectorProxyObject *>(value);
    BoundVector *inner = vp->bound;
    void *inner_raw = inner->raw_vector();
    vec->append_from_cpp(inner_raw); // FIXED: passing void* directly
    break;
}
```

**However**, with the function pointer fix above, this becomes:
```cpp
case ValueType::Vector:
{
    if (!PyObject_TypeCheck(value, &VectorProxyType))
    {
        PyErr_SetString(PyExc_TypeError, "Expected VectorProxy");
        return nullptr;
    }
    auto *vp = reinterpret_cast<VectorProxyObject *>(value);
    BoundVector *inner = vp->bound;
    // append_fn expects a pointer to the actual vector object
    vec->append_from_cpp(inner->raw_vector());
    break;
}
```

---

## Fix 4: Memory leaks from wrapper allocations

**Problem:** Every field/element access allocates new `BoundStruct`/`BoundVector` and wrapper objects with `new`, but there's no cleanup.

**Files:** `python_proxy.cpp` (StructProxyType, VectorProxyType) and `value_interface.cpp` (wrap_field, wrap_vector_element)

**Fix Part A: Add tp_dealloc for StructProxyType**

Add before `StructProxyType` definition in `python_proxy.cpp`:

```cpp
// Add after StructProxyObject definition
static void StructProxy_dealloc(PyObject *self)
{
    StructProxyObject *proxy = (StructProxyObject *)self;
    
    // Delete the BoundStruct wrapper
    delete proxy->bound;
    
    // Free the Python object
    PyObject_Del(self);
}
```

Then update `StructProxyType`:
```cpp
PyTypeObject StructProxyType = {
    PyVarObject_HEAD_INIT(NULL, 0) "cpp.StructProxy",
    sizeof(StructProxyObject),
    0,
    StructProxy_dealloc,  // CHANGED: was 0, now StructProxy_dealloc
    // ... rest unchanged
```

**Fix Part B: Add tp_dealloc for VectorProxyType**

Add before `VectorProxyType` definition:

```cpp
static void VectorProxy_dealloc(PyObject *self)
{
    VectorProxyObject *proxy = (VectorProxyObject *)self;
    
    // Delete the BoundVector wrapper
    delete proxy->bound;
    
    // Free the Python object
    PyObject_Del(self);
}
```

Then update `VectorProxyType`:
```cpp
PyTypeObject VectorProxyType = {
    PyVarObject_HEAD_INIT(NULL, 0) "cpp.VectorProxy",
    sizeof(VectorProxyObject),
    0,
    VectorProxy_dealloc,  // CHANGED: was 0, now VectorProxy_dealloc
    // ... rest unchanged
```

**Fix Part C: Delete wrapper in wrap_field and wrap_vector_element**

In `value_interface.cpp`, the `PyBoundStructProxy` and `PyBoundVectorProxy` local structs also get leaked. Add destructors:

```cpp
// In PyInterface::wrap_field() - Struct case
struct PyBoundStructProxy : PyBoundValue
{
    BoundStruct *bs;

    PyBoundStructProxy(const std::string &n, BoundStruct *b)
    {
        name = n;
        type = ValueType::Struct;
        bs = b;
    }
    
    ~PyBoundStructProxy() override
    {
        // Note: bs is owned by StructProxy, don't delete here
        // OR change ownership model
    }

    PyObject *to_python() override
    {
        return StructProxy_New(bs);
    }

    bool from_python(PyObject *) override
    {
        return false;
    }
};
```

**Better Fix Part C: Don't allocate wrapper at all**

Since these wrappers are immediately used then discarded, we can avoid heap allocation:

```cpp
// In value_interface.cpp - replace entire wrap_field function
PyBoundValue *PyInterface::wrap_field(const FieldInfo *field, void *fieldPtr)
{
    switch (field->type)
    {
    case ValueType::Int:
    case ValueType::Float:
    case ValueType::Bool:
    case ValueType::String:
        // Keep as-is for scalar types
        // ... existing code ...
        
    case ValueType::Struct:
    case ValueType::Vector:
        // Don't create wrapper - caller should handle directly
        // This requires changing the interface
        return nullptr;
        
    default:
        return nullptr;
    }
}
```

**Actually, the real fix is to change the proxy getattro/setattro:**

```cpp
// In StructProxy_getattro - FIXED VERSION
static PyObject *StructProxy_getattro(PyObject *self, PyObject *attr)
{
    StructProxyObject *proxy = (StructProxyObject *)self;
    const char *name = PyUnicode_AsUTF8(attr);
    const FieldInfo *field = proxy->bound->get_field(name);

    if (!field)
    {
        PyErr_Format(PyExc_AttributeError, "Unknown field '%s'", name);
        return nullptr;
    }

    void *fieldPtr = proxy->bound->get_field_ptr(field);

    // Handle directly based on field type
    switch (field->type)
    {
    case ValueType::Int:
        return PyLong_FromLong(*static_cast<int*>(fieldPtr));
    
    case ValueType::Float:
        return PyFloat_FromDouble(*static_cast<float*>(fieldPtr));
    
    case ValueType::Bool:
    {
        ByteBool b = *static_cast<ByteBool*>(fieldPtr);
        return PyBool_FromLong((b != FALSE_BYTE) ? 1 : 0);
    }
    
    case ValueType::String:
        return PyUnicode_FromString(static_cast<std::string*>(fieldPtr)->c_str());
    
    case ValueType::Struct:
    {
        const StructInfo *sinfo = static_cast<const StructInfo *>(field->type_meta);
        BoundStruct *bstruct = new BoundStruct(field->name, fieldPtr, sinfo);
        return StructProxy_New(bstruct);
    }
    
    case ValueType::Vector:
    {
        const VectorInfo *vinfo = static_cast<const VectorInfo *>(field->type_meta);
        BoundVector *bvec = new BoundVector(field->name, fieldPtr, vinfo);
        return VectorProxy_New(bvec);
    }
    
    default:
        PyErr_SetString(PyExc_RuntimeError, "Unsupported field type");
        return nullptr;
    }
}
```

Similar for `StructProxy_setattro` and `VectorProxy_getitem`/`VectorProxy_setitem`.

---

## Fix 5: Missing error propagation on assignment failure

**Problem:** When `from_python()` returns false, no Python exception is set.

**File:** `python_proxy.cpp` line 173 and line 274

**Current Code (StructProxy_setattro):**
```cpp
return val->from_python(value) ? 0 : -1;
```

**Fixed Code:**
```cpp
if (!val->from_python(value))
{
    // Ensure exception is set
    if (!PyErr_Occurred())
    {
        PyErr_Format(PyExc_TypeError, "Failed to set field '%s'", name);
    }
    delete val;  // Clean up leaked wrapper
    return -1;
}
delete val;  // Clean up on success too
return 0;
```

**Similar fix for VectorProxy_setitem:**
```cpp
if (!val->from_python(value))
{
    if (!PyErr_Occurred())
    {
        PyErr_SetString(PyExc_TypeError, "Failed to set vector element");
    }
    delete val;
    return -1;
}
delete val;
return 0;
```

---

## Fix 6: Multiple definition errors from globals in header

**Problem:** `scores`, `enemies`, `grid` declared in header with external linkage.

**File:** `data_game_traits.hpp`

**Current Code:**
```cpp
std::vector<int> scores;
std::vector<Enemy> enemies;
std::vector<std::vector<int>> grid;
```

**Fix Option 1: Make inline (C++17+):**
```cpp
inline std::vector<int> scores;
inline std::vector<Enemy> enemies;
inline std::vector<std::vector<int>> grid;
```

**Fix Option 2: Move to cpp file (better):**

Create `data_game_traits.cpp`:
```cpp
#include "data_game_traits.hpp"

// Define global vectors
std::vector<int> scores;
std::vector<Enemy> enemies;
std::vector<std::vector<int>> grid;
```

In `data_game_traits.hpp`:
```cpp
// Declare as extern
extern std::vector<int> scores;
extern std::vector<Enemy> enemies;
extern std::vector<std::vector<int>> grid;
```

Add to `CMakeLists.txt`:
```cmake
add_executable(EmbeddedPythonLoop 
                main.cpp
                cpp_module.cpp
                python_proxy.cpp 
                value_interface.cpp
                data_game_traits.cpp  # ADD THIS
                )
```

---

## Fix 7: Curses not shut down on exit

**Problem:** `initscr()` called but never `endwin()`.

**File:** `main.cpp`

**Current Code:**
```cpp
// ---------------------------------------------------------
// Cleanup
// ---------------------------------------------------------
Py_DECREF(updateFunc);
Py_DECREF(module);
Py_Finalize();

std::cout << "\nProgram terminated.\n";
return 0;
```

**Fixed Code:**
```cpp
// ---------------------------------------------------------
// Cleanup
// ---------------------------------------------------------
endwin();  // ADD THIS - restore terminal state

Py_DECREF(updateFunc);
Py_DECREF(module);
Py_Finalize();

std::cout << "\nProgram terminated.\n";
return 0;
```

**Also add cleanup on error paths:**
```cpp
if (!module)
{
    PyErr_Print();
    std::cerr << "Failed to import Python module controller.py\n";
    endwin();  // ADD THIS
    Py_Finalize();
    return 1;
}

// ... later ...

if (!updateFunc || !PyCallable_Check(updateFunc))
{
    std::cerr << "Function update_values() not found or not callable\n";
    Py_XDECREF(updateFunc);
    Py_DECREF(module);
    endwin();  // ADD THIS
    Py_Finalize();
    return 1;
}
```

---

## Fix 8: Struct/Vector proxy deletes global bindings (missing output after first access)

**Problem:** `cpp.player` and `cpp.scores` proxies were constructed with the same `BoundStruct`/`BoundVector` stored in `PyInterface::g_values`. The proxy destructors delete their `bound` pointer, so the first proxy GC destroys the global binding. Subsequent accesses return invalid pointers or silently stop producing output.

**Files:** `cpp_module.cpp`, `python_proxy.cpp`

**Current Code (in module attribute lookup):**
```cpp
case ValueType::Struct:
    return StructProxy_New(static_cast<BoundStruct *>(val));

case ValueType::Vector:
    return VectorProxy_New(static_cast<BoundVector *>(val));
```

**Fixed Code (wrap bound values before returning proxy):**
```cpp
case ValueType::Struct:
{
    auto *bs = static_cast<BoundStruct *>(val);
    // Create a wrapper that the proxy can own and delete safely
    BoundStruct *wrapper = new BoundStruct(bs->name, bs->instance(), bs->info());
    return StructProxy_New(wrapper);
}

case ValueType::Vector:
{
    auto *bv = static_cast<BoundVector *>(val);
    // Create a wrapper that the proxy can own and delete safely
    BoundVector *wrapper = new BoundVector(bv->name, bv->raw_vector(), bv->info());
    return VectorProxy_New(wrapper);
}
```

**Why this works:** The proxy owns and deletes only the wrapper, while the original bindings in `PyInterface::g_values` remain valid for the lifetime of the program.

---

## Summary of Changes Required

1. **python_proxy.cpp**:
   - Fix `cppproxy_getattro` to use `get_value_raw()` and dispatch by type
   - Fix `cppproxy_setattro` similarly
   - Add `StructProxy_dealloc` function
   - Add `VectorProxy_dealloc` function
   - Update `StructProxyType.tp_dealloc`
   - Update `VectorProxyType.tp_dealloc`
   - Fix `StructProxy_getattro` to avoid wrapper allocation
   - Fix `StructProxy_setattro` to check exceptions and clean up
   - Fix `VectorProxy_getitem` to avoid wrapper allocation
   - Fix `VectorProxy_setitem` to check exceptions and clean up
   - Fix `VectorProxy_append` Vector case to not pass pointer address

2. **reflection_vector.hpp**:
   - Add function pointers to `VectorInfo` struct
   - Simplify `BoundVector` methods to use function pointers

3. **data_game_traits.hpp** / **data_game_traits.cpp** (new):
   - Provide function pointer implementations for each vector type
   - Move global vector definitions to .cpp file
   - Mark declarations as `extern` in .hpp

4. **main.cpp**:
   - Add `endwin()` calls before all return/exit points

5. **CMakeLists.txt**:
   - Add `data_game_traits.cpp` to sources

Would you like me to generate the complete fixed versions of these files?
