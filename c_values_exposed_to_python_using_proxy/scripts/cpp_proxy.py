import cppbridge


class CppProxy:
    def __getattr__(self, name):
        return cppbridge.cpp_get(name)

    def __setattr__(self, name, value):
        cppbridge.cpp_set(name, value)


cpp = CppProxy()
