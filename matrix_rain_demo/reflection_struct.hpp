#pragma once

#include <string>
#include <vector> // std::vector

#include "reflection_value.hpp" // BoundValue, ValueType

// Forward declaration for parent tracking
class BoundVector;

// ---------------------------------------------------------
// BoundStruct
// ---------------------------------------------------------

// Metadata for struct fields
struct FieldInfo
{
    std::string name;
    size_t offset;         // byte offset from struct base
    ValueType type;        // Int, Float, Bool, String, Struct, Vector
    const void *type_meta; // optional: points to StructInfo / VectorInfo if needed
};

struct StructInfo
{
    std::string name;
    std::vector<FieldInfo> fields;
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

    size_t compute_struct_size(const StructInfo *sinfo) const
    {
        if (sinfo->fields.empty())
            return 0;

        const FieldInfo &last = sinfo->fields.back();

        size_t field_size = 0;

        switch (last.type)
        {
        case ValueType::Int:
            field_size = sizeof(int);
            break;

        case ValueType::Float:
            field_size = sizeof(float);
            break;

        case ValueType::Bool:
            field_size = sizeof(ByteBool);
            break;

        case ValueType::String:
            field_size = sizeof(std::string);
            break;

        case ValueType::Struct:
            field_size = compute_struct_size(
                static_cast<const StructInfo *>(last.type_meta));
            break;

        case ValueType::Vector:
            // The struct stores a std::vector<T> object
            field_size = sizeof(std::vector<std::byte>);
            break;

        default:
            field_size = 0;
            break;
        }

        return last.offset + field_size;
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