#include "python_proxy.hpp"

extern "C" PyMODINIT_FUNC PyInit_cpp(void)
{
    // Initialize proxy types
    if (PyType_Ready(&CppProxyType) < 0)
        return nullptr;
    if (PyType_Ready(&StructProxyType) < 0)
        return nullptr;
    if (PyType_Ready(&VectorProxyType) < 0)
        return nullptr;

    // Create the root proxy
    PyObject *cpp_obj = create_cpp_proxy();
    if (!cpp_obj)
        return nullptr;

    // The module *is* the proxy
    return cpp_obj;
}
