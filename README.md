# q1compile

A GUI to work with Quake 1 map compiling tools, see *q1compile_readme.txt* for more details.

### Building

You'll need cmake and a Visual Studio version supporting C++17.

First, fetch submodules:

```
$ git submodule init && git submodule update
```

Then run cmake (or the *configure.bat* file):

```
$ cmake . -G "Visual Studio 16 2019" -A x64 -B build
```

Open the visual studio project, build and run it!

### Contributing

Feel free to open an issue if you have a problem or feature request!

If you're a programmer also feel free to fork and open pull requests.

### License

MIT