# Project Overview

C monorepo using GNU Autotools for build management. This is an experimental project for learning C, game development patterns, and build systems.

## Structure

```
├── libs/           # Shared libraries
│   ├── math/       # Vector math (header-only)
│   ├── ds/         # Data structures (planned)
│   ├── net/        # Networking (planned)
│   └── thread/     # Threading utilities (planned)
├── apps/           # Applications
│   └── nbody/      # N-Body simulation with SDL2
```

## Build System

Uses GNU Autotools (autoconf, automake, libtool):

```bash
autoreconf -i      # Generate configure script
./configure        # Configure for your system
make               # Build everything
```

## Running

```bash
./apps/nbody/nbody   # Run the N-Body simulation
```

Controls: ESC to quit, R to reset simulation.

## Dependencies

- SDL2 (detected via pkg-config)
- Standard C library with math (-lm)

## Patterns

- **ECS (Entity Component System)**: Used in game for managing entities with position/physics components
- **Modular design**: Separate modules for rendering, input, and game logic
