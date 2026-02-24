#include "data_game_traits.hpp"

// Define global vectors with explicit empty initialization
std::vector<int> scores = {};
std::vector<Enemy> enemies = {};
std::vector<std::vector<int>> grid = {};
std::vector<std::vector<Enemy>> enemy_waves = {};

// Function pointer implementations for std::vector<int>
std::size_t int_vec_size(void *ptr)
{
    return reinterpret_cast<std::vector<int> *>(ptr)->size();
}

void *int_vec_element_ptr(void *ptr, std::size_t idx)
{
    return &(*reinterpret_cast<std::vector<int> *>(ptr))[idx];
}

bool int_vec_append(void *ptr, void *val)
{
    reinterpret_cast<std::vector<int> *>(ptr)->push_back(*static_cast<int *>(val));
    return true;
}

void *int_vec_create_empty()
{
    return new std::vector<int>();
}

void int_vec_destroy(void *ptr)
{
    delete static_cast<std::vector<int> *>(ptr);
}

// Function pointer implementations for std::vector<Enemy>
std::size_t enemy_vec_size(void *ptr)
{
    return reinterpret_cast<std::vector<Enemy> *>(ptr)->size();
}

void *enemy_vec_element_ptr(void *ptr, std::size_t idx)
{
    return &(*reinterpret_cast<std::vector<Enemy> *>(ptr))[idx];
}

bool enemy_vec_append(void *ptr, void *val)
{
    reinterpret_cast<std::vector<Enemy> *>(ptr)->push_back(*static_cast<Enemy *>(val));
    return true;
}

void *enemy_vec_create_empty()
{
    return new std::vector<Enemy>();
}

void enemy_vec_destroy(void *ptr)
{
    delete static_cast<std::vector<Enemy> *>(ptr);
}

// Function pointer implementations for std::vector<std::vector<int>>
std::size_t grid_vec_size(void *ptr)
{
    return reinterpret_cast<std::vector<std::vector<int>> *>(ptr)->size();
}

void *grid_vec_element_ptr(void *ptr, std::size_t idx)
{
    return &(*reinterpret_cast<std::vector<std::vector<int>> *>(ptr))[idx];
}

bool grid_vec_append(void *ptr, void *val)
{
    reinterpret_cast<std::vector<std::vector<int>> *>(ptr)->push_back(
        *static_cast<std::vector<int> *>(val));
    return true;
}

void *grid_vec_create_empty()
{
    return new std::vector<std::vector<int>>();
}

void grid_vec_destroy(void *ptr)
{
    delete static_cast<std::vector<std::vector<int>> *>(ptr);
}
// Function pointer implementations for std::vector<std::vector<Enemy>>
std::size_t enemy_waves_vec_size(void *ptr)
{
    return reinterpret_cast<std::vector<std::vector<Enemy>> *>(ptr)->size();
}

void *enemy_waves_vec_element_ptr(void *ptr, std::size_t idx)
{
    return &(*reinterpret_cast<std::vector<std::vector<Enemy>> *>(ptr))[idx];
}

bool enemy_waves_vec_append(void *ptr, void *val)
{
    reinterpret_cast<std::vector<std::vector<Enemy>> *>(ptr)->push_back(
        *static_cast<std::vector<Enemy> *>(val));
    return true;
}

void *enemy_waves_vec_create_empty()
{
    return new std::vector<std::vector<Enemy>>();
}

void enemy_waves_vec_destroy(void *ptr)
{
    delete static_cast<std::vector<std::vector<Enemy>> *>(ptr);
}