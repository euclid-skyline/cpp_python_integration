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

### Keyboard Controls

- **P** - Pause/Resume animation
- **+** - Increase speed (0.1x to 3.0x)
- **-** - Decrease speed
- **R** - Reset (clear all columns and restart)

## How It Works

### Architecture Overview

The Matrix rain demo demonstrates a **split-responsibility design**:

**C++ Side (main.cpp):**
- Embeds Python interpreter
- Owns the curses terminal and render loop
- Exposes C++ data structures to Python via binding system
- Calls Python's `update_values()` function each frame
- Reads animation state from bound vector and renders to terminal
- Handles keyboard input and terminal resize

**Python Side (controller.py):**
- Controls animation logic
- Mutates C++ data structures directly via proxy interfaces
- Updates column positions, speeds, trails, and character sequences
- Resets off-screen columns for continuous rain effect
- No return values needed - works by side effects on bound data

### Animation State Variables

The Matrix rain animation works by managing a **column** for each X position on the terminal. Each column in the `matrix_columns` vector is a `MatrixColumn` struct with 4 state variables.

Let me explain with a concrete example first, then detail each variable:

---

#### Complete Example: One Column Falling Down Screen

**Setup:**
- Column at X=10 (horizontal position, 10th character from left)
- `chars = "ABC"` (3-character string)
- `trail = 3` (length of chars string)
- `pos = 0.0` (Y position - starts at TOP of screen on Frame 1)
- `speed = 1.0` (moves down 1 row per frame)

**Frame-by-Frame Visualization:**

```
Frame 1: pos=0.0 (INITIALIZATION - Column starts at top)
Terminal (showing column X=10 only):
Row 0:  'C'  ← chars[2] (HEAD/bottom - brightest WHITE+BOLD) ← pos is HERE
Row -1: 'B'  ← chars[1] (above screen, not visible)
Row -2: 'A'  ← chars[0] (above screen, not visible)

Wait, that's wrong. Let me recalculate...
Actually: pos=0, trail=3, so:
  chars[0] at row: 0 - (3-1) = -2 (above screen)
  chars[1] at row: 0 - (3-2) = -1 (above screen)
  chars[2] at row: 0 - (3-3) = 0 (at row 0)

Frame 2: pos=1.0 (moved down by speed=1.0)
Terminal:
Row -1: 'A'  ← chars[0] (still above screen)
Row 0:  'B'  ← chars[1] (now at top - dimmer)
Row 1:  'C'  ← chars[2] (HEAD - brightest) ← pos is HERE

Frame 3: pos=2.0
Terminal:
Row 0:  'A'  ← chars[0] (now at top)
Row 1:  'B'  ← chars[1] (middle)
Row 2:  'C'  ← chars[2] (HEAD - brightest) ← pos is HERE

Frame 4: pos=3.0
Terminal:
Row 1:  'A'  ← chars[0]
Row 2:  'B'  ← chars[1]
Row 3:  'C'  ← chars[2] (HEAD) ← pos is HERE

Frame 5: pos=4.0
Terminal:
Row 2:  'A'
Row 3:  'B'
Row 4:  'C'  ← pos is HERE
```

**Key Understanding:**
- `pos` points to the **LAST character** in the string (chars[trail-1])
- This is the **HEAD** (bottom) of the trail - the brightest character
- The trail extends **UPWARD** from pos
- chars[0] is at the **TOP** (pos - trail + 1)
- chars[trail-1] is at the **BOTTOM** (pos)

---

#### Now Let's Detail Each Variable:

#### 1. **`trail`** (int) - How Many Characters to Show

**What it is:**
- A simple integer count: 5, 10, 15, 20, etc.
- Defines HOW MANY characters are visible in this falling column

**How Python selects it:**
```python
# When initializing or resetting a column:
column.trail = int(random.randint(5, 20))  # Random between 5 and 20
```

**What it controls on screen:**
```
trail=3  →  Shows 3 characters vertically
trail=8  →  Shows 8 characters vertically  
trail=15 →  Shows 15 characters vertically
```

**Example comparison:**
```
Column with trail=3:          Column with trail=7:
Row 5: 'X'  ← top             Row 8:  'Q'  ← top
Row 6: 'Y'                    Row 9:  'W'
Row 7: 'Z'  ← head (pos)      Row 10: 'E'
                              Row 11: 'R'
                              Row 12: 'T'
                              Row 13: 'Y'
                              Row 14: 'U'  ← head (pos)
```

---

#### 2. **`chars`** (string) - What Characters to Display

**What it is:**
- A string containing exactly `trail` number of characters
- Example: if trail=5, chars might be "K7$pQ"

**How Python generates it:**
```python
# Character pool to choose from:
MATRIX_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()_+-=[]{}|;:,.<>?/~"

# When initializing or resetting:
column.trail = int(random.randint(5, 20))  # First pick trail length
column.chars = "".join(random.choice(MATRIX_CHARS) for _ in range(column.trail))
# This creates a string with EXACTLY trail characters
```

**Example generation:**
```python
trail = 5
chars = "K7$pQ"  # Random 5 characters from pool
# Length check: len(chars) == trail → len("K7$pQ") == 5 ✓

trail = 10  
chars = "aB3#xZ9!mP"  # Random 10 characters
# Length check: len(chars) == trail → len("aB3#xZ9!mP") == 10 ✓
```

**How it displays on screen:**
```
chars = "K7$pQ" (length 5)
chars[0] = 'K'  →  Displays at TOP of trail (row: pos-4)
chars[1] = '7'  →  Displays at row: pos-3
chars[2] = '$'  →  Displays at row: pos-2
chars[3] = 'p'  →  Displays at row: pos-1
chars[4] = 'Q'  →  Displays at BOTTOM/HEAD (row: pos) ← This is the brightest
```

---

#### 3. **`pos`** (float) - Position of the HEAD (Last Character)

**CRITICAL:** `pos` is the Y-coordinate (row number) of the **LAST** character in the string!

**Formula:** 
- chars[0] appears at row: `pos - trail + 1` (TOP of trail)
- chars[1] appears at row: `pos - trail + 2`
- chars[2] appears at row: `pos - trail + 3`  
- ...
- chars[trail-1] appears at row: `pos` (BOTTOM/HEAD)

**Detailed Example with chars="MATRIX" at column X=15:**

```
Given: chars="MATRIX", trail=6, pos=10.0, column X=15

Rendering calculation for each character:
chars[0]='M': y = pos - (trail-1-0) = 10 - 5 = 5  → Row 5, Col 15: 'M'
chars[1]='A': y = pos - (trail-1-1) = 10 - 4 = 6  → Row 6, Col 15: 'A'
chars[2]='T': y = pos - (trail-1-2) = 10 - 3 = 7  → Row 7, Col 15: 'T'
chars[3]='R': y = pos - (trail-1-3) = 10 - 2 = 8  → Row 8, Col 15: 'R'
chars[4]='I': y = pos - (trail-1-4) = 10 - 1 = 9  → Row 9, Col 15: 'I'
chars[5]='X': y = pos - (trail-1-5) = 10 - 0 = 10 → Row 10, Col 15: 'X' ← HEAD

Terminal view (column 15):
Row 5:  M  (dim)
Row 6:  A  (dim)
Row 7:  T  (normal)
Row 8:  R  (BOLD - top 1/3 of trail)
Row 9:  I  (BOLD)
Row 10: X  (WHITE+BOLD - HEAD) ← pos points HERE
```

**How Python updates pos:**
```python
# Each frame:
column.pos = float(column.pos + column.speed)

# Example with speed=2.0:
Frame 1: pos = 5.0
Frame 2: pos = 5.0 + 2.0 = 7.0   (entire trail moved down 2 rows)
Frame 3: pos = 7.0 + 2.0 = 9.0   (moved down 2 more rows)
Frame 4: pos = 9.0 + 2.0 = 11.0  (and so on...)
```

---

#### 4. **`speed`** (float) - How Fast to Move Down

**What it is:**
- Number of rows per frame to move downward
- Example: speed=1.0 means move 1 row per frame, speed=2.5 means 2.5 rows per frame

**How Python selects it:**
```python
# When initializing or resetting:
speed_multiplier = cpp.speed_multiplier  # From keyboard +/- keys (default 1.0)
column.speed = float(random.uniform(2.0, 4.0) * speed_multiplier)
# Random between 2.0-4.0, then scaled by keyboard multiplier
```

**Example with chars="XYZ" at column X=5:**
```
Speed 1.0 (slow):
Frame 1: pos=10 → Row 8:'X', Row 9:'Y', Row 10:'Z'
Frame 2: pos=11 → Row 9:'X', Row 10:'Y', Row 11:'Z'  (moved 1 row)
Frame 3: pos=12 → Row 10:'X', Row 11:'Y', Row 12:'Z' (moved 1 row)

Speed 3.0 (fast):
Frame 1: pos=10 → Row 8:'X', Row 9:'Y', Row 10:'Z'
Frame 2: pos=13 → Row 11:'X', Row 12:'Y', Row 13:'Z' (moved 3 rows!)
Frame 3: pos=16 → Row 14:'X', Row 15:'Y', Row 16:'Z' (moved 3 rows!)
```

---

#### Summary: All 4 Variables Together

**Column Lifecycle Starting from Frame 1:**
```python
Column at X=20, chars="Q#7mK", trail=5, speed=2.5

Frame 1: pos=0.0 (INITIALIZATION - Column starts at top)
  chars[0]='Q' at row: 0 - (5-1-0) = -4 (above screen)
  chars[1]='#' at row: 0 - (5-1-1) = -3 (above screen)
  chars[2]='7' at row: 0 - (5-1-2) = -2 (above screen)
  chars[3]='m' at row: 0 - (5-1-3) = -1 (above screen)
  chars[4]='K' at row: 0 - (5-1-4) = 0  (HEAD at row 0, just appearing)

Frame 2: pos=2.5 (moved down by speed=2.5)
  chars[0]='Q' at row: 2.5 - 4 = -1.5 (rounding to -2, still above)
  chars[1]='#' at row: 2.5 - 3 = -0.5 (rounding to -1, still above)
  chars[2]='7' at row: 2.5 - 2 = 0.5  (rounding to 0)
  chars[3]='m' at row: 2.5 - 1 = 1.5  (rounding to 1)
  chars[4]='K' at row: 2.5 - 0 = 2.5  (rounding to 2, HEAD at row 2)

Frame 3: pos=5.0 (moved down 2.5 more)
  chars[0]='Q' at row: 5 - 4 = 1
  chars[1]='#' at row: 5 - 3 = 2
  chars[2]='7' at row: 5 - 2 = 3
  chars[3]='m' at row: 5 - 1 = 4
  chars[4]='K' at row: 5 - 0 = 5  (HEAD, brightest)

Screen View at Frame 3:
  Row 1: 'Q'  (dim - tail)
  Row 2: '#'  (dim)
  Row 3: '7'  (normal)
  Row 4: 'm'  (BOLD - top 1/3)
  Row 5: 'K'  (WHITE+BOLD - HEAD, brightest)

Frame 4: pos=7.5
  Row 3: 'Q'
  Row 4: '#'
  Row 5: '7'
  Row 6: 'm'
  Row 7: 'K'  (HEAD)

... continues until head goes off-screen ...

Frame N: pos=max_rows + trail + 2 (completely off-screen)
         Column RESET:
         pos = 0.0  (back to top)
         chars = "new random string"
         trail = random new value
         speed = random new value
         [Cycle repeats from Frame 1 of this new string]
```

---

#### Why `pos` and `speed` are Floats (Not Integers)

**Question:** Why use floats for `pos` and `speed` when screen rows are integers?

**Answer:** Floats enable **fractional speeds** and **smooth motion** with precise accumulation.

**Why `speed` is a float:**
- Allows fine-grained speeds like 0.5, 1.5, 2.5 rows per frame (not just 1, 2, 3)
- Provides smooth speed variation between columns
- Example: speed=2.5 gives perfect in-between motion (not too slow at 2, not too fast at 3)

**Why `pos` is a float:**
- Accumulates fractional movement without rounding errors
- Example showing the problem with integers:
  ```python
  # ❌ BAD: If pos were an integer with speed=2.5:
  Frame 1: pos = 5
  Frame 2: pos = 5 + int(2.5) = 5 + 2 = 7   # Lost the 0.5!
  Frame 3: pos = 7 + int(2.5) = 7 + 2 = 9   # Should be 10.0
  Frame 4: pos = 9 + int(2.5) = 9 + 2 = 11  # Should be 12.5
  # Result: Average speed = 2.0 rows/frame (NOT 2.5!)
  
  # ✅ GOOD: With pos as float:
  Frame 1: pos = 5.0
  Frame 2: pos = 5.0 + 2.5 = 7.5   # Keeps the 0.5
  Frame 3: pos = 7.5 + 2.5 = 10.0  # Correct!
  Frame 4: pos = 10.0 + 2.5 = 12.5 # Correct!
  # Result: Average speed = 2.5 rows/frame ✓
  ```

**When does float→int conversion happen?**

Only during rendering in C++:
```cpp
// In render_matrix_rain():
int y = static_cast<int>(column.pos) - (column.trail - 1 - i);
//      ^^^^^^^^^^^^^^^^^^^^^^
//      Float → Int conversion happens HERE

mvaddch(y, x, chars[i]);  // Terminal requires integer row
//      ^
//      Now it's an integer for drawing
```

**Visual comparison:**
```
Float pos (smooth, accurate):
Frame 1: pos=5.0  → draws at row 5
Frame 2: pos=7.5  → draws at row 7  (truncated from 7.5)
Frame 3: pos=10.0 → draws at row 10
Frame 4: pos=12.5 → draws at row 12 (truncated from 12.5)
Frame 5: pos=15.0 → draws at row 15
Distance: 15-5 = 10 rows in 4 frames = 2.5 rows/frame ✓

Integer pos (jerky, drift):
Frame 1: pos=5   → draws at row 5
Frame 2: pos=7   → draws at row 7  (lost 0.5)
Frame 3: pos=9   → draws at row 9  (lost 1.0 total)
Frame 4: pos=11  → draws at row 11 (lost 1.5 total)
Frame 5: pos=13  → draws at row 13 (lost 2.0 total)
Distance: 13-5 = 8 rows in 4 frames = 2.0 rows/frame ✗
```

**Summary:**
- Floats store **precise internal state** (5.0, 7.5, 10.0, 12.5)
- Integers used **only for terminal drawing** (5, 7, 10, 12)
- This ensures mathematically accurate animation without drift

---

**All 4 Variables Impact Visual Effect:**
- `pos` determines Y-position (vertical location)
- `speed` determines how fast it falls
- `trail` determines how many characters visible
- `chars` determines what characters appear
- All work together to create smooth, continuous animation

### Data Flow

```mermaid
flowchart TD
    Start([Main Loop Start - C++]) --> Step1[1. Call Python update_values]
    
    Step1 --> PythonRead[Python reads:<br/>max_rows, max_cols, paused]
    PythonRead --> PythonMutate[Python mutates matrix_columns vector:<br/>- Update pos position<br/>- Update speed<br/>- Update trail length<br/>- Update chars character sequence]
    
    PythonMutate --> Step2[2. Check keyboard input<br/>handle_keyboard_input]
    
    Step2 --> KeyboardActions{Keyboard<br/>Actions}
    KeyboardActions -->|P key| TogglePause[Toggle paused]
    KeyboardActions -->|+/- keys| AdjustSpeed[Adjust speed_multiplier]
    KeyboardActions -->|R key| ClearColumns[Clear matrix_columns]
    KeyboardActions -->|No input| Step3
    TogglePause --> Step3
    AdjustSpeed --> Step3
    ClearColumns --> Step3
    
    Step3[3. Detect terminal resize<br/>resize_term, getmaxyx] --> Step4[4. Render matrix_rain]
    
    Step4 --> RenderLoop[For each column in matrix_columns:]
    RenderLoop --> RenderRead[- Read pos, speed, trail, chars]
    RenderRead --> RenderCalc[- Calculate trail positions]
    RenderCalc --> RenderColor[- Apply color and brightness gradient]
    RenderColor --> RenderDraw[- Draw characters with mvaddch]
    
    RenderDraw --> Step5[5. Refresh screen and sleep<br/>for frame timing]
    
    Step5 --> Start

    style Start fill:#e1f5ff
    style Step1 fill:#fff4e1
    style PythonRead fill:#ffe1e1
    style PythonMutate fill:#ffe1e1
    style Step2 fill:#e1ffe1
    style Step3 fill:#f0e1ff
    style Step4 fill:#ffe1f5
    style Step5 fill:#e1fff0
```

### Renderer Logic (C++ - render_matrix_rain)

The renderer reads animation state from the bound `matrix_columns` vector and draws characters to the terminal:

**1. Column Iteration**
```cpp
for (size_t x = 0; x < columns.size() && x < max_cols; ++x)
```
- Iterates through each column in the vector
- Stops at terminal width boundary to avoid overflow
- Each column represents one vertical line of falling characters

**2. Read Animation State**
```cpp
int pos = static_cast<int>(column.pos);      // Head position (row)
int trail = column.trail;                     // Trail length
const std::string &chars = column.chars;     // Character sequence
```
- `pos`: Y-coordinate of the trail head (bottom of visible trail)
  - **Float→Int conversion:** `column.pos` is stored as `float` (e.g., 7.5) for smooth fractional movement
  - `static_cast<int>()` truncates to integer (7.5 → 7) for terminal row coordinate
  - This happens **only during rendering**, preserving precision in stored state
- `trail`: Number of characters in this column's trail (5-20)
- `chars`: String of random characters to display (length matches trail)

**3. Trail Drawing**
```cpp
for (int i = 0; i < trail; ++i)
{
    int y = pos - (trail - 1 - i);  // Calculate Y position for this character
    // Draw from top of trail (pos - trail + 1) to head (pos)
}
```
- Trail flows downward from `pos`
- Character at index `i=0` is at the top (dimmest)
- Character at index `i=trail-1` is the head (brightest)

**4. Brightness Gradient**
```cpp
if (i == trail - 1)                   // Head character
    attron(COLOR_PAIR(6) | A_BOLD);   // White + Bold = maximum brightness
else if (i >= trail - trail / 3)      // Top third of trail
    attron(A_BOLD);                   // Bold color
// else: normal intensity (dim tail)
```
- Creates visual depth with 3 brightness levels
- Head glows white, upper trail is bright, lower trail dims

**5. Character Rendering**
```cpp
mvaddch(y, static_cast<int>(x), chars[i]);
```
- Places character at calculated position
- `x` is column index (horizontal position)
- `y` is calculated from `pos` and trail offset
- `chars[i]` is the character at this position in the trail

### Python Update Logic (controller.py - update_values)

The Python controller updates animation state each frame by mutating C++ bound structures:

**1. Read Terminal State**
```python
max_cols = cpp.max_cols    # Terminal width
max_rows = cpp.max_rows    # Terminal height
paused = cpp.paused        # Pause flag (ByteBool from C++)
speed_multiplier = cpp.speed_multiplier  # Speed adjustment (1.0 = normal)
```
- Reads C++ bound scalar values
- Terminal size needed for boundary checks and resets

**2. Handle Pause**
```python
if paused and initialized:
    return  # Skip updates but keep rendering
```
- Early exit if paused (keyboard 'P' pressed)
- C++ continues rendering, but positions don't change

**3. Initialize Columns (First Frame or After Resize)**
```python
while len(columns) < max_cols:
    column = columns.append_new()           # Create new MatrixColumn struct
    column.pos = 0.0                        # All columns start at top (row 0)
    column.speed = float(random.uniform(2.0, 4.0) * speed_multiplier)
    column.trail = int(random.randint(5, 20))
    column.chars = "".join(random.choice(MATRIX_CHARS) for _ in range(column.trail))
```
- Ensures one column per terminal column
- **Synchronized start:** All columns begin at top row (0) on first frame
- **Asynchronous falling:** Different speeds and trail lengths create visual variation
- VectorProxy `append_new()` creates C++ struct instances
- Direct field assignment via proxy

**4. Update Each Column**
```python
for index in range(visible_cols):
    column = columns[index]
    
    # Move down by speed
    column.pos = float(column.pos + column.speed)
```
- Mutates `pos` field to move column downward
- **Fractional movement:** `speed` values like 2.5 enable smooth sub-pixel motion
  - Example: pos starts at 5.0, speed is 2.5
  - Frame 1: 5.0 + 2.5 = 7.5 (column moves 2.5 rows)
  - Frame 2: 7.5 + 2.5 = 10.0 (precise accumulation, no rounding error)
  - Frame 3: 10.0 + 2.5 = 12.5 (continuous fluid motion)
- Speed multiplied by `speed_multiplier` (keyboard +/- controls)

**5. Reset Off-Screen Columns**
```python
if column.pos > max_rows + column.trail:
    # Column completely off-screen, recycle it back to top
    column.pos = 0.0                       # Reset to row 0 (top of screen)
    column.speed = float(random.uniform(2.0, 4.0) * speed_multiplier)
    column.trail = int(random.randint(5, 20))
    column.chars = "".join(random.choice(MATRIX_CHARS) for _ in range(column.trail))
```
- Detects when column head passes bottom edge of screen
- Resets to top (row 0) for clean visual cycle
- Randomizes speed, trail length, and characters for variety
- New string immediately starts falling from top

**6. Character Mutation (Optional)**
```python
if random.random() < 0.1:  # 10% chance per frame
    mutation_index = random.randint(0, column.trail - 1)
    chars = chars[:mutation_index] + random.choice(MATRIX_CHARS) + chars[mutation_index + 1:]
    column.chars = chars
```
- Randomly changes one character in the trail for visual variety
- Creates "glitching" effect common in Matrix rain

### Key Design Patterns

**1. Zero-Copy Data Sharing**
- C++ owns `matrix_columns` vector
- Python gets proxy references, not copies
- Mutations directly affect C++ memory
- No serialization overhead

**2. Asymmetric Terminal Resize**
- Width increase: Python adds new columns
- Width decrease: Extra columns ignored but kept (optimization)
- Height change: Columns automatically adapt (reset threshold changes)

**3. Frame-Perfect Synchronization**
- C++ controls frame rate (30 FPS or ~33ms per frame)
- Python update happens before each render
- Consistent visual timing regardless of Python logic complexity

## License

See the repository-level `LICENSE` file.
