#pragma once
#include <vector>

#include "interface_builder.hpp" // REGISTER_STRUCT, REGISTER_VECTOR, FIELD

// 1. Simple struct
struct Player
{
    int health;
    float speed;
};
REGISTER_STRUCT(Player, "Player",
                FIELD(Player, health, Int, nullptr),
                FIELD(Player, speed, Float, nullptr))

// 2. Vector of simple types
extern std::vector<int> scores; // Declare the vector externally to be shared (defined in .cpp)
REGISTER_VECTOR(int, Int, nullptr)

// 3. Struct containing a vector
struct Team
{
    std::vector<int> scores;
    float average;
};
REGISTER_STRUCT(Team, "Team",
                FIELD(Team, scores, Vector, get_vector_info<int>()),
                FIELD(Team, average, Float, nullptr))

// 4. Vector containing structs
struct Enemy
{
    int health;
    float x;
};

extern std::vector<Enemy> enemies; // Declare the vector externally to be shared (defined in .cpp)
REGISTER_STRUCT(Enemy, "Enemy",
                FIELD(Enemy, health, Int, nullptr),
                FIELD(Enemy, x, Float, nullptr))
REGISTER_VECTOR(Enemy, Struct, get_struct_info<Enemy>())

// 5. Vector containing vectors
extern std::vector<std::vector<int>> grid; // Declare the vector externally to be shared (defined in .cpp)
REGISTER_VECTOR(std::vector<int>, Vector, get_vector_info<int>())

// 6. Vector containing vectors of Enemy structs
extern std::vector<std::vector<Enemy>> enemy_waves; // Declare the vector externally to be shared (defined in .cpp)
REGISTER_VECTOR(std::vector<Enemy>, Vector, get_vector_info<Enemy>())
