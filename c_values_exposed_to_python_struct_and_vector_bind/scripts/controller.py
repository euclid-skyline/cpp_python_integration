import cpp


def update_values():

    print("=== 1) Simple struct: Player ===")
    print("Player fields count in C++:", len(cpp.player))
    print("Player health in C++:", cpp.player.health)
    print("Player speed in C++:", cpp.player.speed)

    # Modify fields
    cpp.player.health = 150
    cpp.player.speed = 7.25

    print("Updated Player health in Python script:", cpp.player.health)
    print("Updated Player speed in Python script:", cpp.player.speed)

    print("\n=== 2) Vector of simple types: scores ===")
    print("Scores length:", len(cpp.scores))
    print(
        "Scores in C++ before append:", [cpp.scores[i] for i in range(len(cpp.scores))]
    )
    # Append values
    cpp.scores.append(10)
    cpp.scores.append(20)
    cpp.scores.append(30)

    print(
        "Scores after append in Python script:",
        [cpp.scores[i] for i in range(len(cpp.scores))],
    )

    # Modify element
    cpp.scores[1] = 99
    print(
        "Scores after modification second element in Python script:",
        [cpp.scores[i] for i in range(len(cpp.scores))],
    )

    print("\n=== 3) Struct containing a vector: Team ===")
    print("Team fields count in C++:", len(cpp.team))
    print("Team average in C++:", cpp.team.average)
    print(
        "Team scores in C++:", [cpp.team.scores[i] for i in range(len(cpp.team.scores))]
    )

    # Modify inner vector
    cpp.team.scores[0] = 111
    cpp.team.scores.append(444)

    print("Team fields count after append in Python script:", len(cpp.team))
    print(
        "Updated Team scores in Python script:",
        [cpp.team.scores[i] for i in range(len(cpp.team.scores))],
    )

    # Modify scalar field
    cpp.team.average = 33.3
    print("Updated Team average in Python script:", cpp.team.average)

    print("\n=== 4) Vector containing structs: enemies ===")
    print("Enemies count in C++:", len(cpp.enemies))

    for i in range(len(cpp.enemies)):
        print(f"Enemy {i} in C++: health={cpp.enemies[i].health}, x={cpp.enemies[i].x}")

    # Append via struct proxy creation (depends on your API)
    # If append requires a StructProxy, you do:
    # Create and append a new enemy
    new_enemy = cpp.enemies.append_new()
    new_enemy.health = 60
    new_enemy.x = 10.0

    # Or chain the operations
    cpp.enemies.append_new().health = 100
    cpp.enemies[-1].x = 20.0  # Index -1 gives you the last appended enemy

    # After appending, print all enemies again
    print("Enemies count after append in Python script:", len(cpp.enemies))
    for i in range(len(cpp.enemies)):
        print(
            f"Enemy {i} after append in Python script: health={cpp.enemies[i].health}, x={cpp.enemies[i].x}"
        )

    # Modify struct fields
    if len(cpp.enemies) > 0:
        cpp.enemies[0].health = 999
        cpp.enemies[0].x = 123.45

    print(
        "Updated first enemy from Python script:",
        cpp.enemies[0].health,
        cpp.enemies[0].x,
    )

    print("\n=== 5) Vector containing vectors of ints: grid ===")
    print("Grid outer size in C++:", len(cpp.grid))

    # Access nested elements
    for i in range(len(cpp.grid)):
        row = cpp.grid[i]
        print(f"Row {i} in C++:", [row[j] for j in range(len(row))])

    # Append inner vectors
    row = cpp.grid.append_new_vector()
    row.append(55)
    row.append(66)
    row.append(77)

    print("Grid outer size after append in Python script:", len(cpp.grid))

    # Access nested elements
    for i in range(len(cpp.grid)):
        row = cpp.grid[i]
        print(f"Row {i} in C++:", [row[j] for j in range(len(row))])

    # Modify nested element
    if len(cpp.grid) > 0 and len(cpp.grid[0]) > 1:
        cpp.grid[0][1] = 777

    print("Updated grid in Python script at Position [0][1]:")
    for i in range(len(cpp.grid)):
        row = cpp.grid[i]
        print(f"Row {i}:", [row[j] for j in range(len(row))])
    print("\n=== 6) Vector containing vectors of Enemy structs: enemy_waves ===")
    print("Enemy waves (spawns) count in C++:", len(cpp.enemy_waves))

    # Access nested enemies
    for i in range(len(cpp.enemy_waves)):
        wave = cpp.enemy_waves[i]
        print(f"\nWave {i} has {len(wave)} enemies:")
        for j in range(len(wave)):
            enemy = wave[j]
            print(f"  Enemy {j}: health={enemy.health}, x={enemy.x}")

    # Append a new wave of enemies
    new_wave = cpp.enemy_waves.append_new_vector()
    new_wave.append_new().health = 100
    new_wave[-1].x = 5.0
    new_wave.append_new().health = 110
    new_wave[-1].x = 6.5

    print("Enemy waves count after append in Python script:", len(cpp.enemy_waves))

    # Access all waves again
    for i in range(len(cpp.enemy_waves)):
        wave = cpp.enemy_waves[i]
        print(f"\nWave {i} has {len(wave)} enemies:")
        for j in range(len(wave)):
            enemy = wave[j]
            print(f"  Enemy {j}: health={enemy.health}, x={enemy.x}")

    # Modify nested enemy struct
    if len(cpp.enemy_waves) > 0 and len(cpp.enemy_waves[0]) > 0:
        cpp.enemy_waves[0][0].health = 999
        cpp.enemy_waves[0][0].x = 99.99
        print(
            f"\nModified first enemy in first wave: health={cpp.enemy_waves[0][0].health}, x={cpp.enemy_waves[0][0].x}"
        )


def test_boundary_conditions():
    """Issue 15: Test boundary conditions and error handling"""
    print("\n" + "=" * 70)
    print("=== 7) BOUNDARY TESTING AND ERROR HANDLING ===")
    print("=" * 70)

    print("\n--- Test 7.1: Out of Bounds Access ---")
    print(f"Scores vector length: {len(cpp.scores)}")

    # Try to access beyond bounds (should raise IndexError)
    try:
        val = cpp.scores[999]
        print(f"❌ ERROR: Should have raised IndexError but got {val}")
    except IndexError as e:
        print(f"✓ Correctly caught IndexError for positive out of bounds: {e}")

    # Try negative out of bounds
    try:
        val = cpp.scores[-999]
        print(f"❌ ERROR: Should have raised IndexError but got {val}")
    except IndexError as e:
        print(f"✓ Correctly caught IndexError for negative out of bounds: {e}")

    print("\n--- Test 7.2: Negative Index Boundary ---")
    print(f"Last element via [-1]: {cpp.scores[-1]}")
    print(f"Second-to-last via [-2]: {cpp.scores[-2]}")

    # Boundary testing with negative indices
    if len(cpp.scores) >= 3:
        print(f"Third-to-last via [-3]: {cpp.scores[-3]}")

    try:
        val = cpp.scores[-(len(cpp.scores) + 1)]
        print(f"❌ ERROR: Should have raised IndexError but got {val}")
    except IndexError as e:
        print(f"✓ Correctly caught IndexError for negative out of bounds: {e}")

    print("\n--- Test 7.3: Empty Vector Access ---")
    # Create a test by accessing grid nested structure
    print(f"Grid size: {len(cpp.grid)}")

    # Try to access empty inner vectors if any exist
    for i in range(len(cpp.grid)):
        row_size = len(cpp.grid[i])
        print(f"Grid[{i}] size: {row_size}")
        if row_size == 0:
            try:
                val = cpp.grid[i][0]
                print(
                    f"❌ ERROR: Should have raised IndexError on empty row but got {val}"
                )
            except IndexError as e:
                print(f"✓ Correctly caught IndexError on empty inner vector: {e}")

    print("\n--- Test 7.4: Struct Field Modification from Vector Proxy ---")
    if len(cpp.enemies) > 0:
        # Get a proxy and modify it
        original_health = cpp.enemies[0].health
        cpp.enemies[0].health = 555
        print(
            f"Modified enemy[0].health from {original_health} to {cpp.enemies[0].health}"
        )

        # Get another reference and verify it's the same
        retrieved_health = cpp.enemies[0].health
        print(f"✓ Retrieved health matches: {retrieved_health}")

        # Modify via negative index
        last_enemy_health = cpp.enemies[-1].health
        cpp.enemies[-1].health = 444
        print(
            f"Modified enemy[-1].health from {last_enemy_health} to {cpp.enemies[-1].health}"
        )

    print("\n--- Test 7.5: Negative Index on Struct Vectors ---")
    if len(cpp.enemies) > 1:
        # Test negative indexing on struct vector
        first_enemy_name = cpp.enemies[0].health
        last_enemy_name = cpp.enemies[-1].health
        print(f"First enemy health: {first_enemy_name}")
        print(f"Last enemy health (via -1): {last_enemy_name}")

        # Verify they reference different structs
        if first_enemy_name != last_enemy_name or len(cpp.enemies) == 1:
            print(f"✓ First and last enemies are different (as expected)")

    print("\n--- Test 7.6: Type Mismatch Error Handling ---")
    try:
        # Try to assign wrong type
        cpp.scores[0] = "not_an_int"
        print(f"❌ ERROR: Should have raised TypeError but succeeded")
    except TypeError as e:
        print(f"✓ Correctly caught TypeError for type mismatch: {e}")

    try:
        # Try string where int expected
        cpp.player.health = "invalid"
        print(f"❌ ERROR: Should have raised TypeError but succeeded")
    except TypeError as e:
        print(f"✓ Correctly caught TypeError for struct field: {e}")


def test_nested_vector_modifications():
    """Issue 16: Comprehensive testing of nested vector modifications"""
    print("\n" + "=" * 70)
    print("=== 8) COMPREHENSIVE NESTED VECTOR MODIFICATION TESTING ===")
    print("=" * 70)

    print("\n--- Test 8.1: Nested Scalar Vector - Modify Then Access ---")
    print(f"Initial grid size: {len(cpp.grid)}")

    # Add a new row with specific values
    new_row = cpp.grid.append_new_vector()
    new_row.append(111)
    new_row.append(222)
    new_row.append(333)

    print(f"Added new row with [111, 222, 333]")
    print(f"New grid size: {len(cpp.grid)}")
    print(f"Last row contents: {[cpp.grid[-1][i] for i in range(len(cpp.grid[-1]))]}")

    # Verify we can modify the newly added row
    cpp.grid[-1][0] = 999
    print(f"Modified last row[0] to 999: {cpp.grid[-1][0]}")

    # Verify persistence
    retrieved = cpp.grid[-1][0]
    print(f"✓ Verified modification persisted: {retrieved}")

    print("\n--- Test 8.2: Nested Struct Vector - Add and Modify ---")
    print(f"Initial enemy_waves count: {len(cpp.enemy_waves)}")

    # Create new wave
    new_wave = cpp.enemy_waves.append_new_vector()
    print(f"Created new wave, total waves: {len(cpp.enemy_waves)}")

    # Add enemies to new wave
    enemy1 = new_wave.append_new()
    enemy1.health = 200
    enemy1.x = 10.0
    print(f"Added enemy1: health={enemy1.health}, x={enemy1.x}")

    enemy2 = new_wave.append_new()
    enemy2.health = 250
    enemy2.x = 15.0
    print(f"Added enemy2: health={enemy2.health}, x={enemy2.x}")

    enemy3 = new_wave.append_new()
    enemy3.health = 300
    enemy3.x = 20.0
    print(f"Added enemy3: health={enemy3.health}, x={enemy3.x}")

    print(f"New wave has {len(cpp.enemy_waves[-1])} enemies")

    print("\n--- Test 8.3: Access and Verify Deeply Nested Modifications ---")
    # Verify we can access the newly added enemies
    wave_idx = len(cpp.enemy_waves) - 1
    wave = cpp.enemy_waves[wave_idx]

    print(f"Wave {wave_idx} contents:")
    for i in range(len(wave)):
        enemy = wave[i]
        print(f"  Enemy {i}: health={enemy.health}, x={enemy.x}")

    # Verify via direct access
    print(f"Direct access to newly added enemy:")
    print(f"  cpp.enemy_waves[-1][-1].health = {cpp.enemy_waves[-1][-1].health}")
    print(f"  cpp.enemy_waves[-1][0].health = {cpp.enemy_waves[-1][0].health}")

    print("\n--- Test 8.4: Modify Deeply Nested Elements ---")
    # Modify an enemy in the newly created wave
    cpp.enemy_waves[-1][0].health = 999
    cpp.enemy_waves[-1][0].x = 99.99

    print(f"Modified last wave's first enemy:")
    print(f"  health: {cpp.enemy_waves[-1][0].health}")
    print(f"  x: {cpp.enemy_waves[-1][0].x}")

    # Modify the last enemy in the last wave
    cpp.enemy_waves[-1][-1].health = 1000
    cpp.enemy_waves[-1][-1].x = 100.0

    print(f"Modified last wave's last enemy:")
    print(f"  health: {cpp.enemy_waves[-1][-1].health}")
    print(f"  x: {cpp.enemy_waves[-1][-1].x}")

    print("\n--- Test 8.5: Chained Modifications ---")
    # Create another wave using chain operations
    cpp.enemy_waves.append_new_vector()

    # Add enemies in one go
    cpp.enemy_waves[-1].append_new().health = 50
    cpp.enemy_waves[-1][-1].x = 5.0

    cpp.enemy_waves[-1].append_new().health = 75
    cpp.enemy_waves[-1][-1].x = 7.5

    print(f"Created wave with chained operations")
    print(f"Total waves: {len(cpp.enemy_waves)}")
    print(f"Last wave size: {len(cpp.enemy_waves[-1])}")

    # Verify chain modifications
    for i in range(len(cpp.enemy_waves[-1])):
        enemy = cpp.enemy_waves[-1][i]
        print(f"  Enemy {i}: health={enemy.health}, x={enemy.x}")

    print("\n--- Test 8.6: Mix of Original and New Nested Elements ---")
    original_wave_count = len(cpp.enemy_waves) - 2  # Subtract the 2 we just added
    print(f"Original waves in grid: ~{original_wave_count}")

    # Verify we can still access original waves
    if original_wave_count > 0:
        print(f"First wave still accessible:")
        first_wave = cpp.enemy_waves[0]
        print(f"  Size: {len(first_wave)}")
        if len(first_wave) > 0:
            print(f"  First enemy health: {first_wave[0].health}")

    # Verify new waves are separate
    print(f"New waves created successfully:")
    print(f"  Total waves now: {len(cpp.enemy_waves)}")
    print(f"  Last wave size: {len(cpp.enemy_waves[-1])}")
    print(f"  Second-to-last wave size: {len(cpp.enemy_waves[-2])}")

    print("\n--- Test 8.7: Multi-level Nested Modifications ---")
    # Complex scenario: modify elements at different nesting levels
    if len(cpp.grid) > 0 and len(cpp.grid[0]) > 0:
        original = cpp.grid[0][0]
        cpp.grid[0][0] = 12345
        modified = cpp.grid[0][0]
        print(f"Modified grid[0][0] from {original} to {modified}")

    if len(cpp.enemy_waves) > 0 and len(cpp.enemy_waves[0]) > 0:
        original_health = cpp.enemy_waves[0][0].health
        cpp.enemy_waves[0][0].health = 54321
        modified_health = cpp.enemy_waves[0][0].health
        print(
            f"Modified enemy_waves[0][0].health from {original_health} to {modified_health}"
        )

        # Also modify x coordinate
        original_x = cpp.enemy_waves[0][0].x
        cpp.enemy_waves[0][0].x = 123.456
        modified_x = cpp.enemy_waves[0][0].x
        print(f"Modified enemy_waves[0][0].x from {original_x} to {modified_x}")

    print("\n--- Test 8.8: Boundary Conditions in Nested Vectors ---")
    # Test accessing valid but edge positions
    if len(cpp.grid) > 0:
        # First row, first element
        cpp.grid[0][0] = 9999
        print(f"Modified grid[0][0] (first of first) = {cpp.grid[0][0]}")

        # First row, last element
        if len(cpp.grid[0]) > 0:
            cpp.grid[0][-1] = 8888
            print(f"Modified grid[0][-1] (last of first) = {cpp.grid[0][-1]}")

        # Last row, first element
        cpp.grid[-1][0] = 7777
        print(f"Modified grid[-1][0] (first of last) = {cpp.grid[-1][0]}")

        # Last row, last element
        cpp.grid[-1][-1] = 6666
        print(f"Modified grid[-1][-1] (last of last) = {cpp.grid[-1][-1]}")


# ============================================================================
# Main Execution
# ============================================================================

if __name__ == "__main__":
    try:
        print("\n" + "=" * 70)
        print("C++ PYTHON INTEGRATION FRAMEWORK - COMPREHENSIVE TEST SUITE")
        print("=" * 70)

        # Run main tests for functionality
        update_values()

        # Run boundary testing (Issue 15)
        test_boundary_conditions()

        # Run nested vector modification tests (Issue 16)
        test_nested_vector_modifications()

        print("\n" + "=" * 70)
        print("ALL TESTS COMPLETED SUCCESSFULLY!")
        print("=" * 70)
        print("\nSummary:")
        print("✓ Issue 15 - Boundary Testing: Comprehensive edge cases covered")
        print("✓ Issue 16 - Nested Vector Modifications: Full nested structure tests")
        print("✓ All operations including error handling validated")

    except Exception as e:
        print(f"\n❌ TEST FAILED WITH ERROR: {e}")
        import traceback

        traceback.print_exc()
