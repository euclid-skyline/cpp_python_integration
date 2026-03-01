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
#include "reflection_value.hpp"           // ByteBool, TRUE_BYTE, FALSE_BYTE

void dump_sys_path();

// Render matrix rain animation to terminal
void render_matrix_rain(const std::vector<MatrixColumn> &columns, int max_rows, int max_cols, int total_colors);

// Global flag for clean shutdown (set by signal handler on Ctrl+C)
static bool running = true;

// Global keyboard control states (bound to Python for synchronization)
static ByteBool paused = FALSE_BYTE;  // P key: pause/resume animation
static float speed_multiplier = 1.0f; // +/- keys: speed adjustment (0.1x to 3.0x)

// Handle non-blocking keyboard input for interactive controls
// Called each frame to check for user input without blocking
void handle_keyboard_input()
{
    int ch = getch();
    if (ch == ERR)
        return; // No key pressed

    switch (ch)
    {
    case 'P':
    case 'p':
        paused = (paused == FALSE_BYTE) ? TRUE_BYTE : FALSE_BYTE;
        std::cerr << (paused != FALSE_BYTE ? "\n[PAUSED]\n" : "\n[RESUMED]\n");
        std::cerr.flush();
        break;
    case '+':
    case '=':
        speed_multiplier += 0.1f;
        if (speed_multiplier > 3.0f)
            speed_multiplier = 3.0f;
        std::cerr << "\n[Speed: " << speed_multiplier << "x]\n";
        std::cerr.flush();
        break;
    case '-':
    case '_':
        speed_multiplier -= 0.1f;
        if (speed_multiplier < 0.1f)
            speed_multiplier = 0.1f;
        std::cerr << "\n[Speed: " << speed_multiplier << "x]\n";
        std::cerr.flush();
        break;
    case 'R':
    case 'r':
        matrix_columns.clear();
        std::cerr << "\n[RESET]\n";
        std::cerr.flush();
        break;
    }
}

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
    noecho();              // don't echo keypresses
    curs_set(0);           // hide cursor
    nodelay(stdscr, TRUE); // Make input non-blocking
    cbreak();              // Disable line buffering

    // Initialize color pairs with BLACK background for solid look
    // Skip COLOR_BLACK (0)
    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, COLOR_YELLOW, COLOR_BLACK);
    init_pair(3, COLOR_BLUE, COLOR_BLACK);
    init_pair(4, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(5, COLOR_CYAN, COLOR_BLACK);
    init_pair(6, COLOR_WHITE, COLOR_BLACK);

    // Set window background to black
    bkgd(COLOR_PAIR(1) | ' '); // Black background with space character

    // Reserve COLOR_PAIR(6) (white) for head highlight only.
    // Regular columns cycle through pairs 1-5 so head remains distinct.
    int total_colors = 5;

    // ---------------------------------------------------------
    // Bind C++ variables to Python
    // Python can read/write these via cpp.variable_name
    // ---------------------------------------------------------
    int max_rows, max_cols;
    getmaxyx(stdscr, max_rows, max_cols); // Get initial terminal size

    // Bind animation state vector (Python mutates this directly)
    PyInterface::bind("columns", matrix_columns);

    // Bind terminal dimensions (Python reads for boundary checks)
    PyInterface::bind("max_rows", max_rows);
    PyInterface::bind("max_cols", max_cols);

    // Bind control states (keyboard modifies, Python reads)
    PyInterface::bind("paused", paused);
    PyInterface::bind("speed_multiplier", speed_multiplier);

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
    // Main render loop: ~30 FPS animation cycle
    // 1. Call Python to update animation state (column positions, speeds, etc.)
    // 2. Check keyboard input for interactive controls
    // 3. Detect terminal resize and update dimensions
    // 4. Render updated state to terminal
    // 5. Sleep to maintain consistent frame rate
    // ---------------------------------------------------------
    int frame_count = 0;
    while (running)
    {
        PyObject *result = PyObject_CallObject(updateFunc, nullptr);

        if (result)
        {
            Py_DECREF(result);

            // STEP 1: Check for keyboard input (non-blocking)
            // Updates paused, speed_multiplier, or clears matrix_columns
            handle_keyboard_input();

            // STEP 2: Detect terminal resize and update dimensions
            // Python reads max_rows/max_cols on next frame to adjust columns
            resize_term(0, 0);                    // Let ncurses resize internal buffers
            getmaxyx(stdscr, max_rows, max_cols); // Update bound variables

            // STEP 3: Clear screen and prepare for drawing
            clear(); // Fills entire screen with black background

            // Debug output on first frame and every 50 frames
            if (frame_count == 0 || frame_count % 50 == 0)
            {
                std::cerr << "Frame " << frame_count << ": columns=" << matrix_columns.size() << ", "
                          << "max_rows=" << max_rows << " max_cols=" << max_cols
                          << "\n";
            }

            // STEP 4: Render all columns by reading Python-updated state
            // Each column's pos, speed, trail, chars were updated by Python
            render_matrix_rain(matrix_columns, max_rows, max_cols, total_colors);

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

        std::this_thread::sleep_for(std::chrono::microseconds(16667 / 2)); // ~30 FPS
        // std::this_thread::sleep_for(std::chrono::microseconds(16667*2)); // ~120 FPS
        // std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 10 FPS for easier testing
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

// C++ RENDER: Draw matrix rain columns with 3-color brightness gradient
// Reads animation state from Python-updated MatrixColumn structs
// Creates visual effect: dim tail → medium middle → bright near-head → white head
void render_matrix_rain(const std::vector<MatrixColumn> &columns, int max_rows, int max_cols, int total_colors)
{
    (void)total_colors; // Suppress unused parameter warning (kept for API compatibility)

    // Iterate through each column (one per terminal column up to max_cols)
    for (size_t x = 0; x < columns.size() && x < static_cast<size_t>(max_cols); ++x)
    {
        const auto &column = columns[x];

        // Read Python-updated animation state for this column
        int pos = static_cast<int>(column.pos);  // Head Y position (bottom of trail)
        int trail = column.trail;                // Number of characters in trail (5-20)
        const std::string &chars = column.chars; // Character sequence to display

        // Base color for this column (cycles through 1-5)
        int base_color = (x % 5) + 1; // Range: 1-5 (GREEN, YELLOW, BLUE, MAGENTA, CYAN)

        // Define 3-color gradient per base color (avoid black foreground)
        // Gradient: [tail color, middle color, near-head color]
        int gradient[3];
        switch (base_color)
        {
        case 1:              // GREEN column
            gradient[0] = 3; // BLUE (dim)
            gradient[1] = 1; // GREEN (medium)
            gradient[2] = 5; // CYAN (bright)
            break;
        case 2:              // YELLOW column
            gradient[0] = 4; // MAGENTA (dim)
            gradient[1] = 2; // YELLOW (medium)
            gradient[2] = 5; // CYAN (bright)
            break;
        case 3:              // BLUE column
            gradient[0] = 3; // BLUE (dim)
            gradient[1] = 5; // CYAN (medium)
            gradient[2] = 2; // YELLOW (bright)
            break;
        case 4:              // MAGENTA column
            gradient[0] = 3; // BLUE (dim)
            gradient[1] = 4; // MAGENTA (medium)
            gradient[2] = 2; // YELLOW (bright)
            break;
        case 5:              // CYAN column
            gradient[0] = 3; // BLUE (dim)
            gradient[1] = 5; // CYAN (medium)
            gradient[2] = 2; // YELLOW (bright)
            break;
        default:
            gradient[0] = gradient[1] = gradient[2] = 1; // Fallback to GREEN
            break;
        }

        // Draw each character in the trail from top to bottom
        // i=0 is top (tail), i=trail-1 is head
        for (int i = 0; i < trail && i < static_cast<int>(chars.size()); ++i)
        {
            // Calculate Y coordinate: trail flows from (pos-trail+1) down to pos
            int y = pos - (trail - 1 - i);

            // Only draw if within screen bounds
            if (y >= 0 && y < max_rows)
            {
                int color_pair;

                if (i == trail - 1)
                {
                    // Head (bottom): always white for maximum brightness
                    color_pair = 6; // WHITE
                }
                else
                {
                    // Trail characters: map position to gradient segment (0, 1, or 2)
                    // Divide trail into 3 equal segments based on character position
                    int segment = (i * 3) / trail; // Range: 0-2 (0=tail, 1=middle, 2=near-head)
                    if (segment > 2)
                        segment = 2; // Safety clamp

                    color_pair = gradient[segment];
                }

                // Apply color and draw character
                attron(COLOR_PAIR(color_pair));
                mvaddch(y, static_cast<int>(x), chars[i]);
                attroff(COLOR_PAIR(color_pair));
            }
        }
    }
}
