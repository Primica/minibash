#include "line_edit.h"
#include "completion.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <ncurses.h>

struct LineEditor {
    struct termios orig;
    int raw_enabled;
    char **history;
    int hist_count;
    int hist_cap;
};

static int enable_raw(LineEditor *ed) {
    if (ed->raw_enabled) return 0;
    if (!isatty(STDIN_FILENO)) return -1;

    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &ed->orig) == -1) return -1;
    raw = ed->orig;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL | INPCK | ISTRIP | BRKINT);
    raw.c_cflag |= CS8;
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) return -1;
    ed->raw_enabled = 1;
    return 0;
}

static void disable_raw(LineEditor *ed) {
    if (!ed->raw_enabled) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &ed->orig);
    ed->raw_enabled = 0;
}

LineEditor *line_editor_create(void) {
    LineEditor *ed = calloc(1, sizeof(LineEditor));
    if (!ed) return NULL;
    ed->hist_cap = 128;
    ed->history = calloc(ed->hist_cap, sizeof(char *));
    if (!ed->history) {
        free(ed);
        return NULL;
    }
    return ed;
}

void line_editor_destroy(LineEditor *ed) {
    if (!ed) return;
    disable_raw(ed);
    for (int i = 0; i < ed->hist_count; i++) {
        free(ed->history[i]);
    }
    free(ed->history);
    free(ed);
}

static void history_add(LineEditor *ed, const char *line) {
    if (!line || !*line) return;
    if (ed->hist_count == ed->hist_cap) return;
    ed->history[ed->hist_count++] = strdup(line);
}

static const char *history_get(LineEditor *ed, int index) {
    if (index < 0 || index >= ed->hist_count) return NULL;
    return ed->history[index];
}

static int prompt_display_len(const char *prompt) {
    int len = 0;
    for (const char *p = prompt; *p; p++) {
        if (*p == '\033') {
            while (*p && *p != 'm') {
                p++;
            }
            continue;
        }
        len++;
    }
    return len;
}

static void line_refresh(const char *prompt, const char *buf, int len, int pos) {
    fputs("\r", stdout);
    fputs(prompt, stdout);
    fwrite(buf, 1, (size_t)len, stdout);
    fputs("\033[K", stdout);
    int prompt_len = prompt_display_len(prompt);
    int target = prompt_len + pos;
    int current = prompt_len + len;
    if (current > target) {
        char seq[32];
        snprintf(seq, sizeof(seq), "\033[%dD", current - target);
        fputs(seq, stdout);
    }
    fflush(stdout);
}

static int read_key(void) {
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;
    return c;
}

static void print_completions_table(Completion *comp) {
    if (!comp || comp->count == 0) return;

    // Initialize ncurses for table display
    int max_x, max_y;
    getmaxyx(stdscr, max_y, max_x);
    (void)max_y;  // suppress unused warning

    // Calculate column width
    int max_width = 0;
    for (int i = 0; i < comp->count; i++) {
        int len = (int)strlen(comp->matches[i]);
        if (len > max_width) max_width = len;
    }
    max_width += 2;
    if (max_width > max_x / 2) max_width = max_x / 2;

    int cols = (max_x - 1) / max_width;
    if (cols < 1) cols = 1;

    int row = 0;
    int col = 0;
    for (int i = 0; i < comp->count; i++) {
        const char *match = comp->matches[i];
        
        if (col == 0) {
            move(row, 0);
        }
        printw("%-*s", max_width, match);
        col++;

        if (col >= cols) {
            row++;
            col = 0;
        }
    }
    
    refresh();
}

int line_editor_read(LineEditor *ed, const char *prompt, char **out_line) {
    *out_line = NULL;
    if (!ed || !prompt) return -1;

    int is_tty = isatty(STDIN_FILENO);
    if (!is_tty) {
        char *line = NULL;
        size_t n = 0;
        fputs(prompt, stdout);
        fflush(stdout);
        if (getline(&line, &n, stdin) == -1) {
            free(line);
            return -1;
        }
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n') line[len - 1] = '\0';
        *out_line = line;
        history_add(ed, line);
        return (int)strlen(line);
    }

    if (enable_raw(ed) != 0) {
        return -1;
    }

    // Initialize ncurses
    initscr();
    noecho();
    
    int cap = 256;
    char *buf = malloc((size_t)cap);
    if (!buf) {
        endwin();
        return -1;
    }
    int len = 0;
    int pos = 0;
    int hist_pos = ed->hist_count;

    fputs(prompt, stdout);
    fflush(stdout);

    while (1) {
        int c = read_key();
        if (c == -1) {
            free(buf);
            endwin();
            disable_raw(ed);
            return -1;
        }

        if (c == '\r' || c == '\n') {
            fputs("\r\n", stdout);
            buf[len] = '\0';
            history_add(ed, buf);
            *out_line = buf;
            endwin();
            disable_raw(ed);
            return len;
        }

        if (c == 4) {
            if (len == 0) {
                free(buf);
                endwin();
                disable_raw(ed);
                return -1;
            }
            continue;
        }

        if (c == 127 || c == 8) {
            if (pos > 0) {
                memmove(&buf[pos - 1], &buf[pos], (size_t)(len - pos));
                pos--;
                len--;
                line_refresh(prompt, buf, len, pos);
            }
            continue;
        }

        if (c == 9) {
            buf[len] = '\0';
            char *word_start = &buf[pos];
            for (int i = pos - 1; i >= 0; i--) {
                if (buf[i] == ' ') {
                    word_start = &buf[i + 1];
                    break;
                }
                if (i == 0) {
                    word_start = &buf[0];
                }
            }
            const char *prefix = word_start;
            Completion *comp = completion_find(prefix);
            if (comp && comp->count > 0) {
                if (comp->count == 1) {
                    const char *match = comp->matches[0];
                    int prefix_len = (int)(word_start - buf);
                    int match_len = (int)strlen(match);
                    int new_len = prefix_len + match_len;
                    if (new_len >= cap) {
                        cap = new_len + 64;
                        char *tmp = realloc(buf, (size_t)cap);
                        if (!tmp) {
                            completion_free(comp);
                            endwin();
                            disable_raw(ed);
                            free(buf);
                            return -1;
                        }
                        buf = tmp;
                        word_start = &buf[prefix_len];
                    }
                    strcpy(word_start, match);
                    len = new_len;
                    pos = len;
                    line_refresh(prompt, buf, len, pos);
                } else {
                    // Clear and show table
                    clear();
                    print_completions_table(comp);
                    
                    // Redraw prompt and line
                    move(comp->count / ((80) / 20) + 2, 0);
                    fputs(prompt, stdout);
                    fwrite(buf, 1, (size_t)len, stdout);
                    fflush(stdout);
                }
            }
            completion_free(comp);
            continue;
        }

        if (c == 27) {
            int c1 = read_key();
            int c2 = read_key();
            if (c1 == '[') {
                if (c2 == 'A') {
                    if (ed->hist_count > 0 && hist_pos > 0) {
                        hist_pos--;
                        const char *h = history_get(ed, hist_pos);
                        len = (int)strlen(h);
                        if (len >= cap) {
                            cap = len + 64;
                            char *tmp = realloc(buf, (size_t)cap);
                            if (!tmp) {
                                endwin();
                                disable_raw(ed);
                                free(buf);
                                return -1;
                            }
                            buf = tmp;
                        }
                        memcpy(buf, h, (size_t)len + 1);
                        pos = len;
                        line_refresh(prompt, buf, len, pos);
                    }
                } else if (c2 == 'B') {
                    if (hist_pos < ed->hist_count) {
                        hist_pos++;
                        if (hist_pos == ed->hist_count) {
                            len = 0; pos = 0; buf[0] = '\0';
                        } else {
                            const char *h = history_get(ed, hist_pos);
                            len = (int)strlen(h);
                            if (len >= cap) {
                                cap = len + 64;
                                char *tmp = realloc(buf, (size_t)cap);
                                if (!tmp) {
                                    endwin();
                                    disable_raw(ed);
                                    free(buf);
                                    return -1;
                                }
                                buf = tmp;
                            }
                            memcpy(buf, h, (size_t)len + 1);
                        }
                        pos = len;
                        line_refresh(prompt, buf, len, pos);
                    }
                } else if (c2 == 'C') {
                    if (pos < len) {
                        pos++;
                        line_refresh(prompt, buf, len, pos);
                    }
                } else if (c2 == 'D') {
                    if (pos > 0) {
                        pos--;
                        line_refresh(prompt, buf, len, pos);
                    }
                }
            }
            continue;
        }

        if (isprint(c)) {
            if (len + 1 >= cap) {
                cap *= 2;
                char *tmp = realloc(buf, (size_t)cap);
                if (!tmp) {
                    endwin();
                    disable_raw(ed);
                    free(buf);
                    return -1;
                }
                buf = tmp;
            }
            memmove(&buf[pos + 1], &buf[pos], (size_t)(len - pos));
            buf[pos] = (char)c;
            pos++;
            len++;
            line_refresh(prompt, buf, len, pos);
            continue;
        }
    }
}
