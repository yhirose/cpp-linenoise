#include <iostream>
#include "../linenoise.hpp"

using namespace std;

int main(int argc, const char** argv)
{
    const auto path = "history.txt";

#ifdef _WIN32
    const char *prompt = "hello> ";
#else
    const char *prompt = "\033[32mこんにちは\x1b[0m> ";
#endif
    linenoise::linenoiseState l(prompt);

    // Enable the multi-line mode
    l.EnableMultiLine(true);

    // Set max length of the history
    l.SetHistoryMaxLen(4);

    // Setup completion words every time when a user types
    l.SetCompletionCallback([](const char* editBuffer, std::vector<std::string>& completions) {
        if (editBuffer[0] == 'h') {
#ifdef _WIN32
            completions.push_back("hello こんにちは");
            completions.push_back("hello こんにちは there");
#else
            completions.push_back("hello");
            completions.push_back("hello there");
#endif
        }
    });

    // Load history
    l.LoadHistory(path);

    while (true) {
        std::string line;
        auto quit = l.Readline(line);

        if (quit) {
            break;
        }

        cout <<  "echo: '" << line << "'" << endl;

        // Add line to history
        l.AddHistory(line.c_str());

        // Save history
        l.SaveHistory(path);
    }

    return 0;
}
