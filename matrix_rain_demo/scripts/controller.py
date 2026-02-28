import cpp
import random
import sys

# Matrix characters pool
MATRIX_CHARS = (
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    "!@#$%^&*()_+-=[]{}|;:,.<>?/~"
)

initialized = False
frame_count = 0


def update_values():
    global initialized, frame_count
    frame_count += 1

    try:
        max_cols = cpp.max_cols
        max_rows = cpp.max_rows
    except AttributeError as e:
        sys.stderr.write(f"ERROR: Cannot access cpp variables: {e}\n")
        sys.stderr.flush()
        return

    # Debug output on first frame
    if frame_count == 1:
        sys.stderr.write(
            f"Python: First frame, max_rows={max_rows}, max_cols={max_cols}\n"
        )
        sys.stderr.flush()

    columns = cpp.columns

    # Ensure we have at least one column state per screen column.
    # VectorProxy supports append_new() for vectors of structs.
    while len(columns) < max_cols:
        column = columns.append_new()
        column.pos = float(random.randint(max_rows // 4, max_rows))
        column.speed = float(random.uniform(2.0, 4.0))
        column.trail = int(random.randint(5, 20))
        column.chars = "".join(random.choice(MATRIX_CHARS) for _ in range(column.trail))

    initialized = True

    # PYTHON UPDATE: Update animation state in MatrixColumn structs bound to C++
    # C++ will read these fields (pos, speed, trail, chars) to render each frame
    visible_cols = min(len(columns), max_cols)
    for index in range(visible_cols):
        column = columns[index]

        # Move column down by adding speed to position
        column.pos = float(column.pos + column.speed)

        # Reset column when it goes completely off-screen (recycle for continuous rain)
        if column.pos > max_rows + column.trail:
            column.pos = float(
                random.randint(-column.trail * 2, 0)
            )  # Start above screen
            column.speed = float(random.uniform(2.0, 4.0))  # Randomize fall speed
            column.trail = int(random.randint(5, 20))  # Randomize trail length
            column.chars = "".join(  # Generate new character sequence
                random.choice(MATRIX_CHARS) for _ in range(column.trail)
            )

        # Occasionally randomize a character in the trail
        if random.random() < 0.1 and column.trail > 0:
            chars = column.chars
            # Keep chars length aligned with trail length
            if len(chars) != column.trail:
                chars = "".join(
                    random.choice(MATRIX_CHARS) for _ in range(column.trail)
                )
            mutation_index = random.randint(0, column.trail - 1)
            chars = (
                chars[:mutation_index]
                + random.choice(MATRIX_CHARS)
                + chars[mutation_index + 1 :]
            )
            column.chars = chars

    if frame_count % 50 == 0:
        sys.stderr.write(
            "Python: Frame "
            f"{frame_count}, visible_cols={visible_cols}, "
            f"total_bound_cols={len(columns)}\n"
        )
        sys.stderr.flush()
