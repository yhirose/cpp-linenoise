#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../linenoise.hpp"

using namespace linenoise_core;

void completion(const char* buf, linenoiseCompletions* lc)
{
    if (buf[0] == 'h') {
        linenoiseAddCompletion(lc, "hello");
        linenoiseAddCompletion(lc, "hello there");
    }
}

int main(int argc, const char** argv)
{
    const auto history_path = "history.txt";

    linenoiseSetCompletionCallback(completion);
    
    linenoiseHistorySetMaxLen(4);
    linenoiseHistoryLoad(history_path);

    char* line;
    while ((line = linenoise("hello> ")) != NULL) {
        printf("echo: '%s'\n", line);
        linenoiseHistoryAdd(line);
        linenoiseHistorySave(history_path);
        free(line);
    }

    return 0;
}
