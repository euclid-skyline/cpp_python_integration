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

// Metadata for vector elements
struct VectorInfo
{
    ValueType element_type;   // Int, Float, Bool, String, Struct, ...
    const void *element_meta; // e.g. StructInfo* if element_type == Struct or VectorInfo* if element_type == Vector (for nested vectors)

    // Function pointers for type-erased operations
    std::size_t (*size_fn)(void *vec_ptr);
    void *(*element_ptr_fn)(void *vec_ptr, std::size_t index);
    bool (*append_fn)(void *vec_ptr, void *value_ptr);
    void *(*create_empty_vec_fn)();
    void (*destroy_vec_fn)(void *vec_ptr);
};
// Note: For simplicity, we assume all vectors are std::vector<T> and we only store a void* to it.
// The VectorInfo tells us how to interpret the elements.
class BoundVector : public BoundValue
{
public:
    // Constructor for standalone vectors (not from vector)
    BoundVector(const std::string &name, void *vec_ptr, const VectorInfo *info)
        : m_vec_ptr(vec_ptr), m_info(info), m_parent_vector(nullptr), m_element_index(0)
    {
        this->name = name;
        this->type = ValueType::Vector;
    }

    // Constructor for nested vectors (element of another vector) - parent tracking
    BoundVector(const std::string &name, BoundVector *parent, std::size_t index, const VectorInfo *info)
        : m_vec_ptr(nullptr), m_info(info), m_parent_vector(parent), m_element_index(index)
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
        if (m_parent_vector)
        {
            // Nested vector: resolve from parent
            return m_parent_vector->element_ptr(m_element_index);
        }
        return m_vec_ptr; // Top-level vector
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

    // For nested vectors (Issue 26 fix)
    BoundVector *m_parent_vector; // nullptr if top-level
    std::size_t m_element_index;  // Valid only if m_parent_vector != nullptr
};
