import cppbridge
import random


def update_values():
    # Read values
    h = cppbridge.cpp_get("health")
    speed = cppbridge.cpp_get("speed")
    name = cppbridge.cpp_get("player_name")
    alive = cppbridge.cpp_get("alive")

    NAMES = ["Euclid", "Ada", "Turing", "Hopper", "Newton"]

    alive = random.choice([True, False])
    name = random.choice(NAMES)  # Randomly change name to demonstrate string access

    # Modify them
    cppbridge.cpp_set("health", max(0, min(100, h + random.randint(-5, 5))))
    cppbridge.cpp_set("speed", speed + random.uniform(-0.5, 0.5))
    cppbridge.cpp_set(
        "alive", alive
    )  # Randomly change alive status to demonstrate bool access
    cppbridge.cpp_set(
        "player_name", name
    )  # No change, just demonstrating string access
