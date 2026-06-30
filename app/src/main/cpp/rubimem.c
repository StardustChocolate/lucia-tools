/*
 * rubimem.c —— rubi 外部 root 内存引擎核心实现
 */
#include "rubimem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <inttypes.h>

/* ---------------- 进程发现 ---------------- */

pid_t rm_find_pid_by_name(const char *name) {
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *e;
    pid_t found = 0;
    char path[64], buf[512];
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        /* 优先读 cmdline（含完整包名） */
        snprintf(path, sizeof(path), "/proc/%s/cmdline", e->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) continue;
        buf[n] = '\0';
        /* cmdline 以 \0 分隔，第一段就是进程名 */
        if (strcmp(buf, name) == 0) {
            found = (pid_t)atoi(e->d_name);
            break;
        }
    }
    closedir(d);
    return found;
}

/* ---------------- 句柄 ---------------- */

int rm_open(rm_handle_t *h, pid_t pid) {
    if (!h) return -1;
    h->pid = pid;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/mem", pid);
    h->mem_fd = open(path, O_RDWR);
    if (h->mem_fd < 0) {
        /* 退而求其次：只读 */
        h->mem_fd = open(path, O_RDONLY);
    }
    return h->mem_fd < 0 ? -1 : 0;
}

void rm_close(rm_handle_t *h) {
    if (h && h->mem_fd >= 0) {
        close(h->mem_fd);
        h->mem_fd = -1;
    }
}

/* ---------------- 读写 ---------------- */

ssize_t rm_read(rm_handle_t *h, uint64_t addr, void *buf, size_t len) {
    if (!h || h->mem_fd < 0) return -1;
    return pread64(h->mem_fd, buf, len, (off_t)addr);
}

ssize_t rm_write(rm_handle_t *h, uint64_t addr, const void *buf, size_t len) {
    if (!h || h->mem_fd < 0) return -1;
    return pwrite64(h->mem_fd, buf, len, (off_t)addr);
}

int rm_read_u32(rm_handle_t *h, uint64_t addr, uint32_t *out) {
    return rm_read(h, addr, out, 4) == 4 ? 0 : -1;
}
int rm_read_u64(rm_handle_t *h, uint64_t addr, uint64_t *out) {
    return rm_read(h, addr, out, 8) == 8 ? 0 : -1;
}
int rm_read_ptr(rm_handle_t *h, uint64_t addr, uint64_t *out) {
    return rm_read_u64(h, addr, out);
}
int rm_read_f32(rm_handle_t *h, uint64_t addr, float *out) {
    return rm_read(h, addr, out, 4) == 4 ? 0 : -1;
}

uint64_t rm_chain(rm_handle_t *h, uint64_t base, const int32_t *offsets, int n) {
    uint64_t cur = base;
    for (int i = 0; i < n; i++) {
        uint64_t next;
        if (rm_read_ptr(h, cur + offsets[i], &next) != 0) return 0;
        cur = next;
    }
    return cur;
}

/* ---------------- maps 解析 ---------------- */

int rm_read_maps(pid_t pid, rm_map_t *out, int max) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[512];
    int count = 0;
    while (count < max && fgets(line, sizeof(line), f)) {
        rm_map_t *m = &out[count];
        memset(m, 0, sizeof(*m));
        /* 格式: start-end perms offset dev inode path */
        char perms[8] = {0};
        char pathbuf[256] = {0};
        int matched = sscanf(line, "%" SCNx64 "-%" SCNx64 " %7s %" SCNx64 " %*s %*s %255[^\n]",
                             &m->start, &m->end, perms, &m->offset, pathbuf);
        if (matched < 4) continue;
        strncpy(m->perms, perms, sizeof(m->perms) - 1);
        /* path 可能前导空格 */
        char *p = pathbuf;
        while (*p == ' ') p++;
        strncpy(m->path, p, sizeof(m->path) - 1);
        count++;
    }
    fclose(f);
    return count;
}

uint64_t rm_module_base(pid_t pid, const char *name, const char *want_perm) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[512];
    uint64_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, name)) continue;
        if (want_perm) {
            /* perms 在第一个空格后 */
            char *sp = strchr(line, ' ');
            if (!sp || strncmp(sp + 1, want_perm, strlen(want_perm)) != 0) continue;
        }
        base = strtoull(line, NULL, 16);
        break;
    }
    fclose(f);
    return base;
}
