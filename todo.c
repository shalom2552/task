#include <assert.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>

#define display(...) do { fprintf(stdout, __VA_ARGS__); } while (0)

#define TASKS_PATH "todo.txt"
#define TITLE_LEN 1024

#define LNSEP "="
#define FLSEP ":"
#define STRDONE "DONE"
#define STRTODO "TODO"

#define BG "\033[1m\033[30m\033[47m"
#define RST "\033[0m"

struct termios origin_term;
int term_initialized = 0;
int alt_screen = 1;

typedef enum {
    TAB_TODO,
    TAB_DONE,
    TAB_ALL,
    TAB_COUNT
} TabType;

char* tabs[] = {
    [TAB_TODO] = "TODO",
    [TAB_DONE] = "DONE",
    [TAB_ALL] = "ALL",
};

typedef enum {
    LIST_PENDING,
    LIST_DONE,
} ListType;

typedef struct {
    char title[TITLE_LEN];
} Task;

typedef struct {
    Task* tasks;
    size_t size;
    size_t capacity;
    ListType type;
} TaskList;

TaskList pending_tasks = {.type = LIST_PENDING};
TaskList done_tasks = {.type = LIST_DONE};

struct {
    TabType tab_type;
    ListType list_type;
    size_t selected_idx;
} cursor = {0};

void init(void)
{
    if (!term_initialized) {
        if (alt_screen) display("\033[?1049h");
        tcgetattr(STDIN_FILENO, &origin_term);
        struct termios raw = origin_term;
        raw.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        display("\033[2J"); // clear screen
        display("\033[?25l"); // hide cursor

    }
    term_initialized = 1;
}

void on_sig(int sig) { (void)sig; exit(0); }

void cleanup(void)
{
    if (term_initialized) {
        tcsetattr(STDIN_FILENO, TCSANOW, &origin_term);
        if (alt_screen) display("\033[?1049l");
        display("\033[?25h"); // show cursor
    }
    term_initialized = 0;
}

void list_add(TaskList* list, Task* t)
{
    if (list->size == list->capacity) {
        list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        list->tasks = (Task*)realloc(list->tasks, list->capacity * sizeof(Task));
        assert(list->tasks && "Buy more RAM");
    }
    list->tasks[list->size++] = *t;
}

void list_remove(TaskList* list, int idx)
{
    if (list->size < list->capacity / 2) {
        list->capacity /= 2;
        list->tasks = (Task*)realloc(list->tasks, list->capacity * sizeof(Task));
        assert(list->tasks && "Buy more RAM");
    }
    memmove(&list->tasks[idx], &list->tasks[idx + 1], (list->size - idx - 1) * sizeof(Task));
    --list->size;
}

void load_tasks(char* path)
{
    FILE* file;
    char* line = NULL;
    size_t size = 0;

    file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "fopen: %s: %s\n", strerror(errno), path);
        return;
    }

    Task task = {0};
    while (getline(&line, &size, file) != -1) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char* type = strtok(line, FLSEP);
        char* title = strtok(NULL, FLSEP);
        if (!type || !title) continue;

        while (*title == ' ') ++title;

        strcpy(task.title, title);

        if (!strcmp(type, STRTODO)) {
            list_add(&pending_tasks, &task);
        }
        if (!strcmp(type, STRDONE)) {
            list_add(&done_tasks, &task);
        }
    }

    free(line);
    fclose(file);
}

void store_tasks(char* path)
{
    FILE* file;

    file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "fopen: %s: %s\n", strerror(errno), path);
        exit(1);
    }

    for (size_t i = 0; i < done_tasks.size; ++i) {
        fprintf(file, STRDONE FLSEP"%s\n", done_tasks.tasks[i].title);
    }

    for (size_t i = 0; i < pending_tasks.size; ++i) {
        fprintf(file, STRTODO FLSEP"%s\n", pending_tasks.tasks[i].title);
    }

    fclose(file);
}

void ui_devider(char* title)
{
    struct winsize w; ioctl(0, TIOCGWINSZ, &w);
    for (int i = 0; i < w.ws_col; ++i) {
        display(LNSEP);
    }
    display("\033[%zuG%s", (w.ws_col - strlen(title)) / 2, title);
    display("\n");
}

void ui_title_bar(void)
{
    display("\033[H");
    ui_devider("");
    struct winsize w; ioctl(0, TIOCGWINSZ, &w);
    int title_len = 0;
    for (int i = 0; tabs[i] != 0; ++i) title_len += strlen(tabs[i]) + 2;

    display("\033[%dC", (w.ws_col - title_len) / 2);
    for (int i = 0; i < TAB_COUNT; ++i) {
        if ((TabType)i == cursor.tab_type) {
            display(BG"[%s]"RST, tabs[i]);
        } else {
            display(" %s ", tabs[i]);
        }
    }
    display("\n");
}

void ui_list(TaskList* list, char* title)
{
    ui_devider(title);
    if (list->size == 0) {
        struct winsize w; ioctl(0, TIOCGWINSZ, &w);
        char* empty = "List is empty\n";
        display("\033[%dC%s", (w.ws_col - (int)strlen(empty)) / 2, empty);
    }
    int curr = 0;
    for (int i = 0; (size_t)i < list->size; ++i) {
        if (curr == cursor.selected_idx && cursor.list_type == list->type) display(BG);
        display(" [%s] %s\n"RST, list->type == LIST_DONE ? "X" : " ", list->tasks[i].title);
        ++curr;
    }
}

void ui_footer(void)
{
    struct winsize w; ioctl(0, TIOCGWINSZ, &w);
    display("\033[%dH", w.ws_row - 1);
    ui_devider("");
    int done = done_tasks.size;
    int pending = pending_tasks.size;
    display("Total %d, Done %d, Pending %d", done + pending, done, pending);
}

void ui_render(void)
{
    display("\033[2J"); // clear screen
    ui_title_bar();
    switch (cursor.tab_type) {
        case TAB_ALL:
            ui_list(&pending_tasks, "");
            ui_list(&done_tasks, "");
            break;
        case TAB_TODO:
            ui_list(&pending_tasks, "");
            break;
        case TAB_DONE:
            ui_list(&done_tasks, "");
            break;
        default:
            break;
    }
    ui_footer();
}

void cursor_next_tab(int val)
{
    cursor.tab_type = (cursor.tab_type + val + TAB_COUNT) % TAB_COUNT;
    switch (cursor.tab_type) {
        case TAB_ALL:
            cursor.list_type = LIST_PENDING;
            break;
        case TAB_TODO:
            cursor.list_type = LIST_PENDING;
            break;
        case TAB_DONE:
            cursor.list_type = LIST_DONE;
            break;
        default:
            break;
    }
    cursor.selected_idx = 0;
}

void cursor_next_item(int val)
{
    TaskList* list;
    if (cursor.list_type == LIST_PENDING) list = &pending_tasks;
    if (cursor.list_type == LIST_DONE)    list = &done_tasks;

    if (val < 0 && cursor.selected_idx == 0) return;
    cursor.selected_idx += val;
    if (cursor.selected_idx >= list->size) cursor.selected_idx = list->size - 1;
}

void flip_selected_task(void)
{
    TaskList* src;
    TaskList* dst;
    if (cursor.list_type == LIST_PENDING) {
        src = &pending_tasks;
        dst = &done_tasks;
    }
    if (cursor.list_type == LIST_DONE) {
        src = &done_tasks;
        dst = &pending_tasks;
    }

    for (size_t i = 0; i < src->size; ++i) {
        if (i == cursor.selected_idx) {
            Task* t = &src->tasks[i];
            list_add(dst, t);
            list_remove(src, i);
            break;
        }
    }
    if (cursor.selected_idx >= src->size) {
        cursor_next_item(-1);
    }
}

void run(void)
{
    while (1) {
        ui_render();
        char c = getchar();
        switch (c) {
            case 'l':
            case '\t':
                cursor_next_tab(1);
                break;
            case 'h':
                cursor_next_tab(-1);
                break;
            case ' ':
            case '\n':
                flip_selected_task();
                break;
            case 'j':
                cursor_next_item(1);
                break;
            case 'k':
                cursor_next_item(-1);
                break;
            case 'q':
                return;
            default:
                break;
        }
    }
}

int main(void)
{
    atexit(cleanup);
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    init();

    load_tasks(TASKS_PATH);
    run();
    store_tasks(TASKS_PATH);
    return 0;
}

