#include "parse.h"

#include <stdio.h>
#include <string.h>

int parse_line(char *line, Pipeline *pipeline) {
    init_pipeline(pipeline);

    int current = 0;
    pipeline->count = 1;

    char *saveptr = NULL;
    char *token = strtok_r(line, " \n", &saveptr);
    while (token) {
        if (strcmp(token, "|") == 0) {
            if (pipeline->cmds[current].argc == 0) {
                fprintf(stderr, "pipeline missing command before pipe\n");
                return -1;
            }
            if (pipeline->count >= MAX_CMDS) {
                fprintf(stderr, "too many pipeline stages\n");
                return -1;
            }
            pipeline->cmds[current].output_type = OUTPUT_PIPE;
            current++;
            pipeline->count++;
            token = strtok_r(NULL, " \n", &saveptr);
            continue;
        }

        if (strcmp(token, ">") == 0 || strcmp(token, ">>") == 0) {
            int append = strcmp(token, ">>") == 0;
            char *file = strtok_r(NULL, " \n", &saveptr);
            if (!file) {
                fprintf(stderr, "missing filename for redirection\n");
                return -1;
            }
            pipeline->cmds[current].output_type = append ? OUTPUT_REDIRECT_APPEND : OUTPUT_REDIRECT;
            pipeline->cmds[current].redirect_path = file;
            token = strtok_r(NULL, " \n", &saveptr);
            continue;
        }

        if (strcmp(token, "<") == 0) {
            char *file = strtok_r(NULL, " \n", &saveptr);
            if (!file) {
                fprintf(stderr, "missing filename for input redirection\n");
                return -1;
            }
            pipeline->cmds[current].input_path = file;
            token = strtok_r(NULL, " \n", &saveptr);
            continue;
        }

        if (strcmp(token, "<<") == 0) {
            char *delim = strtok_r(NULL, " \n", &saveptr);
            if (!delim) {
                fprintf(stderr, "missing delimiter for heredoc\n");
                return -1;
            }
            pipeline->cmds[current].heredoc_delim = delim;
            token = strtok_r(NULL, " \n", &saveptr);
            continue;
        }

        if (pipeline->cmds[current].argc < MAX_ARGS - 1) {
            pipeline->cmds[current].args[pipeline->cmds[current].argc++] = token;
        }
        token = strtok_r(NULL, " \n", &saveptr);
    }

    if (pipeline->cmds[0].argc == 0) {
        return 0; // empty line
    }

    for (int i = 0; i < pipeline->count; i++) {
        pipeline->cmds[i].name = pipeline->cmds[i].args[0];
        pipeline->cmds[i].args[pipeline->cmds[i].argc] = NULL;
    }

    return 1;
}
