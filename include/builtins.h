#ifndef BUILTINS_H
#define BUILTINS_H

#include "command.h"

typedef struct {
    char **names;
    char **values;
    int count;
    int cap;
} EnvironmentVars;

typedef struct {
    char **cmds;
    char **aliases;
    int count;
    int cap;
} Aliases;

// Initialize builtins system
int builtins_init(void);

// Check if a command is a builtin
int is_builtin(const char *cmd);

// Execute a builtin command, returns exit code
int execute_builtin(const char *cmd, int argc, char **argv);

// Get current working directory
const char *get_cwd(void);

// Variable management
const char *get_var(const char *name);
int set_var(const char *name, const char *value);
int unset_var(const char *name);

// Alias management
const char *get_alias(const char *name);
int set_alias(const char *name, const char *cmd);
int unset_alias(const char *name);

// Export variable to environment
int export_var(const char *name, const char *value);

// Cleanup
void builtins_cleanup(void);

#endif
