#ifndef COMMAND_H
#define COMMAND_H

#include <stddef.h>

#define MAX_ARGS 128
#define MAX_CMDS 16

typedef enum OutputType {
    OUTPUT_NONE,
    OUTPUT_PIPE,
    OUTPUT_REDIRECT,
    OUTPUT_REDIRECT_APPEND
} OutputType;

typedef struct Command {
    char *name;
    char **args;
    int argc;
    OutputType output_type;
    char *redirect_path;
    char *input_path;
    char *heredoc_delim;
} Command;

typedef struct Pipeline {
    Command cmds[MAX_CMDS];
    int count;
} Pipeline;

void init_command(Command *cmd);
void free_command(Command *cmd);
void init_pipeline(Pipeline *pipeline);
void free_pipeline(Pipeline *pipeline);

#endif
