#include "builtins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

static EnvironmentVars shell_vars = {0};
static Aliases shell_aliases = {0};

int builtins_init(void) {
    shell_vars.cap = 64;
    shell_vars.names = calloc(shell_vars.cap, sizeof(char *));
    shell_vars.values = calloc(shell_vars.cap, sizeof(char *));
    if (!shell_vars.names || !shell_vars.values) return -1;

    shell_aliases.cap = 64;
    shell_aliases.cmds = calloc(shell_aliases.cap, sizeof(char *));
    shell_aliases.aliases = calloc(shell_aliases.cap, sizeof(char *));
    if (!shell_aliases.cmds || !shell_aliases.aliases) return -1;

    return 0;
}

void builtins_cleanup(void) {
    for (int i = 0; i < shell_vars.count; i++) {
        free(shell_vars.names[i]);
        free(shell_vars.values[i]);
    }
    free(shell_vars.names);
    free(shell_vars.values);

    for (int i = 0; i < shell_aliases.count; i++) {
        free(shell_aliases.cmds[i]);
        free(shell_aliases.aliases[i]);
    }
    free(shell_aliases.cmds);
    free(shell_aliases.aliases);
}

int is_builtin(const char *cmd) {
    if (!cmd) return 0;
    return (strcmp(cmd, "cd") == 0 ||
            strcmp(cmd, "exit") == 0 ||
            strcmp(cmd, "pwd") == 0 ||
            strcmp(cmd, "export") == 0 ||
            strcmp(cmd, "set") == 0 ||
            strcmp(cmd, "unset") == 0 ||
            strcmp(cmd, "alias") == 0 ||
            strcmp(cmd, "unalias") == 0 ||
            strcmp(cmd, "echo") == 0);
}

const char *get_cwd(void) {
    static char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return ".";
    }
    return cwd;
}

const char *get_var(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < shell_vars.count; i++) {
        if (strcmp(shell_vars.names[i], name) == 0) {
            return shell_vars.values[i];
        }
    }
    // Try environment variable
    return getenv(name);
}

int set_var(const char *name, const char *value) {
    if (!name) return -1;

    // Check if already exists
    for (int i = 0; i < shell_vars.count; i++) {
        if (strcmp(shell_vars.names[i], name) == 0) {
            free(shell_vars.values[i]);
            shell_vars.values[i] = strdup(value ? value : "");
            return 0;
        }
    }

    // Add new variable
    if (shell_vars.count >= shell_vars.cap) {
        shell_vars.cap *= 2;
        char **tmp_names = realloc(shell_vars.names, shell_vars.cap * sizeof(char *));
        char **tmp_values = realloc(shell_vars.values, shell_vars.cap * sizeof(char *));
        if (!tmp_names || !tmp_values) return -1;
        shell_vars.names = tmp_names;
        shell_vars.values = tmp_values;
    }

    shell_vars.names[shell_vars.count] = strdup(name);
    shell_vars.values[shell_vars.count] = strdup(value ? value : "");
    if (!shell_vars.names[shell_vars.count] || !shell_vars.values[shell_vars.count]) {
        return -1;
    }
    shell_vars.count++;
    return 0;
}

int unset_var(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < shell_vars.count; i++) {
        if (strcmp(shell_vars.names[i], name) == 0) {
            free(shell_vars.names[i]);
            free(shell_vars.values[i]);
            // Shift remaining
            for (int j = i; j < shell_vars.count - 1; j++) {
                shell_vars.names[j] = shell_vars.names[j + 1];
                shell_vars.values[j] = shell_vars.values[j + 1];
            }
            shell_vars.count--;
            return 0;
        }
    }
    return -1;
}

int export_var(const char *name, const char *value) {
    if (!name) return -1;
    set_var(name, value);
    return setenv(name, value ? value : "", 1);
}

const char *get_alias(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < shell_aliases.count; i++) {
        if (strcmp(shell_aliases.cmds[i], name) == 0) {
            return shell_aliases.aliases[i];
        }
    }
    return NULL;
}

int set_alias(const char *name, const char *cmd) {
    if (!name || !cmd) return -1;

    for (int i = 0; i < shell_aliases.count; i++) {
        if (strcmp(shell_aliases.cmds[i], name) == 0) {
            free(shell_aliases.aliases[i]);
            shell_aliases.aliases[i] = strdup(cmd);
            return 0;
        }
    }

    if (shell_aliases.count >= shell_aliases.cap) {
        shell_aliases.cap *= 2;
        char **tmp_cmds = realloc(shell_aliases.cmds, shell_aliases.cap * sizeof(char *));
        char **tmp_aliases = realloc(shell_aliases.aliases, shell_aliases.cap * sizeof(char *));
        if (!tmp_cmds || !tmp_aliases) return -1;
        shell_aliases.cmds = tmp_cmds;
        shell_aliases.aliases = tmp_aliases;
    }

    shell_aliases.cmds[shell_aliases.count] = strdup(name);
    shell_aliases.aliases[shell_aliases.count] = strdup(cmd);
    if (!shell_aliases.cmds[shell_aliases.count] || !shell_aliases.aliases[shell_aliases.count]) {
        return -1;
    }
    shell_aliases.count++;
    return 0;
}

int unset_alias(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < shell_aliases.count; i++) {
        if (strcmp(shell_aliases.cmds[i], name) == 0) {
            free(shell_aliases.cmds[i]);
            free(shell_aliases.aliases[i]);
            for (int j = i; j < shell_aliases.count - 1; j++) {
                shell_aliases.cmds[j] = shell_aliases.cmds[j + 1];
                shell_aliases.aliases[j] = shell_aliases.aliases[j + 1];
            }
            shell_aliases.count--;
            return 0;
        }
    }
    return -1;
}

// Builtin: cd
static int builtin_cd(int argc, char **argv) {
    if (argc < 2) {
        const char *home = getenv("HOME");
        if (home && chdir(home) == 0) return 0;
        fprintf(stderr, "minibash: cd: home directory not set\n");
        return 1;
    }

    if (chdir(argv[1]) != 0) {
        fprintf(stderr, "minibash: cd: %s: No such file or directory\n", argv[1]);
        return 1;
    }
    return 0;
}

// Builtin: pwd
static int builtin_pwd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("%s\n", get_cwd());
    return 0;
}

// Builtin: exit
static int builtin_exit(int argc, char **argv) {
    int code = 0;
    if (argc > 1) {
        code = atoi(argv[1]);
    }
    exit(code);
    return code;
}

// Builtin: export
static int builtin_export(int argc, char **argv) {
    if (argc < 2) {
        // List all exported variables
        for (int i = 0; i < shell_vars.count; i++) {
            printf("export %s=%s\n", shell_vars.names[i], shell_vars.values[i]);
        }
        return 0;
    }

    char *eq = strchr(argv[1], '=');
    if (eq) {
        *eq = '\0';
        int ret = export_var(argv[1], eq + 1);
        *eq = '=';
        return ret;
    } else {
        return export_var(argv[1], get_var(argv[1]));
    }
}

// Builtin: set
static int builtin_set(int argc, char **argv) {
    if (argc < 2) {
        // List all variables
        for (int i = 0; i < shell_vars.count; i++) {
            printf("%s=%s\n", shell_vars.names[i], shell_vars.values[i]);
        }
        return 0;
    }

    char *eq = strchr(argv[1], '=');
    if (eq) {
        *eq = '\0';
        int ret = set_var(argv[1], eq + 1);
        *eq = '=';
        return ret;
    }
    return 0;
}

// Builtin: unset
static int builtin_unset(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "minibash: unset: usage: unset name\n");
        return 1;
    }
    return unset_var(argv[1]);
}

// Builtin: alias
static int builtin_alias(int argc, char **argv) {
    if (argc < 2) {
        // List all aliases
        for (int i = 0; i < shell_aliases.count; i++) {
            printf("alias %s='%s'\n", shell_aliases.cmds[i], shell_aliases.aliases[i]);
        }
        return 0;
    }

    char *eq = strchr(argv[1], '=');
    if (eq) {
        *eq = '\0';
        int ret = set_alias(argv[1], eq + 1);
        *eq = '=';
        return ret;
    }
    return 0;
}

// Builtin: unalias
static int builtin_unalias(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "minibash: unalias: usage: unalias name\n");
        return 1;
    }
    return unset_alias(argv[1]);
}

// Builtin: echo
static int builtin_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) printf(" ");
    }
    printf("\n");
    return 0;
}

int execute_builtin(const char *cmd, int argc, char **argv) {
    if (!cmd) return 1;

    if (strcmp(cmd, "cd") == 0) return builtin_cd(argc, argv);
    if (strcmp(cmd, "pwd") == 0) return builtin_pwd(argc, argv);
    if (strcmp(cmd, "exit") == 0) return builtin_exit(argc, argv);
    if (strcmp(cmd, "export") == 0) return builtin_export(argc, argv);
    if (strcmp(cmd, "set") == 0) return builtin_set(argc, argv);
    if (strcmp(cmd, "unset") == 0) return builtin_unset(argc, argv);
    if (strcmp(cmd, "alias") == 0) return builtin_alias(argc, argv);
    if (strcmp(cmd, "unalias") == 0) return builtin_unalias(argc, argv);
    if (strcmp(cmd, "echo") == 0) return builtin_echo(argc, argv);

    return 1;
}
