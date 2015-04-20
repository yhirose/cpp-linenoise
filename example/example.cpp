#include <iostream>
#include "../linenoise.hpp"

using namespace std;

int main(int argc, const char** argv)
{
    const auto history_path = "history.txt";

    linenoise::SetCompletionCallback([](const char* editBuffer, std::vector<std::string>& completions) {
        if (editBuffer[0] == 'h') {
            completions.push_back("hello");
            completions.push_back("hello there");
        }
    });
    
    linenoise::SetMultiLine(true);

    linenoise::SetHistoryMaxLen(4);
    linenoise::LoadHistory(history_path);

    std::string line;
    while (!(line = linenoise::Readline("hello> ")).empty()) {
        cout <<  "echo: '" << line << "'" << endl;
        linenoise::AddHistory(line.c_str());
        linenoise::SaveHistory(history_path);
    }

    return 0;
}
