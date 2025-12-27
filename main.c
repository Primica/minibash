#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>

typedef struct Command {
    char *name;
    char **args;
    int argc;
    enum { OUTPUT_NONE, OUTPUT_PIPE, OUTPUT_REDIRECT, OUTPUT_REDIRECT_APPEND } output_type;
    char *redirect_path;
    char *input_path;
} Command;

static void free_command(Command *cmd) {
    free(cmd->args);
    cmd->args = NULL;
    cmd->name = NULL;
    cmd->redirect_path = NULL;
    cmd->input_path = NULL;
    cmd->argc = 0;
}

static void init_command(Command *cmd, int max_args) {
    cmd->args = calloc(max_args, sizeof(char *));
    cmd->argc = 0;
    cmd->name = NULL;
    cmd->redirect_path = NULL;
    cmd->input_path = NULL;
    cmd->output_type = OUTPUT_NONE;
}

static int parse_line(char *line, Command *cmds, int *cmd_count) {
    const int max_args = 128;
    const int max_cmds = 16;
    *cmd_count = 0;

    for (int i = 0; i < max_cmds; i++) {
        init_command(&cmds[i], max_args);
    }

    int current = 0;
    *cmd_count = 1;
    char *saveptr;
    char *token = strtok_r(line, " \n", &saveptr);
    while (token) {
        if (strcmp(token, "|") == 0) {
            if (cmds[current].argc == 0) {
                fprintf(stderr, "pipeline missing command before pipe\n");
                return -1;
            }
            if (*cmd_count >= max_cmds) {
                fprintf(stderr, "too many pipeline stages\n");
                return -1;
            }
            cmds[current].output_type = OUTPUT_PIPE;
            current++;
            (*cmd_count)++;
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
            cmds[current].output_type = append ? OUTPUT_REDIRECT_APPEND : OUTPUT_REDIRECT;
            cmds[current].redirect_path = file;
            token = strtok_r(NULL, " \n", &saveptr);
            continue;
        }

        if (strcmp(token, "<") == 0) {
            char *file = strtok_r(NULL, " \n", &saveptr);
            if (!file) {
                fprintf(stderr, "missing filename for input redirection\n");
                return -1;
            }
            cmds[current].input_path = file;
            token = strtok_r(NULL, " \n", &saveptr);
            continue;
        }

        if (cmds[current].argc < max_args - 1) {
            cmds[current].args[cmds[current].argc++] = token;
        }
        token = strtok_r(NULL, " \n", &saveptr);
    }

    if (cmds[0].argc == 0) {
        return 0; // empty line
    }

    for (int i = 0; i < *cmd_count; i++) {
        cmds[i].name = cmds[i].args[0];
        cmds[i].args[cmds[i].argc] = NULL;
    }

    return 1;
}

static void exec_with_redirects(Command *cmd, int in_fd, int out_fd) {
    if (cmd->input_path) {
        int fd = open(cmd->input_path, O_RDONLY);
        if (fd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }
        in_fd = fd;
    }

    if (in_fd != STDIN_FILENO) {
        if (dup2(in_fd, STDIN_FILENO) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        close(in_fd);
    }

    if (cmd->output_type == OUTPUT_REDIRECT || cmd->output_type == OUTPUT_REDIRECT_APPEND) {
        int flags = O_CREAT | O_WRONLY;
        flags |= (cmd->output_type == OUTPUT_REDIRECT_APPEND) ? O_APPEND : O_TRUNC;
        int fd = open(cmd->redirect_path, flags, 0644);
        if (fd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }
        out_fd = fd;
    }

    if (out_fd != STDOUT_FILENO) {
        if (dup2(out_fd, STDOUT_FILENO) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        close(out_fd);
    }

    execvp(cmd->name, cmd->args);
    perror("execvp");
    exit(EXIT_FAILURE);
}

static void execute_commands(Command *cmds, int cmd_count) {
    if (cmd_count == 1 && cmds[0].output_type == OUTPUT_NONE && cmds[0].input_path == NULL) {
        // simple case but still fork to keep shell alive
    }

    int pipes[16][2];
    for (int i = 0; i < cmd_count - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            return;
        }
    }

    pid_t pids[16];

    for (int i = 0; i < cmd_count; i++) {
        int in_fd = STDIN_FILENO;
        int out_fd = STDOUT_FILENO;

        if (i > 0) {
            in_fd = pipes[i - 1][0];
        }
        if (i < cmd_count - 1) {
            out_fd = pipes[i][1];
            if (cmds[i].output_type == OUTPUT_REDIRECT || cmds[i].output_type == OUTPUT_REDIRECT_APPEND) {
                fprintf(stderr, "ignoring output redirection on non-final pipeline stage\n");
            }
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            // Close pipes opened so far
            for (int k = 0; k < cmd_count - 1; k++) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            return;
        }

        if (pid == 0) {
            // Child: close unused pipe ends
            for (int k = 0; k < cmd_count - 1; k++) {
                if (k != i - 1) close(pipes[k][0]);
                if (k != i) close(pipes[k][1]);
            }
            exec_with_redirects(&cmds[i], in_fd, out_fd);
        }

        pids[i] = pid;
    }

    for (int i = 0; i < cmd_count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    for (int i = 0; i < cmd_count; i++) {
        if (waitpid(pids[i], NULL, 0) == -1) {
            perror("waitpid");
        }
    }
}

void shell_loop() {
    // We stay in the loop until the user exits, if there is an error we print the error and continue, same if 2 
    while (1) {
        char *line = NULL;
        size_t len = 0;
        printf("shell> ");
        if (getline(&line, &len, stdin) == -1) {
            free(line);
            break; // Exit on EOF
        }
        Command cmds[16] = {0};
        int cmd_count = 0;

        int parse_status = parse_line(line, cmds, &cmd_count);
        if (parse_status <= 0) {
            for (int i = 0; i < 16; i++) free_command(&cmds[i]);
            free(line);
            if (parse_status < 0) {
                continue;
            }
            continue; // Empty input
        }

        execute_commands(cmds, cmd_count);

        for (int i = 0; i < 16; i++) free_command(&cmds[i]);
        free(line);
    }
}

int main() {
    shell_loop();
    return 0;
}
