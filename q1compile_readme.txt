q1compile v0.3
=================

A GUI to work with Quake map compiling tools


Definitions
=================

Tools Dir      - Where the compiler tools are (qbsp.exe, light.exe, vis.exe)
Work Dir       - Temporary dir to work with files
Output Dir     - Where the compiled .bsp and .lit files will be
Engine Exe     - The Quake engine executable of your choice
Map Source     - The .map file to compile


Shortcuts
=================

Ctrl + C          - Compile the map and run Quake.
Ctrl + Shift + C  - Compile the map.
Ctrl + B          - Stop current compilation.
Ctrl + R          - Run Quake.
Ctrl + P          - Manage presets.


Issue Tracker
=================

You can open issues and request features at https://github.com/glhrmfrts/q1compile/issues


Changes - v0.3.1
=================

- Added application icon.
- Using standard Windows file dialogs.


Changes - v0.3
=================

- UI change.
- Better logging during compiling.
- If 'Run' command is issued while compilation is in progress, Quake will be run after that.
- Bugfix: Check if .lit file was produced by light.exe instead of assuming it always does.


Changes - v0.2.1
=================

- Bugfix: Reset file browser flags when selecting Engine and Map Source paths.


Changes - v0.2
=================

- If the map source file changes while compiling, stop the compilation and start again.
- Display recent configs in the menu bar.
- Added compiler tools presets.
- Requests confirmation if trying to create a new config while the current one is modified.


Changes - v0.1.1
=================

- Bugfix: Remove .bsp extension in +map argument for engines
- Bugfix: Add .cfg extension when saving configs if no extension was provived


License
=================

MIT

Authors
=================

Guilherme Nemeth - guilherme.nemeth@gmail.com - @nemethg on Quake Mapping discord server