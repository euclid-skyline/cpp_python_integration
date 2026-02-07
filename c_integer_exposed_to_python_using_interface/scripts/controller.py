import cppbridge
import random

def update_value():
    # read C++ value
    h = cppbridge.cpp_get("health")

    # modify it
    h += random.randint(-5, 5)

    # clamp
    h = max(0, min(100, h))

    # write back
    cppbridge.cpp_set("health", h)
    