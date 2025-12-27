#ifndef EXECUTE_H
#define EXECUTE_H

#include "command.h"

// Executes the parsed pipeline. Assumes pipeline has been filled by parse_line.
void execute_commands(Pipeline *pipeline);

#endif
