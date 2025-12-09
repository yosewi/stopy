#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <sys/inotify.h>

/* --- MAKRA I STAŁE --- */
#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(expression) \
  (__extension__                                                              \
    ({ long int __result;                                                     \
       do __result = (long int) (expression);                                 \
       while (__result == -1L && errno == EINTR);                             \
       __result; }))
#endif

#define MAX_CMD_LEN 1024
#define MAX_ARGS 64
#define MAX_JOBS 32
#define COPY_BUF_SIZE 4096
#define EVENT_BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))
#define MAX_WATCHES 4096

/* --- STRUKTURY DANYCH --- */

struct Watch {
    int wd;
    char *path;
};

struct WatchMap {
    struct Watch items[MAX_WATCHES];
    int count;
};

/* --- ZMIENNE GLOBALNE --- */

pid_t job_pids[MAX_JOBS];
char job_srcs[MAX_JOBS][PATH_MAX];
char job_dsts[MAX_JOBS][PATH_MAX];

char *args[MAX_ARGS];
int arg_count = 0;
volatile int keep_running = 1;

/* --- FUNKCJA POMOCNICZA: BUDOWANIE ŚCIEŻKI ABSOLUTNEJ (Zamiast realpath) --- */
/* Zwraca 0 w przypadku sukcesu, -1 w przypadku błędu */
int make_absolute_path(const char *input, char *output) {
    if (input[0] == '/') {
        /* Jeśli zaczyna się od /, to już jest absolutna */
        if (strlen(input) >= PATH_MAX) return -1;
        strncpy(output, input, PATH_MAX - 1);
        output[PATH_MAX - 1] = '\0';
    } else {
        /* Jeśli nie, sklejamy CWD (obecny katalog) + / + input */
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL) return -1;
        
        /* Sprawdzamy czy się zmieści */
        if (strlen(cwd) + 1 + strlen(input) >= PATH_MAX) return -1;
        
        /* Ręczne sklejanie */
        strcpy(output, cwd);
        strcat(output, "/");
        strcat(output, input);
    }
    return 0;
}

/* --- FUNKCJE POMOCNICZE I/O --- */

ssize_t bulk_read(int fd, char* buf, size_t count) {
    ssize_t c;
    ssize_t len = 0;
    do {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0) return c;
        if (c == 0) return len;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char* buf, size_t count) {
    ssize_t c;
    ssize_t len = 0;
    do {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0) return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

int is_dir_empty(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return 0;

    struct dirent *entry;
    int empty = 1;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            empty = 0;
            break;
        }
    }
    closedir(dir);
    return empty;
}

/* --- ZARZĄDZANIE MAPĄ WATCHY --- */

void add_to_map(struct WatchMap *map, int wd, const char *path) {
    if (map->count >= MAX_WATCHES) return;
    map->items[map->count].wd = wd;
    map->items[map->count].path = strdup(path);
    map->count++;
}

struct Watch *find_watch(struct WatchMap *map, int wd) {
    for (int i = 0; i < map->count; i++) {
        if (map->items[i].wd == wd) return &map->items[i];
    }
    return NULL;
}

void remove_from_map(struct WatchMap *map, int wd) {
    for (int i = 0; i < map->count; i++) {
        if (map->items[i].wd == wd) {
            free(map->items[i].path);
            map->items[i] = map->items[map->count - 1];
            map->count--;
            return;
        }
    }
}

/* --- LOGIKA KOPIOWANIA I USUWANIA --- */

int remove_recursive(const char *path) {
    struct stat st;
    if (lstat(path, &st) < 0) return -1;

    if (!S_ISDIR(st.st_mode)) {
        return unlink(path);
    }

    DIR *dir = opendir(path);
    if (!dir) return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        
        char child_path[PATH_MAX];
        snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
        remove_recursive(child_path);
    }
    closedir(dir);
    return rmdir(path);
}

int copy_file_data(const char *src, const char *dst, mode_t mode) {
    int fd_src = TEMP_FAILURE_RETRY(open(src, O_RDONLY));
    if (fd_src < 0) return -1;

    int fd_dst = TEMP_FAILURE_RETRY(open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode));
    if (fd_dst < 0) {
        TEMP_FAILURE_RETRY(close(fd_src));
        return -1;
    }

    char buf[COPY_BUF_SIZE];
    ssize_t bytes_read;
    int result = 0;

    while ((bytes_read = bulk_read(fd_src, buf, sizeof(buf))) > 0) {
        if (bulk_write(fd_dst, buf, bytes_read) != bytes_read) {
            result = -1;
            break;
        }
    }
    if (bytes_read < 0) result = -1;

    TEMP_FAILURE_RETRY(close(fd_src));
    TEMP_FAILURE_RETRY(close(fd_dst));
    return result;
}

int copy_recursive(const char *src_base, const char *dst_base) {
    struct stat st;
    if (lstat(src_base, &st) < 0) return -1;

    if (TEMP_FAILURE_RETRY(mkdir(dst_base, st.st_mode)) < 0 && errno != EEXIST) return -1;

    DIR *dir = opendir(src_base);
    if (!dir) return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char src_path[PATH_MAX];
        char dst_path[PATH_MAX];
        snprintf(src_path, sizeof(src_path), "%s/%s", src_base, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_base, entry->d_name);

        struct stat entry_st;
        if (lstat(src_path, &entry_st) < 0) continue;

        if (S_ISDIR(entry_st.st_mode)) {
            copy_recursive(src_path, dst_path);
        } else if (S_ISREG(entry_st.st_mode)) {
            copy_file_data(src_path, dst_path, entry_st.st_mode);
        } else if (S_ISLNK(entry_st.st_mode)) {
            char target[PATH_MAX];
            ssize_t len = TEMP_FAILURE_RETRY(readlink(src_path, target, sizeof(target) - 1));
            if (len != -1) {
                target[len] = '\0';
                TEMP_FAILURE_RETRY(symlink(target, dst_path));
            }
        }
    }
    closedir(dir);
    return 0;
}

/* --- MONITORING (INOTIFY) --- */

void add_watch_recursive(int notify_fd, struct WatchMap *map, const char *path) {
    uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB | IN_DONT_FOLLOW;
    
    int wd = inotify_add_watch(notify_fd, path, mask);
    if (wd < 0) return;
    
    add_to_map(map, wd, path);

    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (lstat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            add_watch_recursive(notify_fd, map, full_path);
        }
    }
    closedir(dir);
}

void monitor_loop(const char *src_root, const char *dst_root) {
    int notify_fd = inotify_init();
    if (notify_fd < 0) {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }

    struct WatchMap map;
    map.count = 0;
    
    add_watch_recursive(notify_fd, &map, src_root);

    char buffer[EVENT_BUF_LEN];

    while (keep_running) {
        ssize_t len = read(notify_fd, buffer, EVENT_BUF_LEN);
        
        if (len < 0) {
            if (errno == EINTR) continue; 
            perror("read inotify");
            break;
        }

        ssize_t i = 0;
        while (i < len) {
            struct inotify_event *event = (struct inotify_event *) &buffer[i];
            struct Watch *w = find_watch(&map, event->wd);
            
            if (w && event->len > 0) {
                char src_path[PATH_MAX];
                char dst_path[PATH_MAX];
                
                snprintf(src_path, sizeof(src_path), "%s/%s", w->path, event->name);

                if (strncmp(src_path, src_root, strlen(src_root)) == 0) {
                    const char *rel_path = src_path + strlen(src_root);
                    snprintf(dst_path, sizeof(dst_path), "%s%s", dst_root, rel_path);

                    if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
                        remove_recursive(dst_path);
                    }
                    else if (event->mask & (IN_CREATE | IN_MOVED_TO | IN_MODIFY | IN_ATTRIB)) {
                        struct stat st;
                        if (lstat(src_path, &st) == 0) {
                            if (S_ISDIR(st.st_mode)) {
                                copy_recursive(src_path, dst_path);
                                add_watch_recursive(notify_fd, &map, src_path);
                            } else if (S_ISREG(st.st_mode)) {
                                copy_file_data(src_path, dst_path, st.st_mode);
                            } else if (S_ISLNK(st.st_mode)) {
                                char target[PATH_MAX];
                                ssize_t l = readlink(src_path, target, sizeof(target)-1);
                                if (l != -1) {
                                    target[l] = '\0';
                                    unlink(dst_path);
                                    symlink(target, dst_path);
                                }
                            }
                        }
                    }
                }
            }
            
            if (event->mask & IN_IGNORED) {
                remove_from_map(&map, event->wd);
            }

            i += sizeof(struct inotify_event) + event->len;
        }
    }

    close(notify_fd);
    for(int k=0; k<map.count; k++) free(map.items[k].path);
}

/* --- LOGIKA PROCESÓW --- */

void handle_sigterm(int sig) {
    (void)sig;
    keep_running = 0;
}

void child_work(const char *src, const char *dst) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa, NULL);

    if (copy_recursive(src, dst) != 0) {
        exit(EXIT_FAILURE);
    }

    monitor_loop(src, dst);
    
    exit(EXIT_SUCCESS);
}

void add_to_jobs_list(pid_t pid, const char *src, const char *dst) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_pids[i] == 0) {
            job_pids[i] = pid;
            strncpy(job_srcs[i], src, PATH_MAX);
            strncpy(job_dsts[i], dst, PATH_MAX);
            return;
        }
    }
    printf("Ostrzeżenie: Limit kopii zapasowych osiągnięty.\n");
}

void clean_zombie_processes() {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_pids[i] != 0) {
            int status;
            pid_t result = waitpid(job_pids[i], &status, WNOHANG);
            if (result > 0) {
                job_pids[i] = 0;
            }
        }
    }
}

/* --- PARSER I KOMENDY --- */

void clear_args() {
    for (int i = 0; i < arg_count; i++) {
        free(args[i]);
        args[i] = NULL;
    }
    arg_count = 0;
}

void parse_input(char *line) {
    clear_args();
    int len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

    char *ptr = line;
    int in_quote = 0;
    char buffer[MAX_CMD_LEN];
    int buf_idx = 0;

    while (*ptr != '\0' && arg_count < MAX_ARGS) {
        if (*ptr == '"') {
            in_quote = !in_quote;
            ptr++;
        } else if (isspace((unsigned char)*ptr) && !in_quote) {
            if (buf_idx > 0) {
                buffer[buf_idx] = '\0';
                args[arg_count++] = strdup(buffer);
                buf_idx = 0;
            }
            ptr++;
        } else {
            if (buf_idx < MAX_CMD_LEN - 1) buffer[buf_idx++] = *ptr;
            ptr++;
        }
    }
    if (buf_idx > 0) {
        buffer[buf_idx] = '\0';
        if (arg_count < MAX_ARGS) args[arg_count++] = strdup(buffer);
    }
}

void cmd_add() {
    if (arg_count < 3) {
        printf("Użycie: add <zrodlo> <cel> [cel2 ...]\n");
        return;
    }

    /* 1. Budowanie absolutnej ścieżki źródła BEZ realpath */
    char abs_src[PATH_MAX];
    if (make_absolute_path(args[1], abs_src) != 0) {
        printf("Błąd budowania ścieżki źródłowej.\n");
        return;
    }

    struct stat st;
    /* lstat sprawdzi czy plik istnieje - zastępuje to sprawdzenie z realpath */
    if (lstat(abs_src, &st) < 0 || !S_ISDIR(st.st_mode)) {
        printf("Błąd: Źródło '%s' nie istnieje lub nie jest katalogiem.\n", abs_src);
        return;
    }

    for (int i = 2; i < arg_count; i++) {
        char *target = args[i];
        
        char abs_dst[PATH_MAX];
        /* 2. Budowanie absolutnej ścieżki celu BEZ realpath */
        if (make_absolute_path(target, abs_dst) != 0) {
             printf("Błąd budowania ścieżki docelowej.\n");
             continue;
        }

        /* 3. Sprawdzenie pętli (czy cel jest wewnątrz źródła) */
        if (strncmp(abs_dst, abs_src, strlen(abs_src)) == 0) {
            if (abs_dst[strlen(abs_src)] == '/' || abs_dst[strlen(abs_src)] == '\0') {
                printf("Błąd: Nie można utworzyć kopii wewnątrz katalogu źródłowego!\n");
                continue;
            }
        }

        int duplicate = 0;
        for(int j=0; j<MAX_JOBS; j++) {
            if (job_pids[j] != 0 &&
                strcmp(job_srcs[j], abs_src) == 0 &&
                strcmp(job_dsts[j], abs_dst) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            printf("Błąd: Taka kopia jest już aktywna.\n");
            continue;
        }

        struct stat dst_st;
        if (lstat(abs_dst, &dst_st) == 0) {
            if (!S_ISDIR(dst_st.st_mode)) {
                printf("Błąd: Cel '%s' istnieje i nie jest katalogiem.\n", abs_dst); continue;
            }
            if (!is_dir_empty(abs_dst)) {
                printf("Błąd: Cel '%s' nie jest pusty.\n", abs_dst); continue;
            }
        }

        pid_t pid = fork();

        if (pid < 0) { perror("Fork error"); continue; }

        if (pid == 0) {
            close(STDIN_FILENO);
            child_work(abs_src, abs_dst);
            exit(0);
        } else {
            printf("Uruchomiono synchronizację (PID %d): %s -> %s\n", pid, abs_src, abs_dst);
            add_to_jobs_list(pid, abs_src, abs_dst);
        }
    }
}

void cmd_list() {
    clean_zombie_processes();
    printf("--- Aktywne synchronizacje ---\n");
    int found = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_pids[i] != 0) {
            printf("[%d] PID: %d | %s -> %s\n", i, job_pids[i], job_srcs[i], job_dsts[i]);
            found = 1;
        }
    }
    if (!found) printf("Brak aktywnych zadań.\n");
}

void cmd_end() {
    if (arg_count < 2) {
        printf("Użycie: end <zrodlo> [cel] ...\n");
        return;
    }
    
    char *src_arg = args[1];
    char abs_src[PATH_MAX];
    if (make_absolute_path(src_arg, abs_src) != 0) {
        printf("Błąd ścieżki źródłowej.\n");
        return;
    }
    /* Ponieważ realpath nie robi już checka istnienia, lstat jest opcjonalny
       (user może chcieć zakończyć backup katalogu, który już usunął), 
       ale dla porządku można sprawdzić. Zostawiamy jak jest. */

    for (int i = 2; i < arg_count; i++) {
        char *dst_arg = args[i];
        char abs_dst[PATH_MAX];
        
        if (make_absolute_path(dst_arg, abs_dst) != 0) {
             strncpy(abs_dst, dst_arg, PATH_MAX);
        }

        for (int j = 0; j < MAX_JOBS; j++) {
            if (job_pids[j] != 0 &&
                strcmp(job_srcs[j], abs_src) == 0 &&
                (strcmp(job_dsts[j], abs_dst) == 0)) {
                
                kill(job_pids[j], SIGTERM);
                waitpid(job_pids[j], NULL, 0);
                printf("Zatrzymano kopię (PID %d): %s -> %s\n", job_pids[j], job_srcs[j], job_dsts[j]);
                job_pids[j] = 0;
            }
        }
    }
}

int main() {
    for (int i = 0; i < MAX_JOBS; i++) job_pids[i] = 0;
    char line[MAX_CMD_LEN];
    
    printf("--- Backup System (Bez Realpath) ---\n");

    while (1) {
        clean_zombie_processes();
        printf("> ");
        if (fgets(line, sizeof(line), stdin) == NULL) break;
        parse_input(line);
        if (arg_count == 0) continue;

        if (strcmp(args[0], "exit") == 0) {
            printf("Zamykanie...\n");
            for(int i=0; i<MAX_JOBS; i++) {
                if(job_pids[i] != 0) {
                    kill(job_pids[i], SIGTERM);
                    waitpid(job_pids[i], NULL, 0);
                }
            }
            clear_args();
            break;
        } else if (strcmp(args[0], "add") == 0) {
            cmd_add();
        } else if (strcmp(args[0], "list") == 0) {
            cmd_list();
        } else if (strcmp(args[0], "end") == 0) {
            cmd_end();
        } else if (strcmp(args[0], "restore") == 0) {
            printf("TODO: Restore\n");
        } else {
            printf("Nieznana komenda.\n");
        }
        clear_args();
    }
    return 0;
}