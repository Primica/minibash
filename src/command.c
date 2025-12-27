#include "command.h"

#include <stdio.h>
#include <stdlib.h>

void init_command(Command *cmd) {
    cmd->args = calloc(MAX_ARGS, sizeof(char *));
    if (!cmd->args) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    cmd->argc = 0;
    cmd->name = NULL;
    cmd->redirect_path = NULL;
    cmd->input_path = NULL;
    cmd->heredoc_delim = NULL;
    cmd->output_type = OUTPUT_NONE;
}

void free_command(Command *cmd) {
    free(cmd->args);
    cmd->args = NULL;
    cmd->name = NULL;
    cmd->redirect_path = NULL;
    cmd->input_path = NULL;
    cmd->heredoc_delim = NULL;
    cmd->argc = 0;
    cmd->output_type = OUTPUT_NONE;
}

void init_pipeline(Pipeline *pipeline) {
    pipeline->count = 0;
    for (int i = 0; i < MAX_CMDS; i++) {
        init_command(&pipeline->cmds[i]);
    }
}

void free_pipeline(Pipeline *pipeline) {
    for (int i = 0; i < MAX_CMDS; i++) {
        free_command(&pipeline->cmds[i]);
    }
    pipeline->count = 0;
}
