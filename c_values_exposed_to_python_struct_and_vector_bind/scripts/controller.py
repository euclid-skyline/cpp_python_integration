import cpp


def update_values():

    print("=== 1) Simple struct: Player ===")
    print("Player health:", cpp.player.health)
    print("Player speed:", cpp.player.speed)

    # Modify fields
    cpp.player.health = 150
    cpp.player.speed = 7.25

    print("Updated Player health in Python script:", cpp.player.health)
    print("Updated Player speed in Python script:", cpp.player.speed)

    print("\n=== 2) Vector of simple types: scores ===")
    print("Scores length:", len(cpp.scores))

    # Append values
    cpp.scores.append(10)
    cpp.scores.append(20)
    cpp.scores.append(30)

    print("Scores after append:",
          [cpp.scores[i] for i in range(len(cpp.scores))])

    # Modify element
    cpp.scores[1] = 99
    print("Scores after modification:",
          [cpp.scores[i] for i in range(len(cpp.scores))])

    # print("\n=== 3) Struct containing a vector: Team ===")
    # print("Team average:", cpp.team.average)
    # print("Team scores:", [cpp.team.scores[i] for i in range(len(cpp.team.scores))])

    # # Modify inner vector
    # cpp.team.scores[0] = 111
    # cpp.team.scores.append(444)

    # print("Updated Team scores:", [cpp.team.scores[i] for i in range(len(cpp.team.scores))])

    # # Modify scalar field
    # cpp.team.average = 33.3
    # print("Updated Team average:", cpp.team.average)


    # print("\n=== 4) Vector containing structs: enemies ===")
    # print("Enemies count:", len(cpp.enemies))

    # # Add enemies
    # from cpp import Enemy  # If you expose Enemy struct type; if not, skip this line

    # # Append via struct proxy creation (depends on your API)
    # # If append requires a StructProxy, you do:
    # e = cpp.enemies.append_new()  # If you implemented append_new()
    # e.health = 50
    # e.x = 10.0

    # # Or if you manually push in C++ before running Python, just read them:
    # for i in range(len(cpp.enemies)):
    #     print(f"Enemy {i}: health={cpp.enemies[i].health}, x={cpp.enemies[i].x}")

    # # Modify struct fields
    # if len(cpp.enemies) > 0:
    #     cpp.enemies[0].health = 999
    #     cpp.enemies[0].x = 123.45

    # print("Updated first enemy:", cpp.enemies[0].health, cpp.enemies[0].x)


    # print("\n=== 5) Vector containing vectors: grid ===")
    # print("Grid outer size:", len(cpp.grid))

    # # Append inner vectors
    # row = cpp.grid.append_new_vector()  # If you implemented append_new_vector()
    # row.append(1)
    # row.append(2)
    # row.append(3)

    # # Access nested elements
    # for i in range(len(cpp.grid)):
    #     row = cpp.grid[i]
    #     print(f"Row {i}:", [row[j] for j in range(len(row))])

    # # Modify nested element
    # if len(cpp.grid) > 0 and len(cpp.grid[0]) > 1:
    #     cpp.grid[0][1] = 777

    # print("Updated grid:")
    # for i in range(len(cpp.grid)):
    #     row = cpp.grid[i]
    #     print(f"Row {i}:", [row[j] for j in range(len(row))])
