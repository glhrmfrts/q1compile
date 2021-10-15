## TODO: fix recent configs asking to save unsaved changes
## TODO: fix font issues

# q1compile v0.7

A GUI to work with Quake map compiling tools.

NOTE: if you already downloaded a previous version, unzip the newest version in the same directory to preserve user preferences.


## Configuration Files (.cfg)

Saves all your options, the paths you set and chosen tool preset.

Path definitions:

Tools Dir      - Where the compiler tools are (qbsp.exe, light.exe, vis.exe).
Work Dir       - Temporary dir to work with files, it MUST NOT be the same as the map source directory.
Output Dir     - Where the compiled .bsp and .lit files will be.
Editor Exe     - The editor executable (optional).
Engine Exe     - The Quake engine executable of your choice.
Map Source     - The .map file to compile.


## Tools Preset Files (.pre)

Saves flags and enabled/disabled state for the compiling tools (QBSP, LIGHT and VIS)
so it's easier to switch between compiling modes (e.g.: switch between a full compile and something faster for iteration).

It comes with a few built-in presets:

  * No Vis               - QBSP and LIGHT enabled, VIS disabled.
  * No Vis, Only ents    - QBSP and LIGHT enabled with "-onlyents" flag, VIS disabled.
  * Fast Vis             - QBSP, LIGHT and VIS enabled with "-fast" flag.
  * Full                 - QBSP, LIGHT with "-extra4 -soft" flag, and VIS with "-level 4" flag.


## Managing tool presets

You can manage tool presets through clicking the menu 'Compile -> Manage Presets',
clicking 'Manage Presets...' in the preset selection dialog in 'Tools Options' section,
or by hitting the 'Ctrl + P' keys.

That will open a new dialog where you can:

  * Add Preset        - Creates a new empty tool preset.
  * Import Preset     - Load a preset from a .pre file.

The dialog also contains a list of the all the presets available. Inside each item in the list 
there is a 'Actions...' selection dialog where you can:

  * Copy              - Create a new preset which is a duplicate of this one.
  * Select            - Make this your current preset in the configuration file.
  * Export            - Export this preset to a .pre file.


## Menus/Actions

  * File
    * New Config           Ctrl + N              Creates a new configuration file.
    * Load Config          Ctrl + O              Loads an existing configuration file.
    * Save Config          Ctrl + S              Saves the current configuration to the disk.
    * Save Config As...    Ctrl + Shift + S      Allows you to choose a new path for saving the configuration.

  * Edit
    * Open map in editor   Ctrl + E              Opens the map in the configured editor.
    * Open editor          Ctrl + Shift + E      Opens the configured editor.

  * Compile
    * Compile and run      Ctrl + C              Compile the map and run Quake.
    * Compile only         Ctrl + Shift + C      Compile the map only.
    * Stop                 Ctrl + B              Stop current compilation.

    * Run                  Ctrl + R              Run Quake, if pressed during compilation it will be run right after.

    * Clear working files  --                    Delete the map's .bsp and .lit files from the "Work Dir".
    * Clear output files   --                    Delete the map's .bsp and .lit files from the "Output Dir".

    * Reset Work Dir       Ctrl + Shift + W      Resets the 'Work Dir' to it's default, which is the user's temp directory.

    * Manage presets       Ctrl + P              Opens a window for managing tool presets.

  * Help
    * About                                      This readme.


## Video tutorial

https://youtu.be/IAhoxXGLce8


## Issue Tracker

You can open issues and request features at https://github.com/glhrmfrts/q1compile/issues.


## Changes - v0.7 - (10/11/2021)

  * Open multiple config tabs.
  * Customize compilation steps and their order.
  * Option to auto-save when closing configs or exiting the app.
  * Save selected compiler options as a new preset.
  * Utility links to mapping tutorials, downloading tools and editor.
  * Reorganized UI more logically.
  * Bugfixes regarding loading/saving recent configs.


## Changes - v0.6 - (20/11/2020)

  * Remember whether sections of the UI were open or not.
  * Add option to compile the map on launch, instead of using the 'watch map file' option.
  * Add option to save current tools options as new preset.
  * Better logging for better error diagnostics.
  * Bugfix: Check worldspawn and brush entities' light related fields when doing map diff.
  * Bugfix: Don't compile if the 'Work Dir' is the same as the map source directory.


## Changes - v0.5 - (17/10/2020)

  * Add "auto apply -onlyents" option, to perform a map diff and re-compile only the necessary steps (experimental feature).
  * Add "use map mod" option to launch the game using the mod indicated in the map file (TB only).
  * Detect presence of .pts file and indicate in the GUI if map is leaking.
  * Copy the .pts file back to the source dir.
  * Add help markers and tooltips for the various options in the GUI.
  * Set the user's temp dir as default for Work Dir.
  * Add button to open paths in explorer.
  * Change the GUI font.
  * Add editor exe path and editor launch options.
  * If watching a map, compile on launch.
  * Fix the weird and ugly output from the compiler tools caused by "\r" character.


## Changes - v0.4.1 - (04/08/2020)

  * Link against DirectX 9 to support Windows 7.


## Changes - v0.4 - (01/08/2020)

  * Improved documentation.
  * Added ability to enable/disable quake engine output.
  * Changed location of user preferences to application directory.


## Changes - v0.3.1 - (03/07/2020)

  * Added application icon.
  * Using standard Windows file dialogs.


## Changes - v0.3 - (26/05/2020)

  * UI change.
  * Better logging during compiling.
  * If 'Run' command is issued while compilation is in progress, Quake will be run after that.
  * Bugfix: Check if .lit file was produced by light.exe instead of assuming it always does.


## Changes - v0.2.1 - (26/05/2020)

  * Bugfix: Reset file browser flags when selecting Engine and Map Source paths.


## Changes - v0.2 - (25/05/2020)

  * If the map source file changes while compiling, stop the compilation and start again.
  * Display recent configs in the menu bar.
  * Added compiler tools presets.
  * Requests confirmation if trying to create a new config while the current one is modified.


## Changes - v0.1.1 - (20/05/2020)

  * Bugfix: Remove .bsp extension in +map argument for engines.
  * Bugfix: Add .cfg extension when saving configs if no extension was provived.


### Credits

Dear ImGui - Omar Cornut - https://github.com/ocornut/imgui

ImGui Markdown - Juliette Foucaut & Doug Binks - https://github.com/juliettef/imgui_markdown

Droid Sans Mono font - Google Android

Guilherme Nemeth - guilherme.nemeth@gmail.com - @nemethg on Quake Mapping discord server


### License

q1compile is licensed under the MIT License, see LICENSE file for more information.