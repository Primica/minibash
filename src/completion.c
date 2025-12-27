#include "completion.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static int is_executable(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

static void add_match(Completion *c, const char *match) {
    if (c->count == 0) {
        c->matches = malloc(16 * sizeof(char *));
    } else if ((c->count % 16) == 0) {
        c->matches = realloc(c->matches, (c->count + 16) * sizeof(char *));
    }
    if (!c->matches) return;
    c->matches[c->count++] = strdup(match);
}

static void complete_from_path(Completion *c, const char *prefix) {
    char *path = getenv("PATH");
    if (!path) return;

    char *dup = strdup(path);
    if (!dup) return;

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
            if (strncmp(ent->d_name, prefix, strlen(prefix)) == 0) {
                char candidate[512];
                snprintf(candidate, sizeof(candidate), "%s/%s", dir, ent->d_name);
                if (is_executable(candidate)) {
                    int exists = 0;
                    for (int i = 0; i < c->count; i++) {
                        if (strcmp(c->matches[i], ent->d_name) == 0) {
                            exists = 1;
                            break;
                        }
                    }
                    if (!exists) {
                        add_match(c, ent->d_name);
                    }
                }
            }
        }
        closedir(d);
        dir = strtok_r(NULL, ":", &saveptr);
    }

    free(dup);
}

static void complete_from_fs(Completion *c, const char *prefix) {
    const char *sep = strrchr(prefix, '/');
    const char *dir_part = ".";
    const char *file_part = prefix;

    char dir_buf[512] = {0};
    if (sep) {
        int dir_len = (int)(sep - prefix);
        if (dir_len == 0) {
            dir_part = "/";
        } else {
            strncpy(dir_buf, prefix, (size_t)dir_len);
            dir_part = dir_buf;
        }
        file_part = sep + 1;
    }

    DIR *d = opendir(dir_part);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (strncmp(ent->d_name, file_part, strlen(file_part)) != 0) continue;

        char candidate[512];
        snprintf(candidate, sizeof(candidate), "%s/%s", dir_part, ent->d_name);
        
        struct stat st;
        if (stat(candidate, &st) == 0) {
            char match[512];
            if (sep) {
                snprintf(match, sizeof(match), "%s/%s", dir_part, ent->d_name);
            } else {
                snprintf(match, sizeof(match), "%s", ent->d_name);
            }
            if (S_ISDIR(st.st_mode)) {
                char match_dir[512];
                snprintf(match_dir, sizeof(match_dir), "%s/", match);
                add_match(c, match_dir);
            } else {
                add_match(c, match);
            }
        }
    }
    closedir(d);
}

Completion *completion_find(const char *prefix) {
    Completion *c = calloc(1, sizeof(Completion));
    if (!c) return NULL;

    if (!prefix || !*prefix) {
        return c;
    }

    if (strchr(prefix, '/')) {
        complete_from_fs(c, prefix);
    } else {
        complete_from_path(c, prefix);
        complete_from_fs(c, prefix);
    }

    return c;
}

void completion_free(Completion *c) {
    if (!c) return;
    for (int i = 0; i < c->count; i++) {
        free(c->matches[i]);
    }
    free(c->matches);
    free(c);
}
