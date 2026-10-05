/*
 * task - a simple task manager tui
 *
 * Manage project tasks by using this program in any directory.
 * Runing this program creates a tasks.txt file you can manage using this TUI.
 * For using another tasks file simply run this with the `-f` flag:
 *      `task -f ~/.tasks.txt`
 *
 * author:  shalom2552
 * date:    2026-10-05
 * license: MIT
 */
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define VERSION    "0.0.3"

#define TASKS_PATH ".tasks.txt"
#define STRDONE    "DONE"
#define STRTODO    "TODO"
#define LNSEP      "="
#define FLSEP      ":"

#define TITLE_LEN  1024
#define BG         "\033[1m\033[30m\033[47m"
#define RST        "\033[0m"

#define display(...) do { fprintf(stdout, __VA_ARGS__); } while (0)

typedef enum {
    TAB_TODO,
    TAB_DONE,
    TAB_ALL,
    TAB_COUNT
} TabType;

typedef enum {
    LIST_PENDING,
    LIST_DONE,
} ListType;

typedef struct {
    char title[TITLE_LEN];
} Task;

typedef struct {
    Task*    tasks;
    size_t   size;
    size_t   capacity;
    ListType type;
} TaskList;

TaskList pending_tasks = {.type = LIST_PENDING};
TaskList done_tasks    = {.type = LIST_DONE};

char* tabs[] = {
    [TAB_TODO] = "TODO",
    [TAB_DONE] = "DONE",
    [TAB_ALL]  = "ALL",
};

struct {
    TabType  tab_type;
    ListType list_type;
    size_t   selected_idx;
} cursor = {0};

struct termios origin_term;
struct termios raw_term;
int term_initialized  = 0;
char* tasks_file_path = TASKS_PATH;

void init(void)
{
    if (!term_initialized) {
        display("\033[?1049h"); // enter alt-screen
        tcgetattr(STDIN_FILENO, &origin_term);
        raw_term = origin_term;
        raw_term.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw_term);
        display("\033[2J");   // clear screen
        display("\033[?25l"); // hide cursor
    }
    term_initialized = 1;
}

void on_sig(int sig) { (void)sig; exit(0); }

void cleanup(void)
{
    if (term_initialized) {
        tcsetattr(STDIN_FILENO, TCSANOW, &origin_term);
        display("\033[?1049l"); // exit alt-screen
        display("\033[?25h");   // show cursor
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

void list_remove(TaskList* list, size_t idx)
{
    if (idx >= list->size) return;
    if (list->size < list->capacity / 2) {
        list->capacity /= 2;
        list->tasks = (Task*)realloc(list->tasks, list->capacity * sizeof(Task));
        assert(list->tasks && "Buy more RAM");
    }
    memmove(&list->tasks[idx], &list->tasks[idx + 1], (list->size - idx - 1) * sizeof(Task));
    --list->size;
}

TaskList* list_get(ListType type)
{
    switch (type) {
        case LIST_DONE:
            return &done_tasks;
        case LIST_PENDING:
            return &pending_tasks;
        default:
            return NULL;
    }
}

void load_tasks(char* path)
{
    FILE* file;
    char* line = NULL;
    size_t size = 0;

    file = fopen(path, "r");
    if (!file) return;

    Task task = {0};
    while (getline(&line, &size, file) != -1) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        size_t sep = strcspn(line, FLSEP);
        char* type = line;
        char* title = line + sep + 1;
        line[sep] = '\0';

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

size_t ui_len(char* s)
{
    size_t n = 0;
    while (*s) {
        if (*s == '\033') {
            while (*s && *s != 'm') ++s;
            if (*s) ++s;
            continue;
        }
        if (*s != '\n') ++n;
        ++s;
    }
    return n;
}

void ui_centered(char* s)
{
    struct winsize w; ioctl(0, TIOCGWINSZ, &w);
    display("\033[%zuG%s", (w.ws_col - ui_len(s)) / 2, s);
}

void ui_devider(char* title)
{
    struct winsize w; ioctl(0, TIOCGWINSZ, &w);
    for (int i = 0; i < w.ws_col; ++i) {
        display(LNSEP);
    }
    ui_centered(title);
    display("\n");
}

void ui_title_bar(void)
{
    display("\033[H"); // clear screen
    ui_devider("");

    char title[TITLE_LEN] = {0};
    for (int i = 0; i < TAB_COUNT; ++i) {
        char tab[TITLE_LEN] = {0};
        if ((TabType)i == cursor.tab_type) {
            sprintf(tab, BG" %s "RST, tabs[i]);
        } else {
            sprintf(tab, " %s ", tabs[i]);
        }
        strcat(title, tab);
    }
    strcat(title, "\n");
    ui_centered(title);
}

void ui_list(TaskList* list, char* title)
{
    ui_devider(title);
    if (list->size == 0) {
        ui_centered("List is empty\n");
    }
    size_t curr = 0;
    for (size_t i = 0; (size_t)i < list->size; ++i) {
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
    display("Total %d | Done %d | Pending %d", done + pending, done, pending);

    char right[20] = "";
    sprintf(right, "%d/%d | v%s", done, done + pending, VERSION);
    display("\033[%d;%dH%s", w.ws_row, (int)(w.ws_col - strlen(right)), right);
}

void ui_render(void)
{
    display("\033[2J");
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
    TaskList* list = list_get(cursor.list_type);
    if (val < 0 && cursor.selected_idx == 0) return;

    cursor.selected_idx += val;
    if (cursor.selected_idx >= list->size) cursor.selected_idx = list->size - 1;
}

/* needs to free the returned string */
char* ui_input_prompt(char* msg)
{
    struct winsize w; ioctl(0, TIOCGWINSZ, &w);
    display("\033[?25h");          // show cursor
    display("\033[%dH", w.ws_row); // move to bottom row

    raw_term.c_lflag |= (ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_term);

    char* prompt = malloc(TITLE_LEN);
    assert(prompt);
    display("\033[0K%s: ", msg);
    fgets(prompt, TITLE_LEN, stdin);

    raw_term.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_term);
    display("\033[?25l"); // hide cursor

    return prompt;
}

void task_create(void)
{
    char* title = ui_input_prompt("New");

    int offset = 0;
    while (title[offset] == ' ' && title[offset] != '\0') ++offset;
    if (title[offset] != '\0') {
        Task t;
        sprintf(t.title, "%s", title + offset);
        list_add(&pending_tasks, &t);
    }

    free(title);
}

void task_delete(void)
{
    TaskList* list = list_get(cursor.list_type);
    list_remove(list, cursor.selected_idx);
}

void task_edit_title(void)
{
    Task* t = NULL;
    TaskList* list = list_get(cursor.list_type);

    for (size_t i = 0; i < list->size; ++i) {
        if (i == cursor.selected_idx) {
            t = &list->tasks[i];
            break;
        }
    }

    if (t) {
        char* new_name = ui_input_prompt("Rename");
        sprintf(t->title, "%s", new_name);
        free(new_name);
    }
}

void task_mark(void)
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

void help(void)
{
    display("\033[0H\033[2J");
    ui_devider("");
    ui_centered("HELP\n");
    ui_devider("");
    puts("keys:");
    puts("  ENTER   - mark done/undone");
    puts("  j/k     - list next/prev");
    puts("  h/l     - tab next/prev");
    puts("  TAB     - tab next");
    puts("  a       - add new task");
    puts("  d       - delete a task");
    puts("  r       - rename a task");
    puts("  q       - quit");
    puts("  ?       - show this help");
    ui_devider("");
    ui_centered("press any key...");
    ui_footer();
    getchar();
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
                task_mark();
                break;
            case 'j':
                cursor_next_item(1);
                break;
            case 'k':
                cursor_next_item(-1);
                break;
            case 'a':
                task_create();
                break;
            case 'd':
                task_delete();
                break;
            case 'r':
                task_edit_title();
                break;
            case '?':
                help();
                break;
            case 'q':
                return;
            default:
                break;
        }
    }
}

int main(int argc, char** argv)
{
    atexit(cleanup);
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    init();

    if (argc > 1) {
        if (!strcmp(argv[1], "-f")) {
            tasks_file_path = argv[2];
        }
    }

    load_tasks(tasks_file_path);

    run();

    store_tasks(tasks_file_path);
    return 0;
}

