cpp-linenoise
=============

Multi-platform (Unix, Windows) C++ header-only linenoise-based readline library.

This version of cpp-linenoise is derived from https://github.com/yhirose/cpp-linenoise,
which is in turn assembled from the following libraries:

 * `linenoise.h` and `linenoise.c` ([antirez/linenoise](https://github.com/antirez/linenoise))
 * `ANSI.c` ([adoxa/ansicon](https://github.com/adoxa/ansicon))
 * `Win32_ANSI.h` and `Win32_ANSI.c` ([MSOpenTech/redis](https://github.com/MSOpenTech/redis))

The licenses for the libraries are included in `linenoise.hpp`.

Usage
-----

```c++
#include "linenoise.hpp"

...

const auto path = "history.txt";

linenoise::linenoiseState l("hello> ");

// Setup completion words every time when a user types
l.SetCompletionCallback([](const char* editBuffer, std::vector<std::string>& completions) {
    if (editBuffer[0] == 'h') {
        completions.push_back("hello");
        completions.push_back("hello there");
    }
});

// Enable the multi-line mode
l.EnableMultiLine();

// Set max length of the history
l.SetHistoryMaxLen(4);

// Load history
l.LoadHistory(path);

while (true) {
    // Read line
    std::string line;
    auto quit = l.Readline(line);

    if (quit) {
        break;
    }

    cout <<  "echo: '" << line << "'" << endl;

    // Add text to history
    l.AddHistory(line.c_str());
}

// Save history
l.SaveHistory(path);
```

API
---

The public methods on the linenoiseState class are considered the public API.

License
-------

BSD license (© 2015 Yuji Hirose)
