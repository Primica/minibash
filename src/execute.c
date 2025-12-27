#include "execute.h"
#include "builtins.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

int build_heredoc_fd(const char *delim) {
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

static int is_executable(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

static int path_lookup(const char *name) {
    char *path = getenv("PATH");
    if (!path) return -1;

    char *dup = strdup(path);
    if (!dup) return -1;

    char *saveptr = NULL;
    char *dir = strtok_r(dup, ":", &saveptr);
    while (dir) {
        char candidate[PATH_MAX];
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, name);
        if (is_executable(candidate)) {
            free(dup);
            return 0;
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }

    free(dup);
    return -1;
}

static int levenshtein(const char *a, const char *b) {
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    if (len_a > 128 || len_b > 128) return 999;

    int dp[129][129];
    for (size_t i = 0; i <= len_a; i++) dp[i][0] = (int)i;
    for (size_t j = 0; j <= len_b; j++) dp[0][j] = (int)j;
    for (size_t i = 1; i <= len_a; i++) {
        for (size_t j = 1; j <= len_b; j++) {
            int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            int del = dp[i - 1][j] + 1;
            int ins = dp[i][j - 1] + 1;
            int sub = dp[i - 1][j - 1] + cost;
            int best = del < ins ? del : ins;
            if (sub < best) best = sub;
            dp[i][j] = best;
        }
    }
    return dp[len_a][len_b];
}

static void find_suggestion(const char *name, char *suggestion, size_t size) {
    char *path = getenv("PATH");
    if (!path) return;

    char *dup = strdup(path);
    if (!dup) return;

    int best_score = 4; // only suggest if reasonably close
    char *saveptr = NULL;
    char *dir = strtok_r(dup, ":", &saveptr);
    while (dir) {
        DIR *d = opendir(dir);
        if (!d) {
            dir = strtok_r(NULL, ":", &saveptr);
            continue;
        }
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            int score = levenshtein(name, ent->d_name);
            if (score < best_score) {
                char candidate[PATH_MAX];
                snprintf(candidate, sizeof(candidate), "%s/%s", dir, ent->d_name);
                if (is_executable(candidate)) {
                    best_score = score;
                    snprintf(suggestion, size, "%s", ent->d_name);
                }
            }
        }
        closedir(d);
        dir = strtok_r(NULL, ":", &saveptr);
    }

    free(dup);
}

static int validate_commands(Pipeline *pipeline, char *suggestion, size_t sugg_size) {
    suggestion[0] = '\0';
    for (int i = 0; i < pipeline->count; i++) {
        Command *cmd = &pipeline->cmds[i];
        if (!cmd->name) return -1;

        if (strchr(cmd->name, '/')) {
            if (is_executable(cmd->name)) {
                continue;
            }
            snprintf(suggestion, sugg_size, "%s", cmd->name);
            return i;
        }

        if (path_lookup(cmd->name) == 0) {
            continue;
        }

        find_suggestion(cmd->name, suggestion, sugg_size);
        return i;
    }
    return -1;
}

int execute_commands(Pipeline *pipeline) {
    if (pipeline->count <= 0) {
        return 0;
    }

    // Handle builtins (only if no pipes)
    if (pipeline->count == 1) {
        Command *cmd = &pipeline->cmds[0];
        if (is_builtin(cmd->name)) {
            // Handle redirections for builtins
            int orig_stdin = -1, orig_stdout = -1;
            if (cmd->heredoc_delim) {
                orig_stdin = dup(STDIN_FILENO);
                int fd = build_heredoc_fd(cmd->heredoc_delim);
                if (fd == -1) return 1;
                dup2(fd, STDIN_FILENO);
                close(fd);
            } else if (cmd->input_path) {
                orig_stdin = dup(STDIN_FILENO);
                int fd = open(cmd->input_path, O_RDONLY);
                if (fd == -1) {
                    perror("open");
                    return 1;
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            if (cmd->output_type == OUTPUT_REDIRECT || cmd->output_type == OUTPUT_REDIRECT_APPEND) {
                orig_stdout = dup(STDOUT_FILENO);
                int flags = O_CREAT | O_WRONLY;
                flags |= (cmd->output_type == OUTPUT_REDIRECT_APPEND) ? O_APPEND : O_TRUNC;
                int fd = open(cmd->redirect_path, flags, 0644);
                if (fd == -1) {
                    perror("open");
                    if (orig_stdin >= 0) close(orig_stdin);
                    return 1;
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            int ret = execute_builtin(cmd->name, cmd->argc, cmd->args);

            if (orig_stdin >= 0) {
                dup2(orig_stdin, STDIN_FILENO);
                close(orig_stdin);
            }
            if (orig_stdout >= 0) {
                dup2(orig_stdout, STDOUT_FILENO);
                close(orig_stdout);
            }

            return ret;
        }
    }

    char suggestion[128];
    int bad_idx = validate_commands(pipeline, suggestion, sizeof(suggestion));
    if (bad_idx >= 0) {
        const char *name = pipeline->cmds[bad_idx].name;
        fprintf(stderr, "minibash: command not found: %s", name);
        if (suggestion[0] && strcmp(suggestion, name) != 0) {
            fprintf(stderr, " (did you mean '%s'?)", suggestion);
        }
        fprintf(stderr, "\n");
        return 127;
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
