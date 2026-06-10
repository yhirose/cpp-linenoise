cpp-linenoise
=============

[![test](https://github.com/yhirose/cpp-linenoise/actions/workflows/test.yml/badge.svg)](https://github.com/yhirose/cpp-linenoise/actions/workflows/test.yml)

Multi-platform (Unix, Windows) C++17 header-only readline library, derived
from [antirez/linenoise](https://github.com/antirez/linenoise).

Features
--------

 * Single header file, no dependencies. Just include `linenoise.hpp`.
 * Full Unicode support:
   - Extended grapheme cluster segmentation ([UAX #29]) — cursor movement,
     backspace and delete treat emoji ZWJ sequences (👩‍👩‍👧‍👦), flags (🇯🇵),
     combining marks, Hangul jamo and Indic conjuncts as single characters.
   - Correct display widths ([UAX #11] East Asian Width + emoji), so CJK
     text and emoji render and scroll correctly in single and multi line
     mode. Generated from Unicode 17.0 data.
 * Works in Windows Terminal and the Windows 10+ console through the native
   virtual terminal mode (no ANSI emulation layer; on older consoles it
   degrades to plain line input).
 * Everything the original linenoise does: history, tab completion, hints,
   multi-line editing, mask (password) mode, and bracketed paste with
   display folding for large or multi-line pastes.
 * Multi-line composition for chat-style CLIs: insert newlines with
   Alt+Enter, trailing `\` or trailing space + Enter, or a raw LF from a
   terminal keybind; continuation prompt; Up/Down move between lines.
 * Reverse incremental history search (Ctrl-R), word-wise navigation
   (Alt+B/F, Ctrl+arrows), placeholder text, and a non-blocking API for
   event-driven programs that print output while the user types.

[UAX #29]: https://unicode.org/reports/tr29/
[UAX #11]: https://unicode.org/reports/tr11/

Relationship to linenoise
-------------------------

This is a derived implementation, not a wrapper: the editing engine (key
handling, single/multi line refresh, history, completion, hints, paste
folding) is a function-by-function C++ port of upstream linenoise that is
meant to track it (last synced with commit `a473823`, 2026-05-02), while
two layers are original to this project:

 * The Unicode layer replaces upstream's heuristic UTF-8 handling with
   full UAX #29 grapheme cluster segmentation and UCD-generated width
   tables, validated against the official `GraphemeBreakTest.txt`.
 * The platform layer abstracts terminal I/O so the same engine runs on
   POSIX termios and the Windows console.

Deliberate behavior differences from upstream: Ctrl-C removes the
temporary history entry instead of leaking it, and a Regional Indicator
pair (flag emoji) counts as 2 columns rather than 4. The multi-line
composition features (newline conventions, continuation prompt,
line-aware Up/Down), reverse incremental search, word-wise
movement/deletion, and the placeholder are cpp-linenoise extensions not
present upstream.

Usage
-----

```cpp
#include "linenoise.hpp"

...

const auto path = "history.txt";

// Setup completion words every time when a user types
linenoise::SetCompletionCallback(
    [](const char* editBuffer, std::vector<std::string>& completions) {
        if (editBuffer[0] == 'h') {
            completions.push_back("hello");
            completions.push_back("hello there");
        }
    });

// Show a hint at the right of the prompt while typing
linenoise::SetHintsCallback(
    [](const char* editBuffer, int& color, bool& bold) -> std::string {
        if (std::string(editBuffer) == "git remote add") {
            color = 35;
            return " <name> <url>";
        }
        return {};
    });

// Enable the multi-line mode
linenoise::SetMultiLine(true);

// Set max length of the history
linenoise::SetHistoryMaxLen(100);

// Load history
linenoise::LoadHistory(path);

while (true) {
    // Read line
    std::string line;
    auto quit = linenoise::Readline("hello> ", line);

    if (quit) {
        break;
    }

    std::cout << "echo: '" << line << "'" << std::endl;

    // Add text to history
    linenoise::AddHistory(line.c_str());
}

// Save history
linenoise::SaveHistory(path);
```

API
---

```cpp
namespace linenoise;

// Line editing. Returns true when the user wants to quit
// (Ctrl-C, or Ctrl-D on an empty line).
bool Readline(const char* prompt, std::string& line);
std::string Readline(const char* prompt, bool& quit);
std::string Readline(const char* prompt);

// Multi-line mode (default: single line)
void SetMultiLine(bool multiLineMode);

// Multi-line composition: choose which conventions insert a newline
// instead of submitting (plain Enter always submits).
// Default: NEWLINE_ALT_ENTER | NEWLINE_LF.
enum NewlineConvention {
    NEWLINE_ALT_ENTER,       // ESC + CR (Alt+Enter on most terminals)
    NEWLINE_BACKSLASH_ENTER, // trailing '\' + Enter (the '\' is removed)
    NEWLINE_SPACE_ENTER,     // trailing space + Enter
    NEWLINE_LF,              // raw LF: Ctrl-J, or a terminal keybind such
                             // as Ghostty's "shift+enter=text:\n"
};
void SetNewlineConventions(int mask);

// Prompt shown before the continuation lines of a multi-line buffer
void SetContinuationPrompt(const char* prompt);

// Dim placeholder text shown after the prompt while the input is empty
void SetPlaceholder(const char* text);

// Mask mode: echo '*' instead of the typed characters (for passwords)
void EnableMaskMode();
void DisableMaskMode();

// Completion
using CompletionCallback =
    std::function<void(const char* editBuffer, std::vector<std::string>& completions)>;
void SetCompletionCallback(CompletionCallback fn);

// Hints shown at the right of the cursor. Return an empty string for no
// hint; 'color' is an ANSI color code (e.g. 35 = magenta).
using HintsCallback =
    std::function<std::string(const char* editBuffer, int& color, bool& bold)>;
void SetHintsCallback(HintsCallback fn);

// History
bool SetHistoryMaxLen(size_t len);
bool LoadHistory(const char* path);
bool SaveHistory(const char* path);
bool AddHistory(const char* line);
const std::vector<std::string>& GetHistory();

// Misc
void ClearScreen();
void HideLine(); // temporarily hide the edited line (to print async output)
void ShowLine(); // show it again
```

### Non-blocking API

For event-driven programs (e.g. printing streamed output while the user
types), drive an editing session manually. Put the input descriptor in
non-blocking mode and call `EditFeed` whenever it is readable:

```cpp
linenoise::EditState st;
linenoise::EditStart(st, "> ");
for (;;) {
    // ... wait with poll()/select() ...
    std::string line;
    auto s = linenoise::EditFeed(st, line);
    if (s == linenoise::EditStatus::More) continue;
    linenoise::EditStop(st);
    if (s == linenoise::EditStatus::Done) { /* use line */ }
    break;
}
// From elsewhere in the event loop:
//   linenoise::EditHide(st);  print asynchronous output;  linenoise::EditShow(st);
```

Key bindings
------------

| Key                  | Action                                        |
| -------------------- | --------------------------------------------- |
| Left, Right          | Move cursor by one grapheme cluster           |
| Up, Down             | Move between lines of a multi-line buffer; history navigation at the first/last line |
| Ctrl-P, Ctrl-N       | History navigation                            |
| Ctrl-R               | Reverse incremental history search (Ctrl-G aborts) |
| Alt-B, Alt-F / Ctrl-Left, Ctrl-Right | Move by word                  |
| Alt-D, Alt-Backspace | Delete next / previous word                   |
| Home, End / Ctrl-A, E| Start / end of line                           |
| Backspace, Delete    | Delete one grapheme cluster                   |
| Tab                  | Cycle completions (Esc cancels)               |
| Ctrl-B, F            | Move left / right                             |
| Ctrl-T               | Swap the two clusters around the cursor       |
| Ctrl-U               | Delete the whole line                         |
| Ctrl-K               | Delete to end of line                         |
| Ctrl-W               | Delete previous word                          |
| Ctrl-L               | Clear screen                                  |
| Ctrl-C               | Cancel (Readline returns quit = true)         |
| Ctrl-D               | Delete right, or EOF on an empty line         |

Pasting multi-line or very large text shows a folded `[... N pasted lines
...]` marker while keeping the real bytes in the line buffer (requires a
terminal with bracketed paste, such as Windows Terminal, iTerm2, or any
modern Unix terminal).

Platform notes
--------------

- **Unix/macOS**: any VT100-compatible terminal. `TERM=dumb` and friends
  fall back to plain line input.
- **Windows**: requires the virtual terminal support of Windows 10 or
  later (Windows Terminal, conhost, ConEmu, etc.). Input is read with
  `ReadConsoleW`, so Unicode input works regardless of the console code
  page. On consoles without VT support the library falls back to plain
  line input.

Building
--------

`linenoise.hpp` is self-contained — copy it into your project and include
it. CMake users can also use the interface target:

```cmake
add_subdirectory(cpp-linenoise)
target_link_libraries(your_app PRIVATE linenoise)
```

C++17 or later is required.

Regenerating the Unicode tables
-------------------------------

The grapheme cluster / width tables embedded in `linenoise.hpp` are
generated from the Unicode Character Database:

```bash
python3 scripts/gen_unicode_tables.py --update linenoise.hpp \
    --test-out test/grapheme_break_test.inc
```

Tests
-----

```bash
cmake -B build
cmake --build build
ctest --test-dir build
```

The test suite includes all official Unicode `GraphemeBreakTest.txt` cases
and drives the editing engine in-process through pipes.

License
-------

BSD license (© 2015-2026 Yuji Hirose; original linenoise © 2010-2023
Salvatore Sanfilippo and Pieter Noordhuis). See `linenoise.hpp` for the
full text.
