CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -g
INCLUDES := -Iinclude
LDFLAGS :=
SRC := main.c src/command.c src/parse.c src/execute.c src/shell.c src/line_edit.c src/completion.c src/builtins.c
BUILD := build
OBJ := $(SRC:%.c=$(BUILD)/%.o)
TARGET := minibash

.PHONY: all clean rebuild

all: $(TARGET)

rebuild: clean all

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
	rm -rf $(BUILD)
