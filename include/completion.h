#ifndef COMPLETION_H
#define COMPLETION_H

typedef struct Completion {
    char **matches;
    int count;
} Completion;

Completion *completion_find(const char *prefix);
void completion_free(Completion *c);

#endif
