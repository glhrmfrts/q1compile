q1compile v0.2.1
=================

A GUI to work with Quake map compiling tools


Definitions
=================

Tools Dir      - Where the compiler tools are (qbsp.exe, light.exe, vis.exe)
Work Dir       - Temporary dir to work with files
Output Dir     - Where the compiled .bsp and .lit files will be
Engine Exe     - The Quake engine executable of your choice
Map Source     - The .map file to compile


Changes - v0.2.1
=================

- Bugfix: Reset file browser flags when selecting Engine and Map Source paths


Changes - v0.2
=================

- If the map source file changes while compiling, stop the compilation and start again
- Display recent configs in the menu bar
- Added compiler tools presets
- Requests confirmation if trying to create a new config while the current one is modified


Changes - v0.1.1
=================

- Remove .bsp extension in +map argument for engines
- Add .cfg extension when saving configs if no extension was provived


Issue Tracker
=================

You can open issues and request features at https://github.com/glhrmfrts/q1compile/issues


License
=================

MIT


Authors
=================

Guilherme Nemeth - guilherme.nemeth@gmail.com - @nemethg on Quake Mapping discord server
