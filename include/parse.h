#ifndef PARSE_H
#define PARSE_H

#include "command.h"

// Parses the input line into a pipeline structure.
// Returns 1 on success, 0 for empty line, -1 on error (already reported).
int parse_line(char *line, Pipeline *pipeline);

#endif
