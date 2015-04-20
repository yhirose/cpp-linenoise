#include <iostream>
#include "../linenoise.hpp"

using namespace linenoise_core;
using namespace std;

int main(int argc, const char** argv)
{
    const auto history_path = "history.txt";

    linenoiseSetCompletionCallback([](const char* buf, linenoiseCompletions& lc) {
        if (buf[0] == 'h') {
            lc.push_back("hello");
            lc.push_back("hello there");
        }
    });
    
    linenoiseSetMultiLine(true);

    linenoiseHistorySetMaxLen(4);
    linenoiseHistoryLoad(history_path);

    std::string line;
    while (!(line = linenoise("hello> ")).empty()) {
        cout <<  "echo: '" << line << "'" << endl;
        linenoiseHistoryAdd(line.c_str());
        linenoiseHistorySave(history_path);
    }

    return 0;
}
