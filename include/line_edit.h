#ifndef LINE_EDIT_H
#define LINE_EDIT_H

#include <stddef.h>

typedef struct LineEditor LineEditor;

LineEditor *line_editor_create(void);
void line_editor_destroy(LineEditor *ed);
// Returns length read, -1 on EOF or error. Allocates *out_line; caller frees.
int line_editor_read(LineEditor *ed, const char *prompt, char **out_line);

#endif
