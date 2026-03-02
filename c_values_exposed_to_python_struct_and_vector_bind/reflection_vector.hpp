#pragma once
#include <vector> // std::vector
#include <string> // std::string

#include "reflection_value.hpp" // BoundValue, ValueType

// Forward declaration - full definition may be in another header
class BoundStruct;
struct StructInfo;

// ---------------------------------------------------------
// BoundVector
// ---------------------------------------------------------

// ============================================================================
// VectorInfo - Complete metadata for a reflected vector type
// ============================================================================
// Populated at compile-time by REGISTER_VECTOR macro via make_vector_info<>().
// Provides type-erased access to std::vector<T> through function pointers.
// ============================================================================
struct VectorInfo
{
    ValueType element_type; // Type of elements: Int, Float, Bool, String, Struct, Vector, etc.

    const void *element_meta; // Metadata for element type (used if element_type is complex):
                              //   - If element_type==Struct: points to StructInfo
                              //   - If element_type==Vector: points to VectorInfo (nested)
                              //   - Otherwise (scalar): nullptr

    // ========== Type-erased operations via generic_vec_* templates ==========
    // All function pointers are filled in by make_vector_info<ElementType>()
    // and work with void* representing std::vector<ElementType>*

    std::size_t (*size_fn)(void *vec_ptr);
    // Returns number of elements in vector.
    // Generated: generic_vec_size<ElementType>

    void *(*element_ptr_fn)(void *vec_ptr, std::size_t index);
    // Returns pointer to element at [index].
    // Bounds-checked: returns nullptr if index >= size().
    // Generated: generic_vec_element_ptr<ElementType>

    bool (*append_fn)(void *vec_ptr, void *value_ptr);
    // Appends *value_ptr (ElementType*) to vector.
    // Return: true on success, false on error.
    // Generated: generic_vec_append<ElementType>

    void *(*create_empty_vec_fn)();
    // Allocates and returns new empty std::vector<ElementType>.
    // Caller responsible for later destroy_vec_fn() call.
    // Generated: generic_vec_create_empty<ElementType>

    void (*destroy_vec_fn)(void *vec_ptr);
    // Destroys and deallocates vector.
    // No-op if vec_ptr is nullptr.
    // Generated: generic_vec_destroy<ElementType>
};
// ===== Note: Void pointer trick for type-erasure =====
// All vectors stored as void* pointing to std::vector<T>.
// VectorInfo function pointers know the true element type T via template instantiation.
// This allows uniform Python API that works for vector<int>, vector<Enemy>, etc.
class BoundVector : public BoundValue
{
public:
    // Constructor for standalone vectors (not from vector or struct field)
    BoundVector(const std::string &name, void *vec_ptr, const VectorInfo *info)
        : m_vec_ptr(vec_ptr), m_info(info), m_parent_vector(nullptr), m_element_index(0),
          m_parent_struct(nullptr), m_field_offset(0)
    {
        this->name = name;
        this->type = ValueType::Vector;
    }

    // Constructor for nested vectors (element of another vector) - parent tracking
    BoundVector(const std::string &name, BoundVector *parent, std::size_t index, const VectorInfo *info)
        : m_vec_ptr(nullptr), m_info(info), m_parent_vector(parent), m_element_index(index),
          m_parent_struct(nullptr), m_field_offset(0)
    {
        this->name = name;
        this->type = ValueType::Vector;
    }

    // Constructor for vector fields inside structs (Issue 5 fix in Gemini Review - parent struct tracking)
    // Used when a vector is a field inside a struct (e.g., std::vector member)
    BoundVector(const std::string &name, BoundStruct *parent, std::size_t field_offset, const VectorInfo *info)
        : m_vec_ptr(nullptr), m_info(info), m_parent_vector(nullptr), m_element_index(0),
          m_parent_struct(parent), m_field_offset(field_offset)
    {
        this->name = name;
        this->type = ValueType::Vector;
    }

    // Reflection helpers
    std::size_t size() const
    {
        if (!m_info || !m_info->size_fn)
            return 0;
        return m_info->size_fn(raw_vector());
    }

    void *element_ptr(std::size_t index) const
    {
        if (!m_info || !m_info->element_ptr_fn)
            return nullptr;
        return m_info->element_ptr_fn(raw_vector(), index);
    }

    const VectorInfo *info() const { return m_info; }

    void *raw_vector() const
    {
        // Priority 1: If this vector is a field inside a struct (Issue 5 fixn in Gemini Review)
        if (m_parent_struct)
        {
            // Recalculate address from parent: parent_addr + field_offset
            return reinterpret_cast<char *>(m_parent_struct->instance()) + m_field_offset;
        }
        // Priority 2: If this vector is an element in another vector (Issue 26 fix in Copilot Review)
        if (m_parent_vector)
        {
            // Nested vector: resolve from parent
            return m_parent_vector->element_ptr(m_element_index);
        }
        // Priority 3: Top-level vector with static address
        return m_vec_ptr;
    }

    // ------------------------------------------------------------
    // append() — required by VectorProxy
    // ------------------------------------------------------------
    bool append_from_cpp(void *value_ptr)
    {
        return m_info->append_fn ? m_info->append_fn(raw_vector(), value_ptr) : false;
    }

private:
    void *m_vec_ptr;          // pointer to std::vector<T> (for top-level vectors)
    const VectorInfo *m_info; // element type metadata

    // For nested vectors (Issue 26 fix in Copilot Review - parent tracking)
    BoundVector *m_parent_vector; // nullptr if not nested in another vector
    std::size_t m_element_index;  // Valid only if m_parent_vector != nullptr

    // For vector fields inside structs (Issue 5 fix in Gemini Review - parent struct tracking)
    BoundStruct *m_parent_struct; // nullptr if not a field inside a struct
    std::size_t m_field_offset;   // Field offset from parent struct base
};
