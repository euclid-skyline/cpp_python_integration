#pragma once
#include <vector> // std::vector
#include <string> // std::string

#include "reflection_value.hpp"  // BoundValue, ValueType
#include "reflection_struct.hpp" // BoundStruct, StructInfo

// ---------------------------------------------------------
// BoundVector
// ---------------------------------------------------------

// Metadata for vector elements
struct VectorInfo
{
    ValueType element_type; // Int, Float, Bool, String, Struct, ...
    void *element_meta;     // e.g. StructInfo* if element_type == Struct or VectorInfo* if element_type == Vector (for nested vectors)

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
    BoundVector(const std::string &name, void *vec_ptr, const VectorInfo *info)
    {
        this->name = name;
        this->type = ValueType::Vector;
        m_vec_ptr = vec_ptr;
        m_info = info;
    }

    // Reflection helpers
    std::size_t size() const
    {
        return m_info->size_fn ? m_info->size_fn(m_vec_ptr) : 0;
    }

    void *element_ptr(std::size_t index) const
    {
        return m_info->element_ptr_fn ? m_info->element_ptr_fn(m_vec_ptr, index) : nullptr;
    }

    const VectorInfo *info() const { return m_info; }
    void *raw_vector() const { return m_vec_ptr; }

    // ------------------------------------------------------------
    // append() — required by VectorProxy
    // ------------------------------------------------------------
    bool append_from_cpp(void *value_ptr)
    {
        return m_info->append_fn ? m_info->append_fn(m_vec_ptr, value_ptr) : false;
    }

private:
    void *m_vec_ptr;          // pointer to std::vector<T>
    const VectorInfo *m_info; // element type metadata
};
