import cpp


def update_values():

    print("=== 1) Simple struct: Player ===")
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
    print("Team average in C++:", cpp.team.average)
    print(
        "Team scores in C++:", [cpp.team.scores[i] for i in range(len(cpp.team.scores))]
    )

    # Modify inner vector
    cpp.team.scores[0] = 111
    cpp.team.scores.append(444)

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
