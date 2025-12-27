CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -g
INCLUDES := -Iinclude
SRC := main.c src/command.c src/parse.c src/execute.c src/shell.c
BUILD := build
OBJ := $(SRC:%.c=$(BUILD)/%.o)
TARGET := minibash

.PHONY: all clean rebuild

all: $(TARGET)

rebuild: clean all

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@

$(BUILD)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
	rm -rf $(BUILD)
