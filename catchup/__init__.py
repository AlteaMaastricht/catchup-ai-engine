"""Catchup: a hexagonal stone-placement game engine.

The rules, search and console player are implemented in C++.  This package
exposes them to Python (ctypes) so the Tkinter board can keep working.

Build the library first:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
"""

__all__ = ["board", "state", "search", "ui", "gui", "record"]
