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
// ============================================================
PyBoundValue *PyInterface::wrap_field(const FieldInfo *field, void *fieldPtr)
{
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
    // Struct type
    // ------------------------------------------------------------
    case ValueType::Struct:
    {
        const StructInfo *sinfo =
            static_cast<const StructInfo *>(field->type_meta);

        // Create BoundStruct for this field
        BoundStruct *bstruct =
            new BoundStruct(field->name, fieldPtr, sinfo);

        // Wrap BoundStruct inside a PyBoundValue
        struct PyBoundStructProxy : PyBoundValue
        {
            BoundStruct *bs;

            PyBoundStructProxy(const std::string &n, BoundStruct *b)
            {
                name = n;
                type = ValueType::Struct;
                bs = b;
            }

            PyObject *to_python() override
            {
                return StructProxy_New(bs);
            }

            bool from_python(PyObject *) override
            {
                return false;   // Struct assignment not supported
            }
        };  

        return new PyBoundStructProxy(field->name, bstruct);
    }

    // ------------------------------------------------------------
    // Vector type
    // ------------------------------------------------------------
    case ValueType::Vector:
    {
        const VectorInfo *vinfo =
            static_cast<const VectorInfo *>(field->type_meta);

        BoundVector *bvec =
            new BoundVector(field->name, fieldPtr, vinfo);

        struct PyBoundVectorProxy : PyBoundValue
        {
            BoundVector *bv;

            PyBoundVectorProxy(const std::string &n, BoundVector *v)
            {
                name = n;
                type = ValueType::Vector;
                bv = v;
            }

            PyObject *to_python() override
            {
                return VectorProxy_New(bv);
            }

            bool from_python(PyObject *) override
            {
                return false;   // Vector assignment not supported
            }
        };

        return new PyBoundVectorProxy(field->name, bvec);
    }

    default:
        return nullptr;
    }
}

// ============================================================
//  PyInterface::wrap_vector_element()
//  - Creates the correct PyBoundValue subclass for a vector element
// ============================================================
PyBoundValue *PyInterface::wrap_vector_element(BoundVector *vec, void *elemPtr)
{
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
    // Struct type
    // ------------------------------------------------------------
    case ValueType::Struct:
    {
        const StructInfo *sinfo =
            static_cast<const StructInfo *>(info->element_meta);

        BoundStruct *bstruct =
            new BoundStruct(vec->name, elemPtr, sinfo);

        struct PyBoundStructProxy : PyBoundValue
        {
            BoundStruct *bs;

            PyBoundStructProxy(const std::string &n, BoundStruct *b)
            {
                name = n;
                type = ValueType::Struct;
                bs = b;
            }

            PyObject *to_python() override
            {
                return StructProxy_New(bs);
            }

            bool from_python(PyObject *) override
            {
                return false;   // Struct assignment not supported
            }
        };

        return new PyBoundStructProxy(vec->name, bstruct);
    }

    // ------------------------------------------------------------
    // Vector type (vector of vector)
    // ------------------------------------------------------------
    case ValueType::Vector:
    {
        const VectorInfo *vinfo =
            static_cast<const VectorInfo *>(info->element_meta);

        BoundVector *bvec =
            new BoundVector(vec->name, elemPtr, vinfo);

        struct PyBoundVectorProxy : PyBoundValue
        {
            BoundVector *bv;

            PyBoundVectorProxy(const std::string &n, BoundVector *v)
            {
                name = n;
                type = ValueType::Vector;
                bv = v;
            }

            PyObject *to_python() override
            {
                return VectorProxy_New(bv);
            }

            bool from_python(PyObject *) override
            {
                return false;   // Vector assignment not supported
            }
        };

        return new PyBoundVectorProxy(vec->name, bvec);
    }

    default:
        return nullptr;
    }
}
