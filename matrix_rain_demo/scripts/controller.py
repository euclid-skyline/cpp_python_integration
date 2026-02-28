import cpp
import random
import sys

# Keyboard Controls:
# P - Pause/Resume animation
# + - Increase speed (max 3.0x)
# - - Decrease speed (min 0.1x)
# R - Reset animation (clear all columns)

# Matrix characters pool
MATRIX_CHARS = (
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    "!@#$%^&*()_+-=[]{}|;:,.<>?/~"
)

initialized = False
frame_count = 0

# ========================================================================
# PYTHON ANIMATION CONTROLLER
# ========================================================================
# This function is called by C++ every frame (~30 FPS) to update animation state.
# It mutates C++ bound data structures directly via proxy interfaces.
# No return value needed - works entirely through side effects on cpp.* variables.
# ========================================================================


def update_values():
    """Update matrix rain animation state by mutating C++ bound structures.

    Called by C++ main loop each frame. Reads terminal dimensions and control
    states, then mutates the matrix_columns vector to animate falling characters.
    """
    global initialized, frame_count
    frame_count += 1

    # -----------------------------------------------------------------------
    # STEP 1: Read C++ bound variables (terminal state and control flags)
    # -----------------------------------------------------------------------
    try:
        max_cols = cpp.max_cols  # Terminal width (columns)
        max_rows = cpp.max_rows  # Terminal height (rows)
        paused = cpp.paused  # Pause state (keyboard P)
        speed_multiplier = cpp.speed_multiplier  # Speed adjustment (keyboard +/-)
    except AttributeError as e:
        sys.stderr.write(f"ERROR: Cannot access cpp variables: {e}\n")
        sys.stderr.flush()
        return

    # -----------------------------------------------------------------------
    # STEP 2: Handle pause - skip updates but keep rendering
    # -----------------------------------------------------------------------
    if paused and initialized:
        return  # Early exit - C++ continues rendering frozen state

    # Debug output on first frame
    if frame_count == 1:
        sys.stderr.write(
            f"Python: First frame, max_rows={max_rows}, max_cols={max_cols}\n"
        )
        sys.stderr.flush()

    # -----------------------------------------------------------------------
    # STEP 3: Get reference to C++ matrix_columns vector via proxy
    # Direct mutations affect C++ memory - no copies or serialization
    # -----------------------------------------------------------------------
    columns = cpp.columns  # VectorProxy<MatrixColumn>

    # -----------------------------------------------------------------------
    # STEP 4: Initialize columns on first frame or after terminal width increase
    # Creates one column per terminal column using VectorProxy.append_new()
    # -----------------------------------------------------------------------
    while len(columns) < max_cols:
        column = columns.append_new()  # Creates C++ MatrixColumn struct

        # Initialize position: all columns start at top of screen (row 0)
        # Synchronized beginning - randomness comes from speed and trail differences
        column.pos = 0.0

        # Random speed in range 2.0-4.0, scaled by keyboard control multiplier
        column.speed = float(random.uniform(2.0, 4.0) * speed_multiplier)

        # Random trail length: 5-20 characters
        column.trail = int(random.randint(5, 20))

        # Generate random character sequence matching trail length
        column.chars = "".join(random.choice(MATRIX_CHARS) for _ in range(column.trail))

    initialized = True

    # -----------------------------------------------------------------------
    # STEP 5: Update each visible column (animate falling rain)
    # Mutations directly affect C++ memory that renderer will read
    # -----------------------------------------------------------------------
    # Only update columns that fit on screen (ignore extras on resize shrink)
    visible_cols = min(len(columns), max_cols)

    for index in range(visible_cols):
        column = columns[index]  # StructProxy<MatrixColumn>

        # Move column down by speed (pixels per frame)
        # Speed already includes keyboard multiplier from initialization
        column.pos = float(column.pos + column.speed)

        # -----------------------------------------------------------------------
        # STEP 6: Reset off-screen columns (continuous rain effect)
        # When head passes bottom edge, recycle column from top
        # -----------------------------------------------------------------------
        if column.pos > max_rows + column.trail:
            # Reset column to top of screen (row 0)
            # New string starts from top, then falls at its own speed
            column.pos = 0.0

            # Randomize speed with current multiplier
            column.speed = float(random.uniform(2.0, 4.0) * speed_multiplier)

            # Randomize trail length for variety
            column.trail = int(random.randint(5, 20))

            # Generate new character sequence
            column.chars = "".join(
                random.choice(MATRIX_CHARS) for _ in range(column.trail)
            )

        # -----------------------------------------------------------------------
        # STEP 7: Optional character mutation (glitch effect)
        # 20% chance per frame to change the HEAD character at column.pos
        # (head is chars[trail - 1], the brightest glyph on screen)
        # -----------------------------------------------------------------------
        if random.random() < 0.3 and column.trail > 0:
            chars = column.chars

            # Ensure chars string matches trail length (safety check)
            if len(chars) != column.trail:
                chars = "".join(
                    random.choice(MATRIX_CHARS) for _ in range(column.trail)
                )

            # Mutate at column.pos (HEAD): this maps to chars[trail - 1]
            mutation_index = column.trail - 1

            old_char = chars[mutation_index]
            new_char = old_char
            while new_char == old_char:
                new_char = random.choice(MATRIX_CHARS)

            chars = chars[:mutation_index] + new_char + chars[mutation_index + 1 :]
            column.chars = chars  # Mutate C++ string

    # -----------------------------------------------------------------------
    # Debug output every 50 frames to monitor performance
    # -----------------------------------------------------------------------
    if frame_count % 50 == 0:
        sys.stderr.write(
            "Python: Frame "
            f"{frame_count}, visible_cols={visible_cols}, "
            f"total_bound_cols={len(columns)}\n"
        )
        sys.stderr.flush()
