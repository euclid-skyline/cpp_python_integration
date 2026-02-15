#pragma once

#include <string>
#include <vector> // std::vector

#include "reflection_value.hpp" // BoundValue, ValueType

// ---------------------------------------------------------
// BoundStruct
// ---------------------------------------------------------

// Metadata for struct fields
struct FieldInfo
{
    std::string name;
    size_t offset;   // byte offset from struct base
    ValueType type;  // Int, Float, Bool, String, Struct, Vector
    void *type_meta; // optional: points to StructInfo / VectorInfo if needed
};

struct StructInfo
{
    std::string name;
    std::vector<FieldInfo> fields;
};

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
        return reinterpret_cast<char *>(m_instance) + f->offset;
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
    void *instance() const { return m_instance; }

private:
    void *m_instance;
    const StructInfo *m_info;
};
