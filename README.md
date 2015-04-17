cpp-linenoise
=============

Multi-platfrom (Unix, Windows) C++ header-only linenoise library.

This library just gathered code from following excellent libraries, clean it up, and put it into a C++ header file for convenience.

 * `linenoise.h` and `linenose.c` ([antirez/linenoise](https://github.com/antirez/linenoise))
 * `ANSI.c` ([adoxa/ansicon](https://github.com/adoxa/ansicon))
 * `Win32_ANSI.h` and `Win32_ANSI.c` ([MSOpenTech/redis](https://github.com/MSOpenTech/redis))

The licenses for the libraries are included in `linenoise.hpp`.

Usage
-----

  1. Include `linenoise.hpp` in your project.

See the [example.cpp]() to see how to use the library.

License
-------

BSD license (© 2015 Yuji Hirose)
