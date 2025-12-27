#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "execute.h"
#include "parse.h"

static int run_capture(const char *cmd, char *out, size_t out_size) {
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return -1;
    }

    if (!fgets(out, out_size, fp)) {
        pclose(fp);
        return -1;
    }

    pclose(fp);
    size_t len = strcspn(out, "\n");
    out[len] = '\0';
    return 0;
}

static int git_branch(char *branch, size_t size) {
    char probe[8];
    if (run_capture("git rev-parse --is-inside-work-tree 2>/dev/null", probe, sizeof(probe)) != 0) {
        return -1;
    }
    if (strcmp(probe, "true") != 0) {
        return -1;
    }

    if (run_capture("git symbolic-ref --short HEAD 2>/dev/null", branch, size) == 0) {
        return 0;
    }
    if (run_capture("git rev-parse --short HEAD 2>/dev/null", branch, size) == 0) {
        return 0;
    }
    return -1;
}

static void build_prompt(char *prompt, size_t size) {
    char cwd[PATH_MAX] = {0};
    const char *folder = "?";
    if (getcwd(cwd, sizeof(cwd))) {
        const char *last_slash = strrchr(cwd, '/');
        if (last_slash && *(last_slash + 1) != '\0') {
            folder = last_slash + 1;
        } else {
            folder = cwd;
        }
    }

    char branch[128];
    if (git_branch(branch, sizeof(branch)) == 0) {
        snprintf(prompt, size, "minibash [%s] (git:%s) $ ", folder, branch);
    } else {
        snprintf(prompt, size, "minibash [%s] $ ", folder);
    }
}

void shell_loop(void) {
    while (1) {
        char *line = NULL;
        size_t len = 0;
        char prompt[256];
        build_prompt(prompt, sizeof(prompt));
        fputs(prompt, stdout);
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
