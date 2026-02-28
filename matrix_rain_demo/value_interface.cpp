#include "value_interface.hpp"
#include "python_proxy.hpp"

// Explicit template instantiation
// template void PyInterface::bind<int>(const std::string &, int &);
// template void PyInterface::bind<float>(const std::string &, float &);
// template void PyInterface::bind<ByteBool>(const std::string &, ByteBool &);
// template void PyInterface::bind<std::string>(const std::string &, std::string &);

// ------------------------------------------------------------
// Retrieve raw BoundValue*
// ------------------------------------------------------------
BoundValue *PyInterface::get_value_raw(const std::string &name)
{
    auto it = g_values.find(name);
    return (it != g_values.end()) ? it->second.get() : nullptr;
}

// ---------------------------------------------------------
PyBoundValue *PyInterface::get_value(const std::string &name)
{
    auto it = g_values.find(name);
    return (it != g_values.end())
               ? dynamic_cast<PyBoundValue *>(it->second.get())
               : nullptr;
}

// ============================================================
//  PyInterface::wrap_field()
//  - Creates the correct PyBoundValue subclass for a struct field
//  - For scalar types only; structs and vectors handled by caller
// ============================================================
PyBoundValue *PyInterface::wrap_field(const FieldInfo *field, void *fieldPtr)
{
    if (!field || !fieldPtr)
        return nullptr;

    switch (field->type)
    {
    // ------------------------------------------------------------
    // Scalar types
    // ------------------------------------------------------------
    case ValueType::Int:
        return new PyBoundInt(field->name, *static_cast<int *>(fieldPtr));

    case ValueType::Float:
        return new PyBoundFloat(field->name, *static_cast<float *>(fieldPtr));

    case ValueType::Bool:
        return new PyBoundBool(field->name, *static_cast<ByteBool *>(fieldPtr));

    case ValueType::String:
        return new PyBoundString(field->name, *static_cast<std::string *>(fieldPtr));

    // ------------------------------------------------------------
    // Struct and Vector types - caller handles these directly
    // ------------------------------------------------------------
    case ValueType::Struct:
    case ValueType::Vector:
        return nullptr;

    default:
        return nullptr;
    }
}

// ============================================================
//  PyInterface::wrap_vector_element()
//  - Creates the correct PyBoundValue subclass for a vector element
//  - For scalar types only; structs and vectors handled by caller
// ============================================================
PyBoundValue *PyInterface::wrap_vector_element(BoundVector *vec, void *elemPtr)
{
    if (!vec || !elemPtr)
        return nullptr;

    const VectorInfo *info = vec->info();

    switch (info->element_type)
    {
    // ------------------------------------------------------------
    // Scalar types
    // ------------------------------------------------------------
    case ValueType::Int:
        return new PyBoundInt(vec->name, *static_cast<int *>(elemPtr));

    case ValueType::Float:
        return new PyBoundFloat(vec->name, *static_cast<float *>(elemPtr));

    case ValueType::Bool:
        return new PyBoundBool(vec->name, *static_cast<ByteBool *>(elemPtr));

    case ValueType::String:
        return new PyBoundString(vec->name, *static_cast<std::string *>(elemPtr));

    // ------------------------------------------------------------
    // Struct and Vector types - caller handles these directly
    // ------------------------------------------------------------
    case ValueType::Struct:
    case ValueType::Vector:
        return nullptr;

    default:
        return nullptr;
    }
}
