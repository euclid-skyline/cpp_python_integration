#pragma once
#include <vector>                // std::vector
#include <string>                // std::string

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
            const StructInfo *sinfo =
                static_cast<const StructInfo *>(m_info->element_meta);
            std::size_t struct_size = compute_struct_size(sinfo);
            const auto *bytes = reinterpret_cast<const std::vector<std::byte> *>(m_vec_ptr);
            return struct_size ? bytes->size() / struct_size : 0;
        }

        case ValueType::Vector:
            // vector-of-vector stored as std::vector<void*>
            return reinterpret_cast<const std::vector<void *> *>(m_vec_ptr)->size();

        default:
            return 0;
        }
    }

    void *element_ptr(std::size_t index) const
    {
        switch (m_info->element_type)
        {
        case ValueType::Int:
            return &(*reinterpret_cast<std::vector<int> *>(m_vec_ptr))[index];
        case ValueType::Float:
            return &(*reinterpret_cast<std::vector<float> *>(m_vec_ptr))[index];
        case ValueType::Bool:
            return &(*reinterpret_cast<std::vector<ByteBool> *>(m_vec_ptr))[index];
        case ValueType::String:
            return &(*reinterpret_cast<std::vector<std::string> *>(m_vec_ptr))[index];

        case ValueType::Struct:
        {
            const StructInfo *sinfo =
                static_cast<const StructInfo *>(m_info->element_meta);
            std::size_t struct_size = compute_struct_size(sinfo);
            auto *bytes = reinterpret_cast<std::vector<std::byte> *>(m_vec_ptr);
            return bytes->data() + index * struct_size;
        }

        case ValueType::Vector:
        {
            // element is a pointer to inner std::vector<...>
            auto *vec = reinterpret_cast<std::vector<void *> *>(m_vec_ptr);
            return (*vec)[index];
        }

        default:
            return nullptr;
        }
    }

    const VectorInfo *info() const { return m_info; }
    void *raw_vector() const { return m_vec_ptr; }

    // ------------------------------------------------------------
    // append() — required by VectorProxy
    // ------------------------------------------------------------
    bool append_from_cpp(void *value_ptr)
    {
        switch (m_info->element_type)
        {
        case ValueType::Int:
            reinterpret_cast<std::vector<int> *>(m_vec_ptr)
                ->push_back(*static_cast<int *>(value_ptr));
            return true;

        case ValueType::Float:
            reinterpret_cast<std::vector<float> *>(m_vec_ptr)
                ->push_back(*static_cast<float *>(value_ptr));
            return true;

        case ValueType::Bool:
            reinterpret_cast<std::vector<ByteBool> *>(m_vec_ptr)
                ->push_back(*static_cast<ByteBool *>(value_ptr));
            return true;

        case ValueType::String:
            reinterpret_cast<std::vector<std::string> *>(m_vec_ptr)
                ->push_back(*static_cast<std::string *>(value_ptr));
            return true;

        case ValueType::Struct:
        {
            const StructInfo *sinfo =
                static_cast<const StructInfo *>(m_info->element_meta);
            std::size_t struct_size = compute_struct_size(sinfo);

            auto *bytes = reinterpret_cast<std::vector<std::byte> *>(m_vec_ptr);
            std::byte *src = reinterpret_cast<std::byte *>(value_ptr);
            bytes->insert(bytes->end(), src, src + struct_size);
            return true;
        }

        case ValueType::Vector:
        {
            // value_ptr is pointer to inner std::vector<...>
            auto *outer = reinterpret_cast<std::vector<void *> *>(m_vec_ptr);
            outer->push_back(value_ptr);
            return true;
        }

        default:
            return false;
        }
    }

private:
    static std::size_t compute_struct_size(const StructInfo *sinfo)
    {
        if (!sinfo || sinfo->fields.empty())
            return 0;

        const FieldInfo &last = sinfo->fields.back();
        std::size_t field_size = 0;

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
            // struct stores a std::vector<...> object
            field_size = sizeof(std::vector<std::byte>);
            break;

        default:
            field_size = 0;
            break;
        }

        return last.offset + field_size;
    }

    void *m_vec_ptr;          // pointer to std::vector<T>
    const VectorInfo *m_info; // element type metadata
};
