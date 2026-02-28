// ---------------------------------------------------------
// Force Python to build without debug refcount/tracing,
// regardless of MSVC _DEBUG or CRT mode.
// ---------------------------------------------------------
// Disable MSVC Debug STL and Debug Python

#include <Python.h>
#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#if defined(_WIN32)
#include <curses.h> // PDCurses
#else
#include <ncurses.h> // Linux
#endif

#include <iomanip>
#include <vector>

#include "python_locator.hpp"
#include "cpp_module.hpp"

#include "matrix_rain_animation_data.hpp" // Struct & Vector metadata for Matrix rain

void dump_sys_path();

// Global flag for clean shutdown
static bool running = true;

void signal_handler(int)
{
    running = false;
}

int main()
{
    // ---------------------------------------------------------
    // Register signals
    // ---------------------------------------------------------
    std::signal(SIGINT, signal_handler);
#ifdef SIGTSTP
    std::signal(SIGTSTP, signal_handler);
#endif
#ifdef SIGQUIT
    std::signal(SIGQUIT, signal_handler);
#endif

    // ---------------------------------------------------------
    // Register our custom module BEFORE Python initializes
    // ---------------------------------------------------------
    PyImport_AppendInittab("cpp", &PyInit_cpp);

    // ---------------------------------------------------------
    // Locate Python (system, bundled, or ZIP)
    // ---------------------------------------------------------
    auto py = pyembed::locate_python();

    // If no valid Python was found → exit
    if (py.home.empty())
    {
        std::cerr << "ERROR: Cannot locate a proper Python setup (system, bundled, or ZIP).\n";
        return 1;
    }
#ifdef APP_DEBUG
    // Print full status report
    pyembed::print_python_status(py);
#endif
    // ---------------------------------------------------------
    // Modern Python initialization (Python 3.11+)
    // ---------------------------------------------------------

    PyStatus status;
    PyConfig config;
    PyConfig_InitPythonConfig(&config);

    // Correct bundled/system detection
    bool using_bundled = (py.source == pyembed::PythonSource::BUNDLED);
    bool using_zip = (py.source == pyembed::PythonSource::ZIP);

    // ---------------------------------------------------------
    // Bundled/Zip Python → full manual configuration
    // ---------------------------------------------------------
    if (using_bundled || using_zip)
    {
        config.module_search_paths_set = 1;

        // Set Python home
        config.home = Py_DecodeLocale(py.home.string().c_str(), nullptr);

        // Add home directory
        PyWideStringList_Append(&config.module_search_paths,
                                Py_DecodeLocale(py.home.string().c_str(), nullptr));

        // Add Lib directory
        PyWideStringList_Append(&config.module_search_paths,
                                Py_DecodeLocale(py.lib.string().c_str(), nullptr));

        std::cout << "\nUsing "
                  << (using_bundled ? "bundled" : "ZIP-based")
                  << " Python\n";
    }
    // ---------------------------------------------------------
    // System Python → let Python auto-configure
    // ---------------------------------------------------------
    else
    {
        std::cout << "\nUsing system Python\n";
        config.module_search_paths_set = 0; // let Python build sys.path

        // Tell Python exactly where the system installation lives
        config.home = Py_DecodeLocale(py.home.string().c_str(), nullptr);
        config.prefix = Py_DecodeLocale(py.home.string().c_str(), nullptr);
        config.exec_prefix = Py_DecodeLocale(py.home.string().c_str(), nullptr);

        config.program_name = Py_DecodeLocale("python", nullptr);
        config.executable = Py_DecodeLocale(py.home.string().c_str(), nullptr);
    }

    // ---------------------------------------------------------
    // Add scripts folder ONLY when using bundled/Zip Python
    // ---------------------------------------------------------
    if (using_bundled || using_zip)
    {
        auto exeDir = pyembed::get_executable_dir();
        auto scriptsPath = exeDir / "scripts";

        PyWideStringList_Append(&config.module_search_paths,
                                Py_DecodeLocale(scriptsPath.string().c_str(), nullptr));
    }

    // ---------------------------------------------------------
    // Initialize Python
    // ---------------------------------------------------------
    status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status))
    {
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }
    PyConfig_Clear(&config);

#ifdef APP_DEBUG
    // Optional: show cleaned sys.path
    dump_sys_path();
#endif

    // ---------------------------------------------------------
    // If using system Python, manually add scripts folder after initialization not before
    // ---------------------------------------------------------
    if (!using_bundled && !using_zip)
    {
        PyObject *sys = PyImport_ImportModule("sys");
        if (!sys)
        {
            PyErr_Print();
            return 1;
        }

        PyObject *path = PyObject_GetAttrString(sys, "path");
        if (!path)
        {
            PyErr_Print();
            Py_DECREF(sys);
            return 1;
        }

        auto exeDir = pyembed::get_executable_dir();
        auto scriptsPath = exeDir / "scripts";

        PyObject *scriptsPathObj = PyUnicode_FromString(scriptsPath.string().c_str());
        if (!scriptsPathObj || PyList_Append(path, scriptsPathObj) != 0)
        {
            Py_XDECREF(scriptsPathObj);
            Py_DECREF(path);
            Py_DECREF(sys);
            PyErr_Print();
            return 1;
        }
        Py_DECREF(scriptsPathObj);

        Py_DECREF(path);
        Py_DECREF(sys);
    }

    // ---------------------------------------------------------
    // Import Python controller.py script
    // ---------------------------------------------------------

    PyObject *moduleName = PyUnicode_FromString("controller");
    PyObject *module = PyImport_Import(moduleName);
    Py_DECREF(moduleName);

    if (!module)
    {
        PyErr_Print();
        std::cerr << "Failed to import Python module controller.py\n";
        // endwin();
        Py_Finalize();
        return 1;
    }

    initscr();             // start curses mode
    start_color();         // enable color if possible
    use_default_colors();  // allow default terminal colors
    noecho();              // don't echo keypresses
    curs_set(0);           // hide cursor
    nodelay(stdscr, TRUE); // Make input non-blocking
    cbreak();              // Disable line buffering

    // Initialize color pairs (foreground colors with default background)
    // Skip COLOR_BLACK (0)
    init_pair(1, COLOR_GREEN, -1);
    init_pair(2, COLOR_YELLOW, -1);
    init_pair(3, COLOR_BLUE, -1);
    init_pair(4, COLOR_MAGENTA, -1);
    init_pair(5, COLOR_CYAN, -1);
    init_pair(6, COLOR_WHITE, -1);

    int total_colors = 6; // We only initialized 6 colors (1-6), so cycle through them

    // ---------------------------------------------------------
    // Bind C++ variables sections
    // ---------------------------------------------------------
    int max_rows, max_cols;
    getmaxyx(stdscr, max_rows, max_cols);

    PyInterface::bind("columns", matrix_columns);
    PyInterface::bind("max_rows", max_rows);
    PyInterface::bind("max_cols", max_cols);

    // ----------------------------------------------------------

    // Get function
    PyObject *updateFunc = PyObject_GetAttrString(module, "update_values");
    if (!updateFunc || !PyCallable_Check(updateFunc))
    {
        std::cerr << "Function update_values() not found or not callable\n";
        Py_XDECREF(updateFunc);
        Py_DECREF(module);
        // endwin();
        Py_Finalize();
        return 1;
    }

    // ---------------------------------------------------------
    // Main loop
    // ---------------------------------------------------------
    int frame_count = 0;
    while (running)
    {
        PyObject *result = PyObject_CallObject(updateFunc, nullptr);

        if (result)
        {
            Py_DECREF(result);
            // Always update curses internal size
            resize_term(0, 0); // Let ncurses resize internal buffers
            getmaxyx(stdscr, max_rows, max_cols);
            // Clear screen
            erase();

            // Debug output on first frame and every 50 frames
            if (frame_count == 0 || frame_count % 50 == 0)
            {
                std::cerr << "Frame " << frame_count << ": columns=" << matrix_columns.size() << ", "
                          << "max_rows=" << max_rows << " max_cols=" << max_cols
                          << "\n";
            }

            // Draw each column independently
            for (size_t x = 0; x < matrix_columns.size() && x < static_cast<size_t>(max_cols); ++x)
            {
                const auto &column = matrix_columns[x];
                int pos = static_cast<int>(column.pos);
                int trail = column.trail;
                const std::string &chars = column.chars;

                int color_index = (x % total_colors) + 1;
                attron(COLOR_PAIR(color_index));

                // Draw the trail from top to bottom
                for (int i = 0; i < trail && i < static_cast<int>(chars.size()); ++i)
                {
                    int y = pos - (trail - 1 - i); // trail flows downward from pos
                    if (y >= 0 && y < max_rows)
                    {
                        // Brightest at head (bottom of trail), dimmer upward
                        if (i == trail - 1)
                            attron(A_BOLD); // Head of trail is brightest

                        mvaddch(y, x, chars[i]);

                        if (i == trail - 1)
                            attroff(A_BOLD);
                    }
                }

                attroff(COLOR_PAIR(color_index));
            }

            refresh();
            frame_count++;
        }
        else
        {
            if (PyErr_ExceptionMatches(PyExc_KeyboardInterrupt))
            {
                // User pressed Ctrl+C inside Python code
                PyErr_Clear();
                running = false;
                continue;
            }

            // Other Python exceptions
            std::cerr << "Python error at frame " << frame_count << ":\n";
            PyErr_Print();
            break;
        }

        // std::this_thread::sleep_for(std::chrono::microseconds(16667)); // ~60 FPS
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 10 FPS for easier testing
    }

    // ---------------------------------------------------------
    // Cleanup
    // ---------------------------------------------------------
    endwin();
    Py_DECREF(updateFunc);
    Py_DECREF(module);
    Py_Finalize();

    std::cout << "\nProgram terminated.\n";
    return 0;
}

void dump_sys_path()
{
    PyObject *sys = PyImport_ImportModule("sys");
    if (!sys)
    {
        PyErr_Print();
        return;
    }

    PyObject *path = PyObject_GetAttrString(sys, "path");
    Py_DECREF(sys);

    if (!path)
    {
        PyErr_Print();
        return;
    }

    std::cout << "\n--- sys.path ---\n";

    Py_ssize_t size = PyList_Size(path);
    for (Py_ssize_t i = 0; i < size; ++i)
    {
        PyObject *item = PyList_GetItem(path, i);
        if (!PyUnicode_Check(item))
        {
            std::cout << "<non-unicode entry>\n";
            continue;
        }
        const char *entry = PyUnicode_AsUTF8(item);
        if (!entry)
        {
            PyErr_Clear();
            std::cout << "<unprintable entry>\n";
            continue;
        }
        std::cout << entry << "\n";
    }

    std::cout << "----------------\n\n";

    Py_DECREF(path);
}
