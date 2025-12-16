#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source)                                                \
  (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
   exit(EXIT_FAILURE))

#define MAX_CMD_LEN 1024
#define MAX_ARGS 64
#define MAX_JOBS 32
#define COPY_BUF_SIZE 4096
#define EVENT_BUF_LEN (64 * (sizeof(struct inotify_event) + NAME_MAX + 1))
#define MAX_WATCHES 8192

struct Watch {
  int wd;
  char *path;
};

struct WatchMap {
  struct Watch watch_map[MAX_WATCHES];
  int watch_count;
};

pid_t pids[MAX_JOBS];
char pid_srcs[MAX_JOBS][PATH_MAX];
char pid_dsts[MAX_JOBS][PATH_MAX];

char *args[MAX_ARGS];
int arg_count = 0;
volatile int keep_running = 1;
volatile int main_keep_running = 1;

void sethandler(void (*f)(int), int sigNo) {
  struct sigaction act;
  memset(&act, 0, sizeof(struct sigaction));
  act.sa_handler = f;
  if (sigaction(sigNo, &act, NULL) == -1) {
    ERR("sigaction");
  }
}

void main_handler(int sig) { main_keep_running = 0; }

void sigterm_handler(int sig) { keep_running = 0; }

ssize_t bulk_read(int fd, char *buf, size_t count) {
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

ssize_t bulk_write(int fd, char *buf, size_t count) {
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

int make_absolute_path(const char *input, char *output) {
  char *res = realpath(input, output);
  if (res == NULL) {
    perror("realpath");
    return -1;
  }
  return 0;
}

int is_dir_empty(const char *path) {
  DIR *d;
  struct dirent *dp;
  int empty = 1;

  if ((d = opendir(path)) == NULL) {
    perror("opendir\n");
    return 0;
  }

  do {
    if ((dp = readdir(d)) != NULL) {
      if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {
        empty = 0;
        break;
      }
    }
  } while (dp != NULL);

  if (closedir(d)) {
    ERR("closedir");
  }

  return empty;
}

void add_to_map(struct WatchMap *map, int wd, const char *path) {
  if (map->watch_count >= MAX_WATCHES) {
    fprintf(stderr, "Too many watches\n");
    return;
  }
  map->watch_map[map->watch_count].wd = wd;
  map->watch_map[map->watch_count].path = strdup(path);
  map->watch_count++;
}

struct Watch *find_watch(struct WatchMap *map, int wd) {
  for (int i = 0; i < map->watch_count; i++) {
    if (map->watch_map[i].wd == wd) {
      return &map->watch_map[i];
    }
  }
  return NULL;
}

void remove_from_map(struct WatchMap *map, int wd) {
  for (int i = 0; i < map->watch_count; i++) {
    if (map->watch_map[i].wd == wd) {
      free(map->watch_map[i].path);
      map->watch_map[i] = map->watch_map[map->watch_count - 1];
      map->watch_count--;
      return;
    }
  }
}

void update_watch_paths(struct WatchMap *map, const char *old_path,
                        const char *new_path) {
  size_t old_len = strlen(old_path);

  for (int i = 0; i < map->watch_count; i++) {
    if (strncmp(map->watch_map[i].path, old_path, old_len) == 0 &&
        (map->watch_map[i].path[old_len] == '/' ||
         map->watch_map[i].path[old_len] == '\0')) {
      char new_full_path[PATH_MAX];
      snprintf(new_full_path, sizeof(new_full_path), "%s%s", new_path,
               map->watch_map[i].path + old_len);
      free(map->watch_map[i].path);
      map->watch_map[i].path = strdup(new_full_path);
    }
  }
}

void add_watch_recursive(int notify_fd, struct WatchMap *map,
                         const char *base_path) {
  uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM |
                  IN_MOVED_TO | IN_DELETE_SELF;
  int wd = inotify_add_watch(notify_fd, base_path, mask);

  if (wd < 0) {
    perror("inotify_add_watch");
    return;
  }

  add_to_map(map, wd, base_path);

  DIR *dir = opendir(base_path);

  if (!dir) {
    perror("opendir");
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name);

    struct stat st;
    if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
      add_watch_recursive(notify_fd, map, full_path);
    }
  }

  if (closedir(dir)) {
    ERR("closedir");
  }
}

int copy_file_data(const char *src, const char *dst, mode_t mode) {
  int f_src = TEMP_FAILURE_RETRY(open(src, O_RDONLY));
  if (f_src == -1) {
    perror("open\n");
    return -1;
  }

  struct stat st;
  if (fstat(f_src, &st) < 0) {
    TEMP_FAILURE_RETRY(close(f_src));
    perror("fstat\n");
    return -1;
  }

  int f_dst = TEMP_FAILURE_RETRY(open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode));

  if (f_dst == -1) {
    TEMP_FAILURE_RETRY(close(f_src));
    perror("open\n");
    return -1;
  }

  char buf[COPY_BUF_SIZE];
  ssize_t bytes_read;
  int result = 0;

  while ((bytes_read = bulk_read(f_src, buf, sizeof(buf))) > 0) {
    if (bulk_write(f_dst, buf, bytes_read) != bytes_read) {
      result = -1;
      perror("bulk_write\n");
      break;
    }
  }
  if (bytes_read < 0) {
    result = -1;
  }

  if (result == 0) {
    struct timespec times[2];
    times[0] = st.st_atim;
    times[1] = st.st_mtim;

    if (futimens(f_dst, times) < 0) {
      result = -1;
    }

    if (fchmod(f_dst, mode) < 0) {
      perror("fchmod");
      result = -1;
    }
  }

  TEMP_FAILURE_RETRY(close(f_src));
  TEMP_FAILURE_RETRY(close(f_dst));
  return result;
}

int copy_recursive(const char *src_base, const char *dst_base,
                   const char *root_src, const char *root_dst) {
  DIR *d;
  struct dirent *entry;
  struct stat st;

  if (lstat(src_base, &st) < 0) {
    perror("lstat\n");
    return -1;
  }

  if (TEMP_FAILURE_RETRY(mkdir(dst_base, st.st_mode)) < 0 && errno != EEXIST) {
    perror("mkdir\n");
    return -1;
  }

  if ((d = opendir(src_base)) == NULL) {
    perror("opendir\n");
    return -1;
  }

  while ((entry = readdir(d)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    char src_path[PATH_MAX];
    char dst_path[PATH_MAX];

    snprintf(src_path, sizeof(src_path), "%s/%s", src_base, entry->d_name);
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_base, entry->d_name);

    struct stat entry_st;

    if (lstat(src_path, &entry_st) < 0) {
      perror("lstat\n");
      continue;
    }

    if (S_ISDIR(entry_st.st_mode)) {
      copy_recursive(src_path, dst_path, root_src, root_dst);
    }

    else if (S_ISREG(entry_st.st_mode)) {
      copy_file_data(src_path, dst_path, entry_st.st_mode);
    }

    else if (S_ISLNK(entry_st.st_mode)) {
      char target[PATH_MAX];
      ssize_t len =
          TEMP_FAILURE_RETRY(readlink(src_path, target, sizeof(target) - 1));
      if (len != -1) {
        target[len] = '\0';
        unlink(dst_path);

        if (strncmp(target, root_src, strlen(root_src)) == 0) {
          char new_target[PATH_MAX];
          snprintf(new_target, sizeof(new_target), "%s%s", root_dst,
                   target + strlen(root_src));
          TEMP_FAILURE_RETRY(symlink(new_target, dst_path));
        }

        else {
          TEMP_FAILURE_RETRY(symlink(target, dst_path));
        }
      }
    }
  }

  if (closedir(d)) {
    ERR("closedir");
  }

  return 0;
}

int remove_recursive(const char *path) {
  struct stat st;
  DIR *d;

  if (lstat(path, &st) < 0) {
    perror("lstat\n");
    return -1;
  }

  if (!S_ISDIR(st.st_mode)) {
    return unlink(path);
  }

  if ((d = opendir(path)) == NULL) {
    perror("opendir\n");
    return -1;
  }

  struct dirent *entry;
  while ((entry = readdir(d)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    char child_path[PATH_MAX];
    snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
    remove_recursive(child_path);
  }

  if (closedir(d)) {
    ERR("closedir");
  }

  return rmdir(path);
}

void monitor(const char *src_base, const char *dst_base) {
  int notify_fd = inotify_init();

  if (notify_fd < 0) {
    exit(EXIT_FAILURE);
  }

  struct WatchMap map = {0};

  add_watch_recursive(notify_fd, &map, src_base);

  int root_wd = -1;
  if (map.watch_count > 0) {
    root_wd = map.watch_map[0].wd;
  }

  uint32_t pending_cookie = 0;
  char pending_move_path[PATH_MAX] = "";
  char buffer[EVENT_BUF_LEN];

  while (keep_running) {
    ssize_t len = read(notify_fd, buffer, EVENT_BUF_LEN);

    if (len < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }

    ssize_t i = 0;
    while (i < len) {
      struct inotify_event *event = (struct inotify_event *)&buffer[i];

      if ((event->mask & IN_IGNORED) || (event->mask & IN_DELETE_SELF)) {
        if (event->wd == root_wd) {
          keep_running = 0;
          break;
        }

        remove_from_map(&map, event->wd);

        if (map.watch_count == 0) {
          keep_running = 0;
        }

        i += sizeof(struct inotify_event) + event->len;
        continue;
      }

      struct Watch *watch = find_watch(&map, event->wd);

      if (watch && event->len > 0) {
        char src_path[PATH_MAX];
        char dst_path[PATH_MAX];

        snprintf(src_path, sizeof(src_path), "%s/%s", watch->path, event->name);

        if (strncmp(src_path, src_base, strlen(src_base)) == 0) {
          const char *rel_path = src_path + strlen(src_base);
          snprintf(dst_path, sizeof(dst_path), "%s%s", dst_base, rel_path);

          if (event->mask & IN_MOVED_FROM) {
            pending_cookie = event->cookie;
            strncpy(pending_move_path, src_path, sizeof(pending_move_path));

            remove_recursive(dst_path);
          }

          else if (event->mask & IN_MOVED_TO) {
            if (event->cookie == pending_cookie && pending_cookie != 0) {
              update_watch_paths(&map, pending_move_path, src_path);
              pending_cookie = 0;
            }

            struct stat st;
            if (lstat(src_path, &st) == 0) {
              if (S_ISDIR(st.st_mode)) {
                copy_recursive(src_path, dst_path, src_base, dst_base);
                add_watch_recursive(notify_fd, &map, src_path);
              } else if (S_ISREG(st.st_mode)) {
                copy_file_data(src_path, dst_path, st.st_mode);
              }
            }
          }

          else if (event->mask & IN_DELETE) {
            remove_recursive(dst_path);
          }

          else if (event->mask & (IN_CREATE | IN_MODIFY | IN_ATTRIB)) {
            struct stat st;

            if (lstat(src_path, &st) == 0) {
              if (S_ISDIR(st.st_mode)) {
                copy_recursive(src_path, dst_path, src_base, dst_base);
                add_watch_recursive(notify_fd, &map, src_path);
              }

              else if (S_ISREG(st.st_mode)) {
                copy_file_data(src_path, dst_path, st.st_mode);
              }

              else if (S_ISLNK(st.st_mode)) {
                char target[PATH_MAX];
                ssize_t l = readlink(src_path, target, sizeof(target) - 1);
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
      i += sizeof(struct inotify_event) + event->len;
    }
  }

  close(notify_fd);
  for (int k = 0; k < map.watch_count; k++) {
    free(map.watch_map[k].path);
  }
}

void child_work(const char *src, const char *dst) {
  sethandler(sigterm_handler, SIGTERM);
  sethandler(SIG_IGN, SIGINT);

  if (copy_recursive(src, dst, src, dst) != 0) {
    exit(EXIT_FAILURE);
  }

  monitor(src, dst);

  exit(EXIT_SUCCESS);
}

void add_to_pids_list(pid_t pid, const char *src, const char *dst) {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (pids[i] == 0) {
      pids[i] = pid;
      strncpy(pid_srcs[i], src, PATH_MAX);
      strncpy(pid_dsts[i], dst, PATH_MAX);
      return;
    }
  }
  printf("Too many children!!!\n");
}

void forkbomb_protector() {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (pids[i] != 0) {
      pid_t result = waitpid(pids[i], NULL, WNOHANG);

      if (result > 0 || (result == -1 && errno == ECHILD)) {
        pids[i] = 0;
      }
    }
  }
}

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

  if (len > 0 && line[len - 1] == '\n') {
    line[len - 1] = '\0';
  }

  char *ptr = line;
  int in_quote = 0;
  char buffer[MAX_CMD_LEN];
  int buf_idx = 0;
  while (*ptr != '\0' && arg_count < MAX_ARGS) {
    if (*ptr == '\\') {
      ptr++;

      if (*ptr != '\0') {
        if (buf_idx < MAX_CMD_LEN - 1) {
          buffer[buf_idx++] = *ptr;
        }
        ptr++;
      }

      continue;
    }

    if (*ptr == '"') {
      in_quote = !in_quote;
      ptr++;
    }

    else if (isspace((unsigned char)*ptr) && !in_quote) {
      if (buf_idx > 0) {
        buffer[buf_idx] = '\0';
        args[arg_count++] = strdup(buffer);
        buf_idx = 0;
      }
      ptr++;
    }

    else {
      if (buf_idx < MAX_CMD_LEN - 1) {
        buffer[buf_idx++] = *ptr;
        ptr++;
      }
    }
  }
  if (buf_idx > 0) {
    buffer[buf_idx] = '\0';

    if (arg_count < MAX_ARGS) {
      args[arg_count++] = strdup(buffer);
    }
  }
}

void cmd_add() {
  if (arg_count < 3) {
    printf("Usage: add <source> <backup> <backup2> ...\n");
    return;
  }

  char abs_src[PATH_MAX];

  if (make_absolute_path(args[1], abs_src) != 0) {
    printf("Source path error\n");
    return;
  }

  struct stat st;

  if (lstat(abs_src, &st) < 0 || !S_ISDIR(st.st_mode)) {
    printf("Error: Source '%s' is not a directory.\n", abs_src);
    return;
  }

  for (int i = 2; i < arg_count; i++) {
    char *target = args[i];
    char abs_dst[PATH_MAX];
    int created_new = 0;

    struct stat st_check;
        if (lstat(target, &st_check) < 0) {
            if (errno == ENOENT) {
                if (mkdir(target, 0755) < 0) {
                    perror("mkdir target failed");
                    continue;
                }
                created_new = 1;
            } else {
                perror("lstat target failed");
                continue;
            }
        }

    if (make_absolute_path(target, abs_dst) != 0) {
      printf("Destination path error\n");
      continue;
    }

    if (strncmp(abs_dst, abs_src, strlen(abs_src)) == 0) {
      if (abs_dst[strlen(abs_src)] == '/' || abs_dst[strlen(abs_src)] == '\0') {
        printf("Error: Backup inside a source directory\n");
        continue;
      }
    }

    int duplicate = 0;

    for (int j = 0; j < MAX_JOBS; j++) {
      if (pids[j] != 0 && strcmp(pid_srcs[j], abs_src) == 0 &&
          strcmp(pid_dsts[j], abs_dst) == 0) {
        duplicate = 1;
        break;
      }
    }

    if (duplicate) {
      printf("Error: That process already exists\n");
      continue;
    }

    if (!created_new) {
        struct stat dst_st;
        if (lstat(abs_dst, &dst_st) == 0) {
            if (!S_ISDIR(dst_st.st_mode) || !is_dir_empty(abs_dst)) { 
                printf("Error: Destination '%s' is not empty.\n", abs_dst); 
                continue; 
            }
        }
    }

    pid_t pid = fork();

    if (pid < 0) {
      perror("Fork error");
      continue;
    }

    if (pid == 0) {
      child_work(abs_src, abs_dst);
      exit(EXIT_SUCCESS);
    }

    else {
      printf("Start PID %d: %s -> %s\n", pid, abs_src, abs_dst);
      add_to_pids_list(pid, abs_src, abs_dst);
    }
  }
}

void cmd_list() {
  forkbomb_protector();

  printf("Active processes:\n");
  int found = 0;
  for (int i = 0; i < MAX_JOBS; i++) {
    if (pids[i] != 0) {
      printf("[%d] PID: %d | %s -> %s\n", i, pids[i], pid_srcs[i], pid_dsts[i]);
      found = 1;
    }
  }
  if (!found) {
    printf("None.\n");
  }
}

void cmd_end() {
  if (arg_count < 2) {
    printf("Usage: end <source> <backup> <backup2> ...\n");
    return;
  }

  char *src_arg = args[1];
  char abs_src[PATH_MAX];

  if (make_absolute_path(src_arg, abs_src) != 0) {
    printf("Source error\n");
    return;
  }

  for (int i = 2; i < arg_count; i++) {
    char *dst_arg = args[i];
    char abs_dst[PATH_MAX];

    if (make_absolute_path(dst_arg, abs_dst) != 0) {
      strncpy(abs_dst, dst_arg, PATH_MAX);
    }

    for (int j = 0; j < MAX_JOBS; j++) {
      if (pids[j] != 0 && strcmp(pid_srcs[j], abs_src) == 0 &&
          strcmp(pid_dsts[j], abs_dst) == 0) {
        kill(pids[j], SIGTERM);
        waitpid(pids[j], NULL, 0);
        printf("Stop PID %d: %s -> %s\n", pids[j], pid_srcs[j], pid_dsts[j]);
        pids[j] = 0;
      }
    }
  }
}

int restore_copy(const char *backup_base, const char *src_base) {
  DIR *d;

  if ((d = opendir(backup_base)) == NULL) {
    return -1;
  }

  struct stat st_backup_base;

  if (lstat(backup_base, &st_backup_base) == 0) {
    if (mkdir(src_base, st_backup_base.st_mode) < 0 && errno != EEXIST) {
    }
  }

  struct dirent *entry;

  while ((entry = readdir(d)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    char backup_path[PATH_MAX];
    char src_path[PATH_MAX];

    snprintf(backup_path, sizeof(backup_path), "%s/%s", backup_base,
             entry->d_name);
    snprintf(src_path, sizeof(src_path), "%s/%s", src_base, entry->d_name);

    struct stat st_backup;

    if (lstat(backup_path, &st_backup) < 0) {
      continue;
    }

    if (S_ISDIR(st_backup.st_mode)) {
      restore_copy(backup_path, src_path);
    }

    else if (S_ISREG(st_backup.st_mode)) {
      struct stat st_src;
      int need_copy = 1;

      if (lstat(src_path, &st_src) == 0 && S_ISREG(st_src.st_mode)) {
        if (st_src.st_size == st_backup.st_size &&
            st_src.st_mtime == st_backup.st_mtime) {
          need_copy = 0;
        }
      }

      if (need_copy) {
        copy_file_data(backup_path, src_path, st_backup.st_mode);
      }

    } else if (S_ISLNK(st_backup.st_mode)) {
      char target[PATH_MAX];
      ssize_t len = readlink(backup_path, target, sizeof(target) - 1);
      if (len != -1) {
        target[len] = '\0';
        unlink(src_path);
        symlink(target, src_path);
      }
    }
  }

  if (closedir(d)) {
    return -1;
  }

  return 0;
}

int restore_clean(const char *src_base, const char *backup_base) {
  DIR *d;

  if ((d = opendir(src_base)) == NULL) {
    return -1;
  }

  struct dirent *entry;

  while ((entry = readdir(d)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    char src_path[PATH_MAX];
    char backup_path[PATH_MAX];

    snprintf(src_path, sizeof(src_path), "%s/%s", src_base, entry->d_name);
    snprintf(backup_path, sizeof(backup_path), "%s/%s", backup_base,
             entry->d_name);

    struct stat st_backup;

    if (lstat(backup_path, &st_backup) < 0) {
      remove_recursive(src_path);
    }

    else {
      struct stat st_src;
      if (lstat(src_path, &st_src) == 0 && S_ISDIR(st_src.st_mode)) {
        restore_clean(src_path, backup_path);
      }
    }
  }

  if (closedir(d)) {
    return -1;
  }

  return 0;
}

void cmd_restore() {
  if (arg_count != 3) {
    printf("Usage: restore <source> <target>\n");
    return;
  }

  char abs_src[PATH_MAX];
  char abs_backup[PATH_MAX];

  if (make_absolute_path(args[1], abs_src) != 0) {
    printf("Source error\n");
    return;
  }

  if (make_absolute_path(args[2], abs_backup) != 0) {
    printf("Backup error\n");
    return;
  }

  struct stat st;

  if (lstat(abs_backup, &st) < 0 || !S_ISDIR(st.st_mode)) {
    printf("Error: Backup '%s' does not exist\n", abs_backup);
    return;
  }

  for (int j = 0; j < MAX_JOBS; j++) {
    if (pids[j] != 0 && strcmp(pid_srcs[j], abs_src) == 0 &&
        strcmp(pid_dsts[j], abs_backup) == 0) {
      printf("Error: You are watching '%s' by '%s'\n", pid_srcs[j],
             pid_dsts[j]);
      return;
    }
  }

  printf("Restoring: %s -> %s\n", abs_backup, abs_src);

  restore_copy(abs_backup, abs_src);

  restore_clean(abs_src, abs_backup);

  printf("Done.\n");
}

int main() {
  sethandler(main_handler, SIGINT);
  sethandler(main_handler, SIGTERM);

  sigset_t mask;
  sigfillset(&mask);
  sigdelset(&mask, SIGINT);
  sigdelset(&mask, SIGTERM);
  sigdelset(&mask, SIGCHLD);

  if (sigprocmask(SIG_SETMASK, &mask, NULL) == -1) {
    ERR("sigprocmask");
  }

  for (int i = 0; i < MAX_JOBS; i++) {
    pids[i] = 0;
  }

  char line[MAX_CMD_LEN];
  printf("Interactive backups\n");

  while (main_keep_running) {
    forkbomb_protector();

    if (fgets(line, sizeof(line), stdin) == NULL) {
      if (errno == EINTR && main_keep_running) {
        continue;
      }
      break;
    }

    parse_input(line);
    if (arg_count == 0) {
      continue;
    }

    if (strcmp(args[0], "exit") == 0) {
      break;
    }

    else if (strcmp(args[0], "add") == 0) {
      cmd_add();
    }

    else if (strcmp(args[0], "list") == 0) {
      cmd_list();
    }

    else if (strcmp(args[0], "end") == 0) {
      cmd_end();
    }

    else if (strcmp(args[0], "restore") == 0) {
      cmd_restore();
    }

    else {
      printf("Unknown command\n");
    }

    clear_args();
  }

  printf("\nFinish\n");
  for (int i = 0; i < MAX_JOBS; i++) {
    if (pids[i] != 0) {
      kill(pids[i], SIGTERM);
    }
  }
  clear_args();
  return 0;
}