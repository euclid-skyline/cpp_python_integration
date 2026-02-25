# C++ Values Exposed to Python (Structs + Vectors)

## Overview

This project exposes C++ scalars, structs, vectors, and nested vectors to Python using a reflection layer plus Python proxy bindings. It is part of the cpp_python_integration workspace and focuses on safe, dynamic access to complex data structures.

Key highlights:
- Pure C++ reflection layer (no Python dependencies)
- Python proxies for structs and vectors with safe memory handling
- Parent tracking for vector element safety
- Extensive architecture and design documentation

## Project Layout

- `main.cpp` - Application entry point and Python embedding
- `reflection_value.hpp` / `reflection_struct.hpp` / `reflection_vector.hpp` - Reflection layer
- `value_interface.hpp` / `value_interface.cpp` - Binding registry and type dispatch
- `python_proxy.hpp` / `python_proxy.cpp` - Python proxy types
- `python_bind.hpp` - Scalar conversion helpers
- `data_game_traits.cpp` / `data_game_traits.hpp` - Example reflected types
- `scripts/` - Python control scripts
- `doc/` - Architecture and design documentation

## Requirements

- CMake 3.15+
- C++20 compiler
- Python development headers and libraries
- Preferred Python version: 3.14.2 (see `python_required_version.txt`)

## Build

From this directory:

```bash
cmake -S . -B build
cmake --build build
```

Notes:
- On Windows, the build copies the Python DLL and `scripts/` folder next to the executable.
- On Linux/macOS, ensure your system Python development package is installed.

## Run

After build, run the executable from the build output directory. Example:

```bash
./build/EmbeddedPythonLoop
```

If you built with a multi-config generator (Visual Studio), run the executable from the configuration folder (e.g., `build/Debug/EmbeddedPythonLoop.exe`).

## Documentation

Start with the documentation index:
- `doc/architecture/DOCUMENTATION_INDEX.md`

Key references:
- `doc/architecture/ARCHITECTURE_DEEP_DIVE.md`
- `doc/architecture/FUNCTION_REFERENCE.md`
- `doc/architecture/OWNERSHIP_MODELS_GUIDE.md`
- `doc/architecture/PARENT_TRACKING_IMPLEMENTATION_GUIDE.md`

## License

See the repository-level `LICENSE` file.
