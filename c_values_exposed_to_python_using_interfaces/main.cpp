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
#include <curses.h>
#include <iomanip>

#include "python_locator.hpp"
#include "value_interface.hpp"

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
    PyImport_AppendInittab("cppbridge", &PyInterface::init_py_module);

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
        PyObject *path = PyObject_GetAttrString(sys, "path");

        auto exeDir = pyembed::get_executable_dir();
        auto scriptsPath = exeDir / "scripts";

        PyList_Append(path, PyUnicode_FromString(scriptsPath.string().c_str()));

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
        Py_Finalize();
        return 1;
    }

    // ---------------------------------------------------------
    // Bind C++ variables
    // ---------------------------------------------------------
    int health = 100;
    float speed = 3.5f;
    bool alive = true;
    std::string name = "Euclid";

    PyInterface::bind("health", health);
    PyInterface::bind("speed", speed);
    PyInterface::bind("alive", alive);
    PyInterface::bind("player_name", name);

    // Get function
    PyObject *updateFunc = PyObject_GetAttrString(module, "update_values");
    if (!updateFunc || !PyCallable_Check(updateFunc))
    {
        std::cerr << "Function update_values() not found or not callable\n";
        Py_XDECREF(updateFunc);
        Py_DECREF(module);
        Py_Finalize();
        return 1;
    }

    initscr();   // start curses mode
    noecho();    // don't echo keypresses
    curs_set(0); // hide cursor

    // ---------------------------------------------------------
    // Main loop
    // ---------------------------------------------------------
    while (running)
    {
        PyObject *result = PyObject_CallObject(updateFunc, nullptr);

        if (result)
        {
            Py_DECREF(result);

            // Clear screen and redraw using ncurses
            clear();

            mvprintw(1, 2, "=== C++ / Python Dashboard ===");
            mvprintw(3, 4, "health : %d", health);
            mvprintw(4, 4, "speed  : %.2f", speed);
            mvprintw(5, 4, "alive  : %s", alive ? "true" : "false");
            mvprintw(6, 4, "name   : %s", name.c_str());

            refresh();    
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
            PyErr_Print();
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    // ---------------------------------------------------------
    // Cleanup
    // ---------------------------------------------------------
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
        std::cout << PyUnicode_AsUTF8(item) << "\n";
    }

    std::cout << "----------------\n\n";

    Py_DECREF(path);
}
