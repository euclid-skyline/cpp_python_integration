# C++ Values Exposed to Python (Matrix Rain Animation Example)

## Overview

This project exposes C++ scalars, structs, and vectors to Python using a reflection layer plus Python proxy bindings. It demonstrates the framework by animating a "Matrix rain" effect where Python controls animation state (a bound vector of struct) and C++ renders to terminal using curses.

Key highlights:
- Pure C++ reflection layer (no Python dependencies)
- Python proxies for structs and vectors with safe memory handling
- Parent tracking for vector element safety
- Real-time animation driven by Python, rendered by C++
- Extensive architecture and design documentation

## Project Layout

- `main.cpp` - Application entry point, Python embedding, and curses rendering loop
- `reflection_value.hpp` / `reflection_struct.hpp` / `reflection_vector.hpp` - Reflection layer
- `value_interface.hpp` / `value_interface.cpp` - Binding registry and type dispatch
- `python_proxy.hpp` / `python_proxy.cpp` - Python proxy types
- `python_bind.hpp` - Scalar conversion helpers
- `matrix_rain_animation_data.cpp` / `matrix_rain_animation_data.hpp` - MatrixColumn struct and animation state vector
- `scripts/controller.py` - Python animation controller
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



## License

See the repository-level `LICENSE` file.
