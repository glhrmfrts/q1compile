q1compile v0.2
=================

A GUI to work with Quake map compiling tools


Definitions
=================

Tools Dir      - Where the compiler tools are (qbsp.exe, light.exe, vis.exe)
Work Dir       - Temporary dir to work with files
Output Dir     - Where the compiled .bsp and .lit files will be
Engine Exe     - The Quake engine executable of your choice
Map Source     - The .map file to compile


Changes - v0.2
=================

- If the map source file changes while compiling, stop the compilation and start again
- Display recent configs in the menu bar


Changes - v0.1.1
=================

- Remove .bsp extension in +map argument for engines
- Add .cfg extension when saving configs if no extension was provived


Authors
=================

Guilherme Nemeth - guilherme.nemeth@gmail.com
