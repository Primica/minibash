#include "execute.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int build_heredoc_fd(const char *delim) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return -1;
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    while (1) {
        fprintf(stdout, "heredoc> ");
        fflush(stdout);
        nread = getline(&line, &len, stdin);
        if (nread == -1) {
            break;
        }

        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
            if (strcmp(line, delim) == 0) {
                break;
            }
            line[nread - 1] = '\n';
        }

        if (write(pipefd[1], line, nread) == -1) {
            perror("write");
            free(line);
            close(pipefd[0]);
            close(pipefd[1]);
            return -1;
        }
    }

    free(line);
    close(pipefd[1]);
    return pipefd[0];
}

static void exec_with_redirects(Command *cmd, int in_fd, int out_fd) {
    if (cmd->heredoc_delim) {
        int fd = build_heredoc_fd(cmd->heredoc_delim);
        if (fd == -1) {
            exit(EXIT_FAILURE);
        }
        in_fd = fd;
    } else if (cmd->input_path) {
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

int execute_commands(Pipeline *pipeline) {
    if (pipeline->count <= 0) {
        return 0;
    }

    int pipes[MAX_CMDS - 1][2];
    for (int i = 0; i < pipeline->count - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            for (int k = 0; k < i; k++) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            return 127;
        }
    }

    pid_t pids[MAX_CMDS];
    int spawned = 0;
    int status_code = 0;

    for (int i = 0; i < pipeline->count; i++) {
        Command *cmd = &pipeline->cmds[i];
        int in_fd = (i > 0) ? pipes[i - 1][0] : STDIN_FILENO;
        int out_fd = (i < pipeline->count - 1) ? pipes[i][1] : STDOUT_FILENO;

        if (i < pipeline->count - 1 &&
            (cmd->output_type == OUTPUT_REDIRECT || cmd->output_type == OUTPUT_REDIRECT_APPEND)) {
            fprintf(stderr, "ignoring output redirection on non-final pipeline stage\n");
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            break;
        }

        if (pid == 0) {
            int keep_read = cmd->heredoc_delim ? -1 : (i - 1);
            int keep_write = (i < pipeline->count - 1) ? i : -1;

            for (int k = 0; k < pipeline->count - 1; k++) {
                if (k != keep_read) close(pipes[k][0]);
                if (k != keep_write) close(pipes[k][1]);
            }

            exec_with_redirects(cmd, in_fd, out_fd);
        }

        pids[spawned++] = pid;
    }

    for (int i = 0; i < pipeline->count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    for (int i = 0; i < spawned; i++) {
        int wstatus = 0;
        if (waitpid(pids[i], &wstatus, 0) == -1) {
            perror("waitpid");
            status_code = 127;
            continue;
        }
        if (i == spawned - 1) {
            if (WIFEXITED(wstatus)) {
                status_code = WEXITSTATUS(wstatus);
            } else if (WIFSIGNALED(wstatus)) {
                status_code = 128 + WTERMSIG(wstatus);
            }
        }
    }

    return status_code;
}
