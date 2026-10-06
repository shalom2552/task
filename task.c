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
#include <asm-generic/errno-base.h>
#include <complex.h>
#include <sys/stat.h>
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

#define VERSION    "0.2.3"

#define TASKS_FILE ".tasks.txt"
#define STATE_FILE "%s/.local/share/task/state.txt"
#define TEXT_LEN   2048

#define BG         "\033[1m\033[30m\033[47m"
#define RST        "\033[0m"

#define STRDONE    "DONE"
#define STRTODO    "TODO"
#define LNSEP      "="
#define FLSEP      ":"

#define display(...) do { fprintf(stdout, __VA_ARGS__); } while (0)
#define derror(...)                   \
    do {                              \
        display("\033[?1049l");       \
        fflush(stdout);               \
        fprintf(stderr, __VA_ARGS__); \
        display("\033[?1049h");       \
    } while (0)

typedef enum {
    TAB_TODO,
    TAB_DONE,
    TAB_ALL,
    TAB_PROJ,
    TAB_COUNT
} TabType;

char* tabs[] = {
    [TAB_TODO] = "PENDING",
    [TAB_DONE] = "DONE",
    [TAB_ALL]  = "ALL",
    [TAB_PROJ] = "PROJECTS",
};

typedef enum {
    LIST_PENDING,
    LIST_DONE,
    LIST_PROJECTS,
} ListType;

typedef struct {
    char title[TEXT_LEN];
} Item;

typedef struct {
    Item*    items;
    char*    marker;
    size_t   size;
    size_t   capacity;
    ListType type;
} List;

List pending_tasks = { .type = LIST_PENDING,  .marker = " [ ] "};
List done_tasks    = { .type = LIST_DONE,     .marker = " [X] " };
List projects      = { .type = LIST_PROJECTS, .marker = "  *  " };

struct {
    TabType  tab_type;
    ListType list_type;
    size_t   selected_idx;
} cursor = {0};

int is_new_project = 1;
char cr_project_path[TEXT_LEN];
char tasks_file_path[TEXT_LEN] = TASKS_FILE;
char state_file_path[TEXT_LEN];

struct termios origin_term;
struct termios raw_term;
int term_initialized  = 0;

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

void list_add(List* list, Item* t)
{
    if (list->size == list->capacity) {
        list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        list->items = (Item*)realloc(list->items, list->capacity * sizeof(Item));
        assert(list->items && "Buy more RAM");
    }
    list->items[list->size++] = *t;
}

void list_remove_by_idx(List* list, size_t idx)
{
    if (idx >= list->size) return;
    if (list->size < list->capacity / 2) {
        list->capacity /= 2;
        list->items = (Item*)realloc(list->items, list->capacity * sizeof(Item));
        assert(list->items && "Buy more RAM");
    }
    memmove(&list->items[idx], &list->items[idx + 1], (list->size - idx - 1) * sizeof(Item));
    --list->size;
}

void list_remove(List* list, Item* t)
{
    for (size_t i = 0; i < list->size; ++i) {
        if (&list->items[i] == t) {
            list_remove_by_idx(list, i);
            return;
        }
    }
}

List* list_get(ListType type)
{
    switch (type) {
        case LIST_DONE:
            return &done_tasks;
        case LIST_PENDING:
            return &pending_tasks;
        case LIST_PROJECTS:
            return &projects;
        default:
            return NULL;
    }
}

Item* list_get_item(List* list, size_t idx)
{
    for (size_t i = 0; i < list->size; ++i) {
        if (i == idx) return &list->items[i];
    }
    return NULL;
}

void state_add_current_project(void)
{
    if (is_new_project && (pending_tasks.size > 0 || done_tasks.size > 0)) {
        Item p;
        sprintf(p.title, "%s", cr_project_path);
        list_add(&projects, &p);
        is_new_project = 0;
    }
}

void load_state(void)
{
    FILE* file;
    char* line = NULL;
    size_t size = 0;

    file = fopen(state_file_path, "r");
    if (!file) return;

    while (getline(&line, &size, file) != -1) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (strcmp(line, cr_project_path) == 0) is_new_project = 0;

        Item p;
        sprintf(p.title, "%s", line);
        list_add(&projects, &p);
    }

    free(line);
    fclose(file);
}

void save_state(void)
{
    FILE* file;
    char* path = state_file_path;

    // Source - https://stackoverflow.com/a/21229518
    char* lastSlash = strrchr(path, '/');
    char* parent = strndup(path, lastSlash - path);

    if (mkdir(parent, 0777) == -1 && errno != EEXIST) {
        derror("mkdir: %s: %s", strerror(errno), parent);
        free(parent);
        return;
    }
    free(parent);

    file = fopen(path, "w");
    if (!file) {
        derror("fopen: %s: %s\n", strerror(errno), path);
        return;
    }

    state_add_current_project(); // if not exist
    for (size_t i = 0; i < projects.size; ++i) {
        fprintf(file, "%s\n", projects.items[i].title);
    }

    fclose(file);
}

void load_tasks(char* path)
{
    FILE* file;
    char* line = NULL;
    size_t size = 0;

    file = fopen(path, "r");
    if (!file) return;

    Item task = {0};
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
    if (is_new_project && pending_tasks.size == 0 && done_tasks.size == 0) {
        return;
    }
    FILE* file;

    file = fopen(path, "w");
    if (!file) {
        derror("fopen: %s: %s\n", strerror(errno), path);
        return;
    }

    for (size_t i = 0; i < done_tasks.size; ++i) {
        fprintf(file, STRDONE FLSEP"%s\n", done_tasks.items[i].title);
    }

    for (size_t i = 0; i < pending_tasks.size; ++i) {
        fprintf(file, STRTODO FLSEP"%s\n", pending_tasks.items[i].title);
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
    char sep[1024] = "";
    for (int i = 0; i < w.ws_col; ++i) {
        strcat(sep, LNSEP);
    }
    display("%s", sep);
    ui_centered(title);
    display("\n");
}

void ui_title_bar(void)
{
    ui_devider("");
    display("\033[K");
    char title[TEXT_LEN] = {0};
    for (int i = 0; i < TAB_COUNT; ++i) {
        char tab[TEXT_LEN] = {0};
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

void ui_list(List* list, char* title)
{
    ui_devider(title);
    display("\033[K");
    if (list->size == 0) ui_centered("List is empty\n");

    struct winsize w; ioctl(0, TIOCGWINSZ, &w);
    size_t count = w.ws_row - 5;
    size_t start = 0;

    if (cursor.tab_type == TAB_ALL) {
        count = w.ws_row / 2 - 3;
        if (count % 2 == 0 && list->type == LIST_DONE) {
            ++count;
        }
    }
    if (cursor.list_type == list->type && cursor.selected_idx >= count) {
        start = cursor.selected_idx - count + 1;
    }

    for (size_t i = start; i < start + count && i < list->size; ++i) {
        if (i == cursor.selected_idx && cursor.list_type == list->type) display(BG);
        display("%s%.*s\033[K\n"RST, list->marker, w.ws_col - (int)strlen(list->marker), list->items[i].title);
    }
}

void ui_footer(void)
{
    struct winsize w; ioctl(0, TIOCGWINSZ, &w);
    display("\033[%dH", w.ws_row - 1);
    ui_devider("");

    int done = done_tasks.size;
    int pending = pending_tasks.size;
    char* project = strrchr(cr_project_path, '/') + 1;
    display("Project /%s | Total %d | Pending %d | Done %d | <?> help", 
            project, done + pending, pending, done);

    char right[20] = "";
    sprintf(right, "%d/%d | v%s", done, done + pending, VERSION);
    display("\033[%d;%dH%s", w.ws_row, (int)(w.ws_col - strlen(right)), right);
}

void ui_render(void)
{
    struct winsize w; ioctl(0, TIOCGWINSZ, &w);
    display("\033[H");
    ui_title_bar();
    switch (cursor.tab_type) {
        case TAB_ALL:
            ui_list(&pending_tasks, "");
            display("\033[J\033[%dH", w.ws_row / 2 + 1);
            ui_list(&done_tasks, "< Done >");
            break;
        case TAB_TODO:
            ui_list(&pending_tasks, "");
            break;
        case TAB_DONE:
            ui_list(&done_tasks, "");
            break;
        case TAB_PROJ:
            ui_list(&projects, "");
            break;
        default:
            break;
    }
    display("\033[J");
    ui_footer();
}

/* needs to free the returned string */
char* ui_input_prompt(char* msg)
{
    struct winsize w; ioctl(0, TIOCGWINSZ, &w);
    display("\033[?25h");          // show cursor
    display("\033[%dH", w.ws_row); // move to bottom row

    raw_term.c_lflag |= (ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_term);

    char* prompt = malloc(TEXT_LEN);
    assert(prompt);
    display("\033[0K%s: ", msg);
    fgets(prompt, TEXT_LEN, stdin);
    prompt[strcspn(prompt, "\n")] = '\0';

    raw_term.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_term);
    display("\033[?25l"); // hide cursor

    return prompt;
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
        case TAB_PROJ:
            cursor.list_type = LIST_PROJECTS;
            break;
        default:
            break;
    }
    cursor.selected_idx = 0;
}

/* val: 1 or -1 */
void cursor_next_item(int val)
{
    List* list = list_get(cursor.list_type);
    if (cursor.tab_type == TAB_ALL) {
        if (val < 0 && (cursor.selected_idx == 0 || list->size == 0)) {
            if (cursor.list_type == LIST_DONE) {
                cursor.list_type = LIST_PENDING;
                cursor.selected_idx = pending_tasks.size - 1;
            }
            return;
        }

        if (val > 0 && (cursor.selected_idx == list->size - 1 || list->size == 0)) {
            if (cursor.list_type == LIST_PENDING) {
                cursor.list_type = LIST_DONE;
                cursor.selected_idx = 0;
            }
            return;
        }

    } else {
        if (val > 0 && cursor.selected_idx == list->size - 1) return;
        if (val < 0 && cursor.selected_idx == 0) return;
    }
    cursor.selected_idx += val;
}

void task_create(void)
{
    char* title = ui_input_prompt("New");

    int offset = 0;
    while (title[offset] == ' ' && title[offset] != '\0') ++offset;
    if (title[offset] != '\0') {
        Item t;
        sprintf(t.title, "%s", title + offset);
        list_add(&pending_tasks, &t);
    }

    free(title);
}

void task_delete(void)
{
    List* list = list_get(cursor.list_type);
    list_remove_by_idx(list, cursor.selected_idx);
    if (cursor.selected_idx >= list->size) {
        cursor_next_item(-1);
    }
}

void task_edit_title(void)
{
    List* list = list_get(cursor.list_type);
    Item* t = list_get_item(list, cursor.selected_idx);

    if (t) {
        char* new_name = ui_input_prompt("Rename");
        sprintf(t->title, "%s", new_name);
        free(new_name);
    }
}

void task_mark(void)
{
    List* src;
    List* dst;
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
            Item* t = &src->items[i];
            list_add(dst, t);
            list_remove_by_idx(src, i);
            break;
        }
    }
    if (cursor.selected_idx >= src->size) {
        cursor_next_item(-1);
    }
}

void project_select()
{
    char project_file_path[TEXT_LEN];
    Item* p = list_get_item(&projects, cursor.selected_idx);
    if (!p) return;

    int n = snprintf(project_file_path, TEXT_LEN, "%s/%s", p->title, TASKS_FILE);
    if (n >= TEXT_LEN) {
        derror("path too long: %s", p->title);
        return;
    }

    if (access(project_file_path, F_OK)) {
        // TODO: ui message 
        list_remove(&projects, p);
        if (cursor.selected_idx >= projects.size) {
            cursor_next_item(-1);
        }
        return;
    }

    state_add_current_project(); // if not exist

    store_tasks(tasks_file_path);
    pending_tasks.size = 0;
    done_tasks.size = 0;

    snprintf(tasks_file_path, TEXT_LEN, "%s", project_file_path);
    snprintf(cr_project_path, TEXT_LEN, "%s", p->title);
    load_tasks(tasks_file_path);

    cursor.selected_idx = 0;
    cursor.list_type = 0;
    cursor.tab_type = 0;

    is_new_project = 0;

    return;
}

void item_select(void)
{
    switch (cursor.list_type) {
        case LIST_PENDING:
        case LIST_DONE:
            task_mark();
            break;
        case LIST_PROJECTS:
            project_select();
            break;
        default:
            return;
    }
}

void item_add(void)
{
    switch (cursor.list_type) {
        case LIST_PENDING:
        case LIST_DONE:
            task_create();
            break;
        case LIST_PROJECTS:
            return; // no-op
        default:
            return;
    }
}

void item_delete(void)
{
    switch (cursor.list_type) {
        case LIST_PENDING:
        case LIST_DONE:
            task_delete();
            break;
        case LIST_PROJECTS:
            return; // no-op
        default:
            return;
    }
}

void item_edit(void)
{
    switch (cursor.list_type) {
        case LIST_PENDING:
        case LIST_DONE:
            task_edit_title();
            break;
        case LIST_PROJECTS:
            return; // no-op
        default:
            return;
    }
}

void help(void)
{
    display("\033[0H\033[2J");
    ui_devider("");
    ui_centered("HELP\n");
    ui_devider("");
    puts("keys:");
    puts("  ENTER     mark done/undone");
    puts("  ↑/↓       list next/prev");
    puts("  ←/→       tab next/prev");
    puts("  j/k       list next/prev");
    puts("  h/l       tab next/prev");
    puts("  TAB       tab next");
    puts("  a         add new task");
    puts("  d         delete a task");
    puts("  r         rename a task");
    puts("  ?         show this help");
    puts("  q         quit");
    puts("  ESC ESC   quit");
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
            case '\033':
                if (getchar() != '[') return;
                char e = getchar();
                switch (e) {
                    case 'A':
                        cursor_next_item(-1);
                        break;
                    case 'B':
                        cursor_next_item(1);
                        break;
                    case 'C':
                        cursor_next_tab(1);
                        break;
                    case 90:
                    case 'D':
                        cursor_next_tab(-1);
                        break;
                    default:
                        break;
                }
                break;
            case 'l':
            case '\t':
                cursor_next_tab(1);
                break;
            case 'h':
                cursor_next_tab(-1);
                break;
            case ' ':
            case '\n':
                item_select();
                break;
            case 'j':
                cursor_next_item(1);
                break;
            case 'k':
                cursor_next_item(-1);
                break;
            case 'a':
                item_add();
                break;
            case 'd':
                item_delete();
                break;
            case 'r':
                item_edit();
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
            sprintf(tasks_file_path, "%s", argv[2]);
        }
    }

    getcwd(cr_project_path, TEXT_LEN);
    sprintf(state_file_path, STATE_FILE, getenv("HOME"));

    load_state();
    load_tasks(tasks_file_path);

    run();

    store_tasks(tasks_file_path);
    save_state();
    return 0;
}

