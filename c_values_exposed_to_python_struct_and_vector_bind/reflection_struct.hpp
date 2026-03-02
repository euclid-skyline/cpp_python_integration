#pragma once

#include <string>
#include <vector> // std::vector

#include "reflection_value.hpp" // BoundValue, ValueType

// Forward declaration for parent tracking
class BoundVector;

// ---------------------------------------------------------
// BoundStruct
// ---------------------------------------------------------

// ============================================================================
// FieldInfo - Description of one field within a struct
// ============================================================================
struct FieldInfo
{
    std::string name;      // Field name as declared in struct
    size_t offset;         // Byte offset from struct base (e.g., offsetof(Struct, field))
    ValueType type;        // Field type: Int, Float, Bool, String, Struct, or Vector
    const void *type_meta; // Optional metadata for complex types:
                           //   - If type==Struct: pointer to StructInfo for nested struct
                           //   - If type==Vector: pointer to VectorInfo for field vector
                           //   - Otherwise: nullptr
};

// ============================================================================
// StructInfo - Complete metadata for a reflected struct type
// ============================================================================
// Populated at compile-time by REGISTER_STRUCT macro.
// Used by proxy system for safe field access, memory allocation, and lifetime mgmt.
// ============================================================================
struct StructInfo
{
    std::string name; // Struct type name (e.g., "Enemy", "Team")

    std::vector<FieldInfo> fields; // All fields in declaration order.
                                   // Used for field lookups and reflection.

    std::size_t size; // Total size of struct (Issue 2 fix).
                      // Replaces unsafe runtime field-offset math.
                      // Set by REGISTER_STRUCT via sizeof(struct_type).

    void (*construct_fn)(void *); // Placement-new default constructor (Issue 1 fix).
                                  // Called to initialize new instances:
                                  //   - Handles nested std::vector/std::string fields
                                  //   - Calls themselves recursively for nested structs
                                  //   - nullptr if struct has no ctor requirements.

    void (*destruct_fn)(void *); // Explicit destructor (Issue 1 fix).
                                 // Called before deallocating:
                                 //   - Destructs all non-trivial members (string, vector)
                                 //   - nullptr if struct has no dtor requirements.
};

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

    // Constructor for vector elements (parent tracking)
    BoundStruct(const std::string &name, BoundVector *parent, std::size_t index, const StructInfo *info)
        : m_instance(nullptr), m_info(info), m_parent_vector(parent), m_element_index(index)
    {
        this->name = name;
        this->type = ValueType::Struct;
    }

    // Reflection helpers
    const FieldInfo *get_field(const std::string &field) const
    {
        for (const auto &f : m_info->fields)
            if (f.name == field)
                return &f;
        return nullptr;
    }

    void *get_field_ptr(const FieldInfo *f) const
    {
        return reinterpret_cast<char *>(instance()) + f->offset;
    }

    const StructInfo *info() const { return m_info; }

    void *instance() const;
    // Implemented after BoundVector is fully defined

private:
    void *m_instance; // Raw pointer (for standalone structs)
    const StructInfo *m_info;

    // For vector elements (Issue 26 fix)
    BoundVector *m_parent_vector; // nullptr if not from vector
    std::size_t m_element_index;  // Valid only if m_parent_vector != nullptr
};
// After BoundStruct is defined, include BoundVector to complete inline implementations
#include "reflection_vector.hpp"

// Now implement BoundStruct::instance() which needs full BoundVector definition
inline void *BoundStruct::instance() const
{
    if (m_parent_vector)
    {
        // Resolve pointer from current vector state
        return m_parent_vector->element_ptr(m_element_index);
    }
    return m_instance; // Static pointer for non-vector structs
}