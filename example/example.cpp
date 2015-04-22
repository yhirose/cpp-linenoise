#include <iostream>
#include "../linenoise.hpp"

using namespace std;

int main(int argc, const char** argv)
{
    const auto path = "history.txt";

    // Enable the multi-line mode
    linenoise::SetMultiLine(true);

    // Set max length of the history
    linenoise::SetHistoryMaxLen(4);

    // Setup completion words every time when a user types
    linenoise::SetCompletionCallback([](const char* editBuffer, std::vector<std::string>& completions) {
        if (editBuffer[0] == 'h') {
            completions.push_back("hello");
            completions.push_back("hello there");
        }
    });

    // Load history
    linenoise::LoadHistory(path);

    while (true) {
        // Read line
        auto line = linenoise::Readline("hello> ");

        if (line.empty()) {
            break;
        }

        cout <<  "echo: '" << line << "'" << endl;

        // Add line to history
        linenoise::AddHistory(line.c_str());

        // Save history
        linenoise::SaveHistory(path);
    }

    return 0;
}
