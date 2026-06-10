#include <iostream>

#include "../linenoise.hpp"

int main() {
    const auto path = "history.txt";

    // Enable the multi-line mode
    linenoise::SetMultiLine(true);

    // Set max length of the history
    linenoise::SetHistoryMaxLen(8);

    // Setup completion words every time when a user types
    linenoise::SetCompletionCallback(
        [](const char* editBuffer, std::vector<std::string>& completions) {
            if (editBuffer[0] == 'h') {
                completions.push_back("hello こんにちは");
                completions.push_back("hello こんにちは there 👋");
            }
        });

    // Show a hint at the right of the prompt while typing
    linenoise::SetHintsCallback(
        [](const char* editBuffer, int& color, bool& bold) -> std::string {
            if (std::string(editBuffer) == "hello") {
                color = 35; // magenta
                bold = false;
                return " こんにちは";
            }
            return {};
        });

    // Load history
    linenoise::LoadHistory(path);

    while (true) {
        std::string line;
        auto quit = linenoise::Readline("\x1b[32mこんにちは\x1b[0m> ", line);

        if (quit) {
            break;
        }

        std::cout << "echo: '" << line << "'" << std::endl;

        // Add text to history
        linenoise::AddHistory(line.c_str());

        // Save history
        linenoise::SaveHistory(path);
    }

    return 0;
}
