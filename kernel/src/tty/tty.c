#include "tty.h"
#include "sched/task.h"
#include "terminal/terminal.h"
#include "fs/vfs.h"
#include <stddef.h>
#include <string.h>

#define TTY_BUF_SIZE 256
#define CMD_BUF_SIZE 128

static char buf[TTY_BUF_SIZE]; // ring buffer so you can type as fast as u want :-)
static volatile size_t head = 0; // next write slot
static volatile size_t tail = 0; // next read slot

static char cmd_buf[CMD_BUF_SIZE];
static size_t cmd_len = 0;

void tty_enqueue(char c) {
    size_t next = (head + 1) % TTY_BUF_SIZE;
    if (next == tail) return; // buffer full, drop the character
    buf[head] = c;
    head = next;
}

static void print_prompt(void) {
    kputchar('$');
    kputchar(' ');
}

void tty_init(void) {
    print_prompt();
}

void tty_clear(void) {
    terminal_clear();
    head = 0;
    tail = 0;
}

static void tty_cat(const char *arg) {
    char path[VFS_PATH_LENGTH];
    if (arg[0] == '/') {
        strcpy(path, arg);
    } else {
        path[0] = '/';
        strcpy(path + 1, arg);
    }

    int fd = open(path, 0);
    if (fd < 0) {
        kprintf("cat: %s: not found\n", arg);
        return;
    }

    char read_buf[64];
    int64_t n;
    while ((n = read(fd, read_buf, sizeof(read_buf) - 1)) > 0) {
        read_buf[n] = '\0';
        kprintf("%s", read_buf);
    }
    kprintf("\n");

    close(fd);
}

static void tty_ls(const char *arg) {
    char path[VFS_PATH_LENGTH];
    if (arg[0] == '\0') {
        path[0] = '/';
        path[1] = '\0';
    } else if (arg[0] == '/') {
        strcpy(path, arg);
    } else {
        path[0] = '/';
        strcpy(path + 1, arg);
    }

    int dir_fd = opendir(path);
    if (dir_fd < 0) {
        kprintf("ls: %s: not found\n", arg);
        return;
    }

    vfs_dirent_t entry;
    while (readdir(dir_fd, &entry) == 1) {
        kprintf(entry.is_dir ? "%s/\n" : "%s\n", entry.name);
    }

    closedir(dir_fd);
}

static void tty_execute_command(const char *cmd) {
    if (strcmp(cmd, "clear") == 0) {
        tty_clear();
    } else if (strcmp(cmd, "tasks") == 0) {
        print_tasks();
    } else if (strncmp(cmd, "cat ", 4) == 0) {
        tty_cat(cmd + 4);
    } else if (strcmp(cmd, "ls") == 0) {
        tty_ls("");
    } else if (strncmp(cmd, "ls ", 3) == 0) {
        tty_ls(cmd + 3);
    } else {
        // kprintf("unknown command: %s\n", cmd);
    }
}

void tty_poll(void) {
    while (tail != head) {
        char c = buf[tail];
        tail = (tail + 1) % TTY_BUF_SIZE;

        if (c == 0x0C) { // ^L: clear screen
            tty_clear();
            print_prompt();
            continue;
        }

        if (c == '\n') {
            cmd_buf[cmd_len] = '\0';
            kputchar(c);
            if (cmd_len > 0) tty_execute_command(cmd_buf);
            cmd_len = 0;
            print_prompt();
            continue;
        }

        if (c == '\b') {
            if (cmd_len > 0) {
                cmd_len--;
                kbackspace();
            }
            continue;
        }

        if (cmd_len < CMD_BUF_SIZE - 1) {
            cmd_buf[cmd_len++] = c;
            kputchar(c);
        }
    }
}
