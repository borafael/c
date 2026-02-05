# C Experiments

A monorepo playground for C projects. Currently: progressive game development.

Built with GNU Autotools and heavy assistance from [Claude Code](https://claude.ai/code).

## Goals

- Learn C programming through hands-on projects
- Understand GNU Autotools (autoconf, automake, libtool)
- Experiment with game development patterns like ECS
- Have fun building things from scratch

## Structure

The repository follows a monorepo layout with shared libraries and applications:

```
c/
├── libs/                  # Shared libraries
│   ├── math/              # Vector math library (header-only)
│   ├── ds/                # Data structures (planned)
│   ├── net/               # Networking (planned)
│   └── thread/            # Threading utilities (planned)
├── apps/                  # Applications
│   └── nbody/             # N-Body gravitational simulation
├── configure.ac           # Autoconf configuration
├── Makefile.am            # Top-level Automake file
└── ...
```

### Planned Libraries

The following library directories exist as placeholders for future development:

- **libs/ds** - Common data structures (linked lists, hash maps, trees, etc.)
- **libs/net** - Networking primitives (sockets, protocols)
- **libs/thread** - Threading utilities (thread pools, synchronization)

## Why Autotools?

Autotools is the traditional build system for C projects on Unix-like systems. While newer alternatives exist (CMake, Meson), learning Autotools provides:
- Understanding of how `./configure && make && make install` works
- Familiarity with a system used by many foundational open-source projects
- Portable builds across different Unix systems

## Building

Prerequisites:
- GCC or Clang
- GNU Autotools (autoconf, automake, libtool)
- pkg-config
- SDL2 development libraries

```bash
# Generate the build system
autoreconf -i

# Configure for your system
./configure

# Build
make
```

## Current Projects

### N-Body Simulation (`apps/nbody`)

A visual gravitational simulation featuring:
- 2000 particles with mutual gravitational attraction
- Entity merging on collision
- Mass-based rendering (size and color vary with mass)
- Boundary collision with bounce

This is not a physically accurate simulation - it's tuned for visual entertainment.

**Controls:**
- `ESC` - Quit
- `R` - Reset simulation

```bash
./apps/nbody/nbody
```

## License

Experimental code for learning purposes.
