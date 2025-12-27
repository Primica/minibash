#include "shell.h"

#include <stdio.h>
#include <stdlib.h>

#include "execute.h"
#include "parse.h"

void shell_loop(void) {
    while (1) {
        char *line = NULL;
        size_t len = 0;
        printf("shell> ");
        if (getline(&line, &len, stdin) == -1) {
            free(line);
            break;
        }

        Pipeline pipeline;
        int parse_status = parse_line(line, &pipeline);
        if (parse_status <= 0) {
            free_pipeline(&pipeline);
            free(line);
            if (parse_status < 0) {
                continue;
            }
            continue;
        }

        execute_commands(&pipeline);

        free_pipeline(&pipeline);
        free(line);
    }
}
