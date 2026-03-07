# Translating Python Semantic Rules into C++ Programming Rules

**Date:** March 7, 2026  
**Purpose:** Practical guide for implementing Python semantic rules as C++ error handling code  
**Cross-Reference:** 
- `SEMANTIC_RULES_CPYTHON_BINDING.md` - 30 semantic rules reference
- `ERROR_HANDLING_ARCHITECTURE.md` - Error handling architecture

---

## Executive Summary

This document provides a **practical translation guide** from Python semantic expectations to C++ implementation patterns. Each semantic rule is translated into:

1. **Validation Check** - What to verify before executing C++ code
2. **Error Condition** - What constitutes a violation
3. **Exception to Throw** - Which C++ exception represents the error
4. **Python Error Type** - Which Python exception to set
5. **ErrorHandler Action** - How to log the violation
6. **Code Template** - Ready-to-use implementation pattern

**Quick Reference:** Use this as a checklist when implementing any Python C API function.

---

## Translation Framework

```
Python Semantic Rule
        ↓
   Validation Check (C++)
        ↓
   Error Detected?
        ↓
   Throw C++ Exception
        ↓
   Catch at Boundary
        ↓
   Set Python Error + Log
        ↓
   Return nullptr to Python
```

---

## Core Programming Rules (15 Rules)

### Rule 1: Reference Semantics
**Semantic:** Python uses references, not copies

#### C++ Programming Rule
```cpp
// ✅ DO: Return reference to same object if already exists
struct ProxyCache {
    std::unordered_map<void*, PyObject*> cache;
    
    PyObject* get_or_create(void* cpp_ptr, CreateFunc create) {
        auto it = cache.find(cpp_ptr);
        if (it != cache.end() && it->second != nullptr) {
            Py_INCREF(it->second);
            return it->second;  // Return existing proxy
        }
        PyObject* proxy = create(cpp_ptr);
        cache[cpp_ptr] = proxy;
        Py_INCREF(proxy);
        return proxy;
    }
};

// ❌ DON'T: Always create new proxy
PyObject* get_player() {
    return create_new_proxy(cpp_player);  // Wrong!
}
```

**Error Detection:** Not directly detectable, but affects equality tests
**Python Error:** N/A (design issue, not runtime error)
**Recommendation:** Use singleton pattern or proxy caching

---

### Rule 3: None vs nullptr
**Semantic:** Python uses None for absence of value, nullptr crashes interpreter

#### C++ Programming Rule
```cpp
// ✅ DO: Return Py_None for null pointers
PyObject* find_entity(int id) {
    Entity* entity = cpp_find_entity(id);
    if (!entity) {
        Py_RETURN_NONE;  // Python semantic: None for "not found"
    }
    return create_entity_proxy(entity);
}

// ❌ DON'T: Return nullptr without setting PyErr
PyObject* find_entity(int id) {
    Entity* entity = cpp_find_entity(id);
    return entity ? create_proxy(entity) : nullptr;  // CRASH!
}
```

**Validation Check:** `if (!ptr) Py_RETURN_NONE;`
**Error Condition:** Returning nullptr without PyErr_SetString
**Error Category:** `ErrorCategory::NULL_REFERENCE`
**Error Handler:**
```cpp
if (!entity && should_exist) {
    ErrorHandler::instance().log_warning(
        "Unexpected null entity",
        ErrorContext("find_entity", "id=" + std::to_string(id))
    );
}
```

---

### Rule 4: Negative Indexing
**Semantic:** Python indices can be negative (count from end)

#### C++ Programming Rule
```cpp
// ✅ DO: Convert negative indices before C++ access
PyObject* vector_getitem(PyObject* self, Py_ssize_t index) {
    VectorProxyObject* proxy = (VectorProxyObject*)self;
    std::size_t size = proxy->vector->size();
    
    try {
        // Python semantic: Normalize negative index
        if (index < 0) {
            index = static_cast<Py_ssize_t>(size) + index;
        }
        
        // Bounds check after normalization
        if (index < 0 || index >= static_cast<Py_ssize_t>(size)) {
            throw std::out_of_range(
                "Index " + std::to_string(index) + " out of range [0, " + 
                std::to_string(size) + ")"
            );
        }
        
        return get_element(proxy->vector, static_cast<std::size_t>(index));
    }
    catch (const std::out_of_range& e) {
        ErrorHandler::instance().log_error(
            ErrorSeverity::WARNING,
            ErrorCategory::INDEX_ERROR,
            e.what(),
            ErrorContext("vector_getitem", "index=" + std::to_string(index))
        );
        PyErr_Format(PyExc_IndexError, "%s", e.what());
        return nullptr;
    }
}

// ❌ DON'T: Pass negative index directly to C++
PyObject* vector_getitem(PyObject* self, Py_ssize_t index) {
    return get_element(proxy->vector, index);  // UB if index < 0!
}
```

**Validation Check:** 
```cpp
if (index < 0) index = size + index;
if (index < 0 || index >= size) throw std::out_of_range(...);
```
**C++ Exception:** `std::out_of_range`
**Python Error:** `PyExc_IndexError`
**Error Category:** `ErrorCategory::INDEX_ERROR`

---

### Rule 6: Iterator Modification Detection
**Semantic:** Python detects container modifications during iteration

#### C++ Programming Rule
```cpp
// ✅ DO: Track modification count and check during iteration
struct BoundVector {
    std::size_t modification_count = 0;
    
    void append(void* value) {
        // ... append logic ...
        ++modification_count;  // Increment on modification
    }
};

struct VectorIterator {
    BoundVector* vector;
    std::size_t mod_count_at_creation;
    std::size_t current_index;
    
    PyObject* next() {
        try {
            // Python semantic: Check for modifications
            if (vector->modification_count != mod_count_at_creation) {
                throw std::runtime_error("Vector modified during iteration");
            }
            
            if (current_index >= vector->size()) {
                PyErr_SetNone(PyExc_StopIteration);
                return nullptr;
            }
            
            return get_element(vector, current_index++);
        }
        catch (const std::runtime_error& e) {
            ErrorHandler::instance().log_error(
                ErrorSeverity::WARNING,
                ErrorCategory::ITERATOR_INVALIDATION,
                e.what(),
                ErrorContext("iterator_next", "index=" + std::to_string(current_index))
            );
            PyErr_Format(PyExc_RuntimeError, "%s", e.what());
            return nullptr;
        }
    }
};

// ❌ DON'T: Allow silent modification during iteration
struct VectorIterator {
    PyObject* next() {
        // No modification check - data corruption possible!
        return get_element(vector, current_index++);
    }
};
```

**Validation Check:**
```cpp
if (iter->mod_count != container->mod_count)
    throw std::runtime_error("Container modified during iteration");
```
**C++ Exception:** `std::runtime_error`
**Python Error:** `PyExc_RuntimeError`
**Error Category:** `ErrorCategory::ITERATOR_INVALIDATION`

---

### Rule 9: Arbitrary Precision Integers
**Semantic:** Python integers have arbitrary precision, C++ integers have fixed bounds

#### C++ Programming Rule
```cpp
// ✅ DO: Check overflow before converting Python int to C++ int
int convert_to_cpp_int(PyObject* value) {
    try {
        // Type check
        if (!PyLong_Check(value)) {
            throw std::invalid_argument(
                "Expected int, got " + std::string(Py_TYPE(value)->tp_name)
            );
        }
        
        // Get Python long (may overflow)
        long long_val = PyLong_AsLong(value);
        if (long_val == -1 && PyErr_Occurred()) {
            throw std::overflow_error("Python int too large for C long");
        }
        
        // Python semantic: Check C++ int bounds
        if (long_val > INT_MAX) {
            throw std::overflow_error(
                "Value " + std::to_string(long_val) + " exceeds INT_MAX (" + 
                std::to_string(INT_MAX) + ")"
            );
        }
        if (long_val < INT_MIN) {
            throw std::overflow_error(
                "Value " + std::to_string(long_val) + " below INT_MIN (" + 
                std::to_string(INT_MIN) + ")"
            );
        }
        
        return static_cast<int>(long_val);
    }
    catch (const std::overflow_error& e) {
        ErrorHandler::instance().log_error(
            ErrorSeverity::ERROR,
            ErrorCategory::TYPE_CONVERSION,
            e.what(),
            ErrorContext("int_conversion", "overflow_check")
        );
        PyErr_Format(PyExc_OverflowError, "%s", e.what());
        return 0;  // Error already set
    }
    catch (const std::invalid_argument& e) {
        ErrorHandler::instance().log_error(
            ErrorSeverity::WARNING,
            ErrorCategory::TYPE_ERROR,
            e.what(),
            ErrorContext("int_conversion", "type_check")
        );
        PyErr_Format(PyExc_TypeError, "%s", e.what());
        return 0;
    }
}

// ❌ DON'T: Silently truncate or overflow
int convert_to_cpp_int(PyObject* value) {
    return PyLong_AsLong(value);  // Silent overflow!
}
```

**Validation Check:**
```cpp
long val = PyLong_AsLong(value);
if (val > INT_MAX || val < INT_MIN)
    throw std::overflow_error(...);
```
**C++ Exception:** `std::overflow_error`
**Python Error:** `PyExc_OverflowError`
**Error Category:** `ErrorCategory::TYPE_CONVERSION`

---

### Rule 10: String Encoding (UTF-8)
**Semantic:** Python strings are Unicode, C++ strings may use different encodings

#### C++ Programming Rule
```cpp
// ✅ DO: Validate UTF-8 encoding with error handling
std::string convert_python_string(PyObject* value) {
    try {
        // Type check
        if (!PyUnicode_Check(value)) {
            throw std::invalid_argument(
                "Expected string, got " + std::string(Py_TYPE(value)->tp_name)
            );
        }
        
        // Convert to UTF-8 with Python semantic
        PyObject* utf8 = PyUnicode_AsUTF8String(value);
        if (!utf8) {
            throw std::runtime_error("Failed to encode string as UTF-8");
        }
        
        const char* str = PyBytes_AsString(utf8);
        if (!str) {
            Py_DECREF(utf8);
            throw std::runtime_error("Failed to extract UTF-8 bytes");
        }
        
        std::string result(str);
        Py_DECREF(utf8);
        return result;
    }
    catch (const std::runtime_error& e) {
        ErrorHandler::instance().log_error(
            ErrorSeverity::ERROR,
            ErrorCategory::ENCODING_ERROR,
            e.what(),
            ErrorContext("string_conversion", "utf8_encoding")
        );
        PyErr_Format(PyExc_UnicodeDecodeError, "%s", e.what());
        return "";
    }
    catch (const std::invalid_argument& e) {
        PyErr_Format(PyExc_TypeError, "%s", e.what());
        return "";
    }
}

// ❌ DON'T: Assume ASCII or ignore encoding errors
std::string convert_python_string(PyObject* value) {
    const char* str = PyUnicode_AsUTF8(value);  // May return NULL!
    return std::string(str);  // Crash if str == nullptr!
}
```

**Validation Check:**
```cpp
PyObject* utf8 = PyUnicode_AsUTF8String(value);
if (!utf8) throw std::runtime_error("UTF-8 encoding failed");
```
**C++ Exception:** `std::runtime_error`
**Python Error:** `PyExc_UnicodeDecodeError`
**Error Category:** `ErrorCategory::ENCODING_ERROR`

---

### Rule 11: Implicit Type Coercion
**Semantic:** Python allows implicit coercions (e.g., bool to int), but validates types strictly

#### C++ Programming Rule
```cpp
// ✅ DO: Validate type before coercion
void set_player_health(PyObject* value) {
    try {
        // Python semantic: Strict type check
        if (!PyLong_Check(value)) {
            throw std::invalid_argument(
                "Expected int for health, got " + 
                std::string(Py_TYPE(value)->tp_name)
            );
        }
        
        int health = convert_to_cpp_int(value);  // With overflow check
        
        // Business logic validation
        if (health < 0) {
            throw std::invalid_argument("Health cannot be negative");
        }
        if (health > 9999) {
            throw std::invalid_argument("Health exceeds maximum (9999)");
        }
        
        cpp_player.health = health;
    }
    catch (const std::invalid_argument& e) {
        ErrorHandler::instance().log_error(
            ErrorSeverity::WARNING,
            ErrorCategory::TYPE_ERROR,
            e.what(),
            ErrorContext("set_health", "type_validation")
        );
        PyErr_Format(PyExc_TypeError, "%s", e.what());
    }
}

// ❌ DON'T: Blindly coerce without type check
void set_player_health(PyObject* value) {
    cpp_player.health = PyLong_AsLong(value);  // Crash if not int!
}
```

**Validation Check:**
```cpp
if (!PyLong_Check(value))
    throw std::invalid_argument("Expected int");
```
**C++ Exception:** `std::invalid_argument`
**Python Error:** `PyExc_TypeError`
**Error Category:** `ErrorCategory::TYPE_ERROR`

---

### Rule 13: Container Lifetime and Views
**Semantic:** Python proxies to container elements must track parent lifetime

#### C++ Programming Rule
```cpp
// ✅ DO: Validate parent container before accessing child element
struct BoundStruct {
    BoundVector* parent_vector = nullptr;
    std::size_t element_index = 0;
    
    void* instance() const {
        try {
            // Python semantic: Check parent still valid
            if (parent_vector) {
                std::size_t parent_size = parent_vector->size();
                
                if (element_index >= parent_size) {
                    throw std::out_of_range(
                        "Element at index " + std::to_string(element_index) + 
                        " no longer exists (parent size: " + 
                        std::to_string(parent_size) + ")"
                    );
                }
                
                return parent_vector->element_ptr(element_index);
            }
            return struct_ptr;
        }
        catch (const std::out_of_range& e) {
            ErrorHandler::instance().log_error(
                ErrorSeverity::WARNING,
                ErrorCategory::LIFETIME_VIOLATION,
                e.what(),
                ErrorContext("struct_instance", "parent_validation")
            );
            PyErr_Format(PyExc_ValueError, "Dangling reference: %s", e.what());
            return nullptr;
        }
    }
};

// ❌ DON'T: Access without parent lifetime check
struct BoundStruct {
    void* instance() const {
        return parent_vector->element_ptr(element_index);  // May be invalid!
    }
};
```

**Validation Check:**
```cpp
if (parent && element_index >= parent->size())
    throw std::out_of_range("Parent modified, element no longer exists");
```
**C++ Exception:** `std::out_of_range`
**Python Error:** `PyExc_ValueError`
**Error Category:** `ErrorCategory::LIFETIME_VIOLATION`

---

## Quick Reference: Validation Checklist

Use this checklist for every Python C API function:

### Pre-Condition Checks
```cpp
// Rule 3: Null check
if (!cpp_ptr) {
    Py_RETURN_NONE;  // or log warning if unexpected
}

// Rule 11: Type check
if (!PyLong_Check(value)) {
    PyErr_SetString(PyExc_TypeError, "Expected int");
    return nullptr;
}

// Rule 9: Overflow check
long val = PyLong_AsLong(value);
if (val > INT_MAX || val < INT_MIN) {
    PyErr_SetString(PyExc_OverflowError, "Value exceeds int range");
    return nullptr;
}

// Rule 4: Index normalization
if (index < 0) index = size + index;
if (index < 0 || index >= size) {
    PyErr_SetString(PyExc_IndexError, "Index out of range");
    return nullptr;
}

// Rule 10: UTF-8 validation
PyObject* utf8 = PyUnicode_AsUTF8String(value);
if (!utf8) {
    PyErr_SetString(PyExc_UnicodeDecodeError, "UTF-8 encoding failed");
    return nullptr;
}

// Rule 13: Lifetime check
if (parent && index >= parent->size()) {
    PyErr_SetString(PyExc_ValueError, "Dangling reference");
    return nullptr;
}

// Rule 6: Modification check (for iterators)
if (iter->mod_count != container->mod_count) {
    PyErr_SetString(PyExc_RuntimeError, "Container modified during iteration");
    return nullptr;
}
```

### Exception Handling Pattern
```cpp
try {
    // Perform C++ operation (may throw)
    cpp_operation();
}
catch (const std::bad_alloc&) {
    ErrorHandler::instance().log_error(
        ErrorSeverity::CRITICAL,
        ErrorCategory::MEMORY_ERROR,
        "Out of memory",
        ErrorContext(function_name, context)
    );
    PyErr_SetString(PyExc_MemoryError, "Out of memory");
    return nullptr;
}
catch (const std::out_of_range& e) {
    ErrorHandler::instance().log_error(
        ErrorSeverity::WARNING,
        ErrorCategory::INDEX_ERROR,
        e.what(),
        ErrorContext(function_name, context)
    );
    PyErr_Format(PyExc_IndexError, "%s", e.what());
    return nullptr;
}
catch (const std::overflow_error& e) {
    ErrorHandler::instance().log_error(
        ErrorSeverity::ERROR,
        ErrorCategory::TYPE_CONVERSION,
        e.what(),
        ErrorContext(function_name, context)
    );
    PyErr_Format(PyExc_OverflowError, "%s", e.what());
    return nullptr;
}
catch (const std::invalid_argument& e) {
    ErrorHandler::instance().log_error(
        ErrorSeverity::WARNING,
        ErrorCategory::TYPE_ERROR,
        e.what(),
        ErrorContext(function_name, context)
    );
    PyErr_Format(PyExc_ValueError, "%s", e.what());
    return nullptr;
}
catch (const std::exception& e) {
    ErrorHandler::instance().log_error(
        ErrorSeverity::ERROR,
        ErrorCategory::RUNTIME_ERROR,
        e.what(),
        ErrorContext(function_name, context)
    );
    PyErr_Format(PyExc_RuntimeError, "%s", e.what());
    return nullptr;
}
catch (...) {
    ErrorHandler::instance().log_error(
        ErrorSeverity::CRITICAL,
        ErrorCategory::UNKNOWN,
        "Unknown C++ exception",
        ErrorContext(function_name, context)
    );
    PyErr_SetString(PyExc_RuntimeError, "Unknown C++ exception");
    return nullptr;
}
```

---

## Complete Example Template

```cpp
/**
 * Template for Python C API function with full semantic validation
 * 
 * Semantic Rules Applied:
 * - Rule 3: None vs nullptr
 * - Rule 4: Negative indexing
 * - Rule 6: Iterator modification
 * - Rule 9: Integer overflow
 * - Rule 10: UTF-8 encoding
 * - Rule 11: Type coercion
 * - Rule 13: Container lifetime
 * - Issue 50: Exception safety
 */
PyObject* example_function(PyObject* self, PyObject* args) {
    // Parse arguments
    PyObject* py_value;
    Py_ssize_t index;
    if (!PyArg_ParseTuple(args, "On", &py_value, &index)) {
        return nullptr;
    }
    
    // Get proxy object
    auto* proxy = reinterpret_cast<ProxyObject*>(self);
    
    // Rule 3: Null pointer check
    if (!proxy || !proxy->bound) {
        ErrorHandler::instance().log_error(
            ErrorSeverity::CRITICAL,
            ErrorCategory::NULL_REFERENCE,
            "Null proxy",
            ErrorContext("example_function", "proxy_validation")
        );
        PyErr_SetString(PyExc_RuntimeError, "Internal error: null proxy");
        return nullptr;
    }
    
    try {
        // Rule 4: Negative index normalization
        std::size_t size = proxy->bound->size();
        if (index < 0) {
            index = static_cast<Py_ssize_t>(size) + index;
        }
        if (index < 0 || index >= static_cast<Py_ssize_t>(size)) {
            throw std::out_of_range("Index out of range");
        }
        
        // Rule 11: Type validation
        if (!PyLong_Check(py_value)) {
            throw std::invalid_argument("Expected int");
        }
        
        // Rule 9: Overflow check
        long long_val = PyLong_AsLong(py_value);
        if (long_val == -1 && PyErr_Occurred()) {
            throw std::overflow_error("Python int too large");
        }
        if (long_val > INT_MAX || long_val < INT_MIN) {
            throw std::overflow_error("Value exceeds C++ int range");
        }
        
        int cpp_value = static_cast<int>(long_val);
        
        // Rule 6: Modification tracking
        std::size_t mod_count_before = proxy->bound->modification_count;
        
        // Rule 13: Lifetime validation
        if (proxy->bound->parent_vector) {
            if (proxy->bound->element_index >= 
                proxy->bound->parent_vector->size()) {
                throw std::out_of_range("Parent modified, element no longer exists");
            }
        }
        
        // Perform C++ operation (Issue 50: may throw)
        proxy->bound->set_value_at(static_cast<std::size_t>(index), cpp_value);
        
        // Log successful operation
        ErrorHandler::instance().log_info(
            "Value set successfully",
            ErrorContext("example_function", 
                        "index=" + std::to_string(index) + 
                        ",value=" + std::to_string(cpp_value))
        );
        
        Py_RETURN_NONE;
    }
    catch (const std::bad_alloc&) {
        ErrorHandler::instance().log_error(
            ErrorSeverity::CRITICAL, ErrorCategory::MEMORY_ERROR,
            "Out of memory", ErrorContext("example_function", "allocation")
        );
        PyErr_SetString(PyExc_MemoryError, "Out of memory");
        return nullptr;
    }
    catch (const std::out_of_range& e) {
        ErrorHandler::instance().log_error(
            ErrorSeverity::WARNING, ErrorCategory::INDEX_ERROR,
            e.what(), ErrorContext("example_function", "bounds_check")
        );
        PyErr_Format(PyExc_IndexError, "%s", e.what());
        return nullptr;
    }
    catch (const std::overflow_error& e) {
        ErrorHandler::instance().log_error(
            ErrorSeverity::ERROR, ErrorCategory::TYPE_CONVERSION,
            e.what(), ErrorContext("example_function", "overflow_check")
        );
        PyErr_Format(PyExc_OverflowError, "%s", e.what());
        return nullptr;
    }
    catch (const std::invalid_argument& e) {
        ErrorHandler::instance().log_error(
            ErrorSeverity::WARNING, ErrorCategory::TYPE_ERROR,
            e.what(), ErrorContext("example_function", "type_check")
        );
        PyErr_Format(PyExc_TypeError, "%s", e.what());
        return nullptr;
    }
    catch (const std::exception& e) {
        ErrorHandler::instance().log_error(
            ErrorSeverity::ERROR, ErrorCategory::RUNTIME_ERROR,
            e.what(), ErrorContext("example_function", "unknown")
        );
        PyErr_Format(PyExc_RuntimeError, "%s", e.what());
        return nullptr;
    }
    catch (...) {
        ErrorHandler::instance().log_error(
            ErrorSeverity::CRITICAL, ErrorCategory::UNKNOWN,
            "Unknown C++ exception", ErrorContext("example_function", "catch_all")
        );
        PyErr_SetString(PyExc_RuntimeError, "Unknown C++ exception");
        return nullptr;
    }
}
```

---

## Summary: Semantic Rule → C++ Code Pattern

| Semantic Rule | Validation Pattern | Exception | Python Error |
|---------------|-------------------|-----------|--------------|
| Rule 3 (None) | `if (!ptr) Py_RETURN_NONE;` | N/A | N/A (return None) |
| Rule 4 (Index) | `if (i<0) i=size+i; if (i<0\|\|i>=size) throw;` | `std::out_of_range` | `IndexError` |
| Rule 6 (Iterator) | `if (mod_count!=saved) throw;` | `std::runtime_error` | `RuntimeError` |
| Rule 9 (Overflow) | `if (val>MAX\|\|val<MIN) throw;` | `std::overflow_error` | `OverflowError` |
| Rule 10 (UTF-8) | `utf8=AsUTF8(); if (!utf8) throw;` | `std::runtime_error` | `UnicodeDecodeError` |
| Rule 11 (Type) | `if (!Check(val)) throw;` | `std::invalid_argument` | `TypeError` |
| Rule 13 (Lifetime) | `if (idx>=parent.size()) throw;` | `std::out_of_range` | `ValueError` |

**All exceptions caught at boundary, logged via ErrorHandler, converted to Python errors**

---

## References

- `SEMANTIC_RULES_CPYTHON_BINDING.md` - Complete 30-rule reference
- `ERROR_HANDLING_ARCHITECTURE.md` - Error architecture
- `doc/fixes/ADDITIONAL_ISSUES_MARCH_2026.md` - Issue 50 fix
- Python C API: https://docs.python.org/3/c-api/

**Next Steps:** Apply these patterns to all Python C API functions systematically
