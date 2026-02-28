// A More Generalized Interface for Exposing C++ Variables to Python
#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <type_traits>
#include <Python.h>

#include "reflection_value.hpp"  // BoundValue, ValueType
#include "reflection_struct.hpp" // StructInfo, FieldInfo, BoundStruct
#include "reflection_vector.hpp" // VectorInfo, BoundVector
#include "python_bind.hpp"       // PyBoundValue and concrete subclasses

// ---------------------------------------------------------
// Interface class
// ---------------------------------------------------------

// Type trait to detect std::vector
template <typename T>
struct is_std_vector : std::false_type
{
};

template <typename T, typename Alloc>
struct is_std_vector<std::vector<T, Alloc>> : std::true_type
{
};

// Type trait to detect reflected struct (we'll specialize this for user-defined structs)
// By default, no struct is reflected. Users can specialize this for their structs.
// Example specialization for a user-defined struct MyStruct:
// struct MyStruct { int x; float y; };
// Then you would write:
// template <>
// struct is_reflected_struct<MyStruct> : std::true_type {};
// This way, the bind() function can automatically detect if a type is a reflected struct and handle it accordingly.

template <typename T>
struct is_reflected_struct : std::false_type
{
};

template <typename T>
const StructInfo *get_struct_info()
{
    static_assert(sizeof(T) == 0, "get_struct_info not specialized for this type");
    return nullptr;
}

template <typename T>
const VectorInfo *get_vector_info()
{
    static_assert(sizeof(T) == 0, "get_vector_info not specialized for this type");
    return nullptr;
}

class PyInterface
{
public:
    // [C++20 FIX] inline static allowed in C++17+, idiomatic in C++20
    static inline std::unordered_map<std::string, std::unique_ptr<BoundValue>> g_values;

    template <typename T>
    static void bind(const std::string &name, T &variable)
    {
        if constexpr (std::is_same_v<T, int>)
        {
            g_values[name] = std::make_unique<PyBoundInt>(name, variable);
        }
        else if constexpr (std::is_same_v<T, float>)
        {
            g_values[name] = std::make_unique<PyBoundFloat>(name, variable);
        }
        else if constexpr (std::is_same_v<T, ByteBool>)
        {
            g_values[name] = std::make_unique<PyBoundBool>(name, variable);
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            g_values[name] = std::make_unique<PyBoundString>(name, variable);
        }
        else if constexpr (is_reflected_struct<T>::value)
        {
            g_values[name] = std::make_unique<BoundStruct>(name, &variable, get_struct_info<T>());
        }
        else if constexpr (is_std_vector<T>::value)
        {
            using Elem = typename T::value_type;
            g_values[name] = std::make_unique<BoundVector>(name, &variable, get_vector_info<Elem>());
        }
        else
        {
            static_assert(!sizeof(T), "Unsupported type for PyInterface::bind()");
        }
    }

    // Retrieve raw BoundValue (scalar, struct or vector) by name
    static BoundValue *get_value_raw(const std::string &name);

    // Retrieve PyBoundValue (for scalar types) by name
    static PyBoundValue *get_value(const std::string &name);

    // Wrap a struct field into the correct PyBoundValue subclass
    static PyBoundValue *wrap_field(const FieldInfo *field, void *fieldPtr);

    // Wrap a vector element into the correct PyBoundValue subclass
    static PyBoundValue *wrap_vector_element(BoundVector *vec, void *elemPtr);
};
