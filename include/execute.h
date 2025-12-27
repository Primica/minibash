#ifndef EXECUTE_H
#define EXECUTE_H

#include "command.h"

// Executes the parsed pipeline. Returns the exit status of the last command (like $?).
int execute_commands(Pipeline *pipeline);

#endif
