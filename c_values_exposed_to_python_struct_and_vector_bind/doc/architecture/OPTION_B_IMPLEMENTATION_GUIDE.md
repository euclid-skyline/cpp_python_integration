# Option B Implementation: Dynamic Element Resolution

## Overview

Replace raw element pointers with **index + parent vector**, resolving the pointer on each access.

---

## Architecture Changes

### 1. Update BoundStruct to Support Parent Tracking

**File:** `reflection_struct.hpp`

**Current (unsafe):**
```cpp
class BoundStruct : public BoundValue
{
public:
    BoundStruct(const std::string &name, void *instance, const StructInfo *info)
    {
        this->name = name;
        this->type = ValueType::Struct;
        m_instance = instance;
        m_info = info;
    }

    void *instance() const { return m_instance; }

private:
    void *m_instance;           // Raw pointer to struct
    const StructInfo *m_info;
};
```

**Updated (safe):**
```cpp
class BoundStruct : public BoundValue
{
public:
    // Constructor for standalone structs (not from vector)
    BoundStruct(const std::string &name, void *instance, const StructInfo *info)
        : m_instance(instance), m_info(info), m_parent_vector(nullptr), m_element_index(0)
    {
        this->name = name;
        this->type = ValueType::Struct;
    }

    // NEW: Constructor for vector elements
    BoundStruct(const std::string &name, BoundVector *parent, std::size_t index, const StructInfo *info)
        : m_instance(nullptr), m_info(info), m_parent_vector(parent), m_element_index(index)
    {
        this->name = name;
        this->type = ValueType::Struct;
    }

    // Dynamic resolution
    void *instance() const
    {
        if (m_parent_vector)
        {
            // Resolve pointer from current vector state
            return m_parent_vector->element_ptr(m_element_index);
        }
        return m_instance;  // Static pointer for non-vector structs
    }

    // Accessor for field pointers
    void *get_field_ptr(const FieldInfo *f) const
    {
        return reinterpret_cast<char *>(instance()) + f->offset;
    }

    const StructInfo *info() const { return m_info; }

private:
    void *m_instance;               // Raw pointer (for standalone structs)
    const StructInfo *m_info;
    
    // NEW: For vector elements
    BoundVector *m_parent_vector;   // nullptr if not from vector
    std::size_t m_element_index;    // Valid only if m_parent_vector != nullptr
};
```

---

### 2. Update BoundVector Similarly

**File:** `reflection_vector.hpp`

**Add same pattern:**
```cpp
class BoundVector : public BoundValue
{
public:
    // Constructor for standalone vectors
    BoundVector(const std::string &name, void *vec_ptr, const VectorInfo *info)
        : m_vec_ptr(vec_ptr), m_info(info), m_parent_vector(nullptr), m_element_index(0)
    {
        this->name = name;
        this->type = ValueType::Vector;
    }

    // NEW: Constructor for nested vectors (element of another vector)
    BoundVector(const std::string &name, BoundVector *parent, std::size_t index, const VectorInfo *info)
        : m_vec_ptr(nullptr), m_info(info), m_parent_vector(parent), m_element_index(index)
    {
        this->name = name;
        this->type = ValueType::Vector;
    }

    void *raw_vector() const
    {
        if (m_parent_vector)
        {
            // Nested vector: resolve from parent
            return m_parent_vector->element_ptr(m_element_index);
        }
        return m_vec_ptr;  // Top-level vector
    }

    std::size_t size() const
    {
        return m_info->size_fn ? m_info->size_fn(raw_vector()) : 0;
    }

    void *element_ptr(std::size_t index) const
    {
        return m_info->element_ptr_fn ? m_info->element_ptr_fn(raw_vector(), index) : nullptr;
    }

    bool append_from_cpp(void *value_ptr)
    {
        return m_info->append_fn ? m_info->append_fn(raw_vector(), value_ptr) : false;
    }

    const VectorInfo *info() const { return m_info; }

private:
    void *m_vec_ptr;                // Raw pointer (for top-level vectors)
    const VectorInfo *m_info;
    
    // NEW: For nested vectors
    BoundVector *m_parent_vector;   // nullptr if top-level
    std::size_t m_element_index;    // Valid only if m_parent_vector != nullptr
};
```

---

### 3. Update VectorProxy_getitem to Use Parent Constructor

**File:** `python_proxy.cpp`

**Current (unsafe):**
```cpp
static PyObject *VectorProxy_getitem(PyObject *self, Py_ssize_t index)
{
    VectorProxyObject *proxy = (VectorProxyObject *)self;
    // ... bounds checking ...

    void *elemPtr = proxy->bound->element_ptr(index);
    const VectorInfo *info = proxy->bound->info();

    switch (info->element_type)
    {
    case ValueType::Struct:
    {
        const StructInfo *sinfo = static_cast<const StructInfo *>(info->element_meta);
        BoundStruct *bstruct = new BoundStruct(proxy->bound->name, elemPtr, sinfo);  // ❌ Uses raw pointer
        return StructProxy_New(bstruct);
    }
    
    case ValueType::Vector:
    {
        const VectorInfo *vinfo = static_cast<const VectorInfo *>(info->element_meta);
        BoundVector *bvec = new BoundVector(proxy->bound->name, elemPtr, vinfo);  // ❌ Uses raw pointer
        return VectorProxy_New(bvec);
    }
    // ...
    }
}
```

**Updated (safe):**
```cpp
static PyObject *VectorProxy_getitem(PyObject *self, Py_ssize_t index)
{
    VectorProxyObject *proxy = (VectorProxyObject *)self;
    // ... bounds checking ...

    const VectorInfo *info = proxy->bound->info();

    switch (info->element_type)
    {
    case ValueType::Struct:
    {
        const StructInfo *sinfo = static_cast<const StructInfo *>(info->element_meta);
        // ✅ Use parent + index constructor instead of raw pointer
        BoundStruct *bstruct = new BoundStruct(
            proxy->bound->name,
            proxy->bound,           // Parent vector
            static_cast<std::size_t>(index),  // Element index
            sinfo
        );
        return StructProxy_New(bstruct);
    }
    
    case ValueType::Vector:
    {
        const VectorInfo *vinfo = static_cast<const VectorInfo *>(info->element_meta);
        // ✅ Use parent + index constructor
        BoundVector *bvec = new BoundVector(
            proxy->bound->name,
            proxy->bound,           // Parent vector
            static_cast<std::size_t>(index),  // Element index
            vinfo
        );
        return VectorProxy_New(bvec);
    }
    // ...
    }
}
```

---

### 4. Update StructProxy_getattro for Nested Structs/Vectors

**File:** `python_proxy.cpp`

When accessing a struct/vector field of a struct, also use parent tracking:

**Current:**
```cpp
case ValueType::Struct:
{
    const StructInfo *sinfo = static_cast<const StructInfo *>(field->type_meta);
    BoundStruct *bstruct = new BoundStruct(field->name, fieldPtr, sinfo);  // ❌
    return StructProxy_New(bstruct);
}

case ValueType::Vector:
{
    const VectorInfo *vinfo = static_cast<const VectorInfo *>(field->type_meta);
    BoundVector *bvec = new BoundVector(field->name, fieldPtr, vinfo);  // ❌
    return VectorProxy_New(bvec);
}
```

**Updated:**
```cpp
case ValueType::Struct:
{
    const StructInfo *sinfo = static_cast<const StructInfo *>(field->type_meta);
    // For struct fields, we still use raw pointer (parent is a struct, not vector)
    // Only vector elements need parent tracking
    BoundStruct *bstruct = new BoundStruct(field->name, fieldPtr, sinfo);
    return StructProxy_New(bstruct);
}

case ValueType::Vector:
{
    const VectorInfo *vinfo = static_cast<const VectorInfo *>(field->type_meta);
    // Vectors as struct fields also use raw pointer (struct layout is stable)
    BoundVector *bvec = new BoundVector(field->name, fieldPtr, vinfo);
    return VectorProxy_New(bvec);
}
```

**Note:** Struct fields don't need parent tracking because struct memory layout is **stable** (structs don't reallocate).

---

### 5. Update VectorProxy_append_new

**File:** `python_proxy.cpp`

**Current:**
```cpp
std::size_t last_idx = vec->size() - 1;
void *elemPtr = vec->element_ptr(last_idx);
BoundStruct *bstruct = new BoundStruct(vec->name, elemPtr, sinfo);  // ❌
return StructProxy_New(bstruct);
```

**Updated:**
```cpp
std::size_t last_idx = vec->size() - 1;
// ✅ Use parent + index
BoundStruct *bstruct = new BoundStruct(vec->name, vec, last_idx, sinfo);
return StructProxy_New(bstruct);
```

---

### 6. Update VectorProxy_append_new_vector

**File:** `python_proxy.cpp`

**Current:**
```cpp
std::size_t last_idx = vec->size() - 1;
void *elemPtr = vec->element_ptr(last_idx);
BoundVector *bvec = new BoundVector(vec->name, elemPtr, inner_info);  // ❌
return VectorProxy_New(bvec);
```

**Updated:**
```cpp
std::size_t last_idx = vec->size() - 1;
// ✅ Use parent + index
BoundVector *bvec = new BoundVector(vec->name, vec, last_idx, inner_info);
return VectorProxy_New(bvec);
```

---

## Circular Dependency Resolution

### The Problem

When implementing parent tracking, we encountered a classic C++ circular dependency issue:

**Include Structure Problem:**
```
reflection_struct.hpp
    └─ Needs to know about BoundVector class for BoundStruct::instance() method
    └─ Contains forward: class BoundVector;
    
reflection_vector.hpp  
    └─ Includes reflection_struct.hpp (for struct field definitions)
    └─ Contains class BoundVector definition
    
Result: BoundVector is only forward-declared when BoundStruct tries to use it
        → Compilation error: "use of undefined type 'BoundVector'"
```

### The Solution

**Strategy: Two-Phase Include with Deferred Implementation**

1. **Phase 1: Forward Declaration**
   ```cpp
   // reflection_struct.hpp
   class BoundVector;  // Forward declaration only
   
   class BoundStruct {
       void *instance() const;  // Declaration only
       ...
   };
   ```

2. **Phase 2: Full Definition at End of File**
   ```cpp
   // At END of reflection_struct.hpp (after BoundStruct is fully defined):
   #include "reflection_vector.hpp"  // Now safe to include
   
   // Implement BoundStruct::instance() - BoundVector is now fully defined
   inline void *BoundStruct::instance() const
   {
       if (m_parent_vector)
       {
           return m_parent_vector->element_ptr(m_element_index);
       }
       return m_instance;
   }
   ```

3. **reflection_vector.hpp Strategy**
   ```cpp
   // reflection_vector.hpp
   #include "reflection_value.hpp"  // OK
   
   // Forward declarations instead of includes
   class BoundStruct;
   struct StructInfo;
   
   class BoundVector { ... };  // Full definition
   ```

### Why This Works

| Phase | What Happens | BoundVector Status |
|-------|--------------|-------------------|
| **reflection_vector.hpp** loads | Declares VectorInfo, BoundVector completely | **FULLY DEFINED** |
| **reflection_struct.hpp** loads | Forward declares BoundVector, defines BoundStruct | **FORWARD ONLY** |
| End of **reflection_struct.hpp** | Includes reflection_vector.hpp | **FULLY DEFINED** ✓ |
| **instance() inline impl** | Uses BoundVector::element_ptr() | **AVAILABLE** ✓ |

### Key Points

- ✅ **No circular includes** - Each header includes are acyclic
- ✅ **Compile-time safe** - Definitions available when needed
- ✅ **Transparent to users** - Normal #include semantics apply
- ✅ **Inline optimization** - No runtime cost for resolution
- ✅ **Standard C++ pattern** - Used in many libraries (e.g., STL containers)

### Include Order in Practice

When user code includes headers:
```cpp
#include "reflection_struct.hpp"   // Brings in everything transitively:
                                    // - reflection_value.hpp
                                    // - reflection_vector.hpp (at end)
                                    // - Full BoundStruct and BoundVector definitions
```

Result: One include statement gives complete types, no manual ordering needed.

---

## Testing

### Test Case 1: Element Proxy After Append
```cpp
// Python test
enemy = cpp.enemies[0]
cpp.enemies.append_new()  # Vector reallocates
enemy.health = 999        # Should work safely now
assert cpp.enemies[0].health == 999
```

### Test Case 2: Nested Vector Element
```cpp
row = cpp.grid[0]
cpp.grid.append_new_vector()  # Grid reallocates
row.append(100)               # Should work safely
```

### Test Case 3: Multiple Proxies
```cpp
e1 = cpp.enemies[0]
e2 = cpp.enemies[1]
cpp.enemies.append_new()
e1.health = 100  # ✅ Safe
e2.health = 200  # ✅ Safe
```

---

## Performance Considerations

### Overhead
- **One extra pointer dereference** per field access
- Negligible for typical use cases
- Can be optimized with caching if needed

### Memory
- Adds 16 bytes per `BoundStruct`/`BoundVector` (pointer + size_t)
- Acceptable for most applications

---

## Migration Path

1. Update `BoundStruct` / `BoundVector` headers
2. Update all proxy creation sites to use new constructors
3. Test with existing controller.py
4. Add new test cases for reallocation safety
5. Update documentation

---

## Backwards Compatibility

This change is **internal only**:
- Python API unchanged
- Existing Python code works without modification
- Only C++ implementation changes

---

## Summary

**Before:**
- Element proxy stores raw pointer
- Unsafe after vector reallocation

**After:**
- Element proxy stores parent + index
- Always resolves current address
- Safe against reallocation

**Key insight:** Indirection through index makes proxies **stable** across vector growth.
