/*
 * rubimem.h —— rubi 外部 root 内存引擎核心 (arm64, Android/Houdini 友好)
 *
 * 通过 /proc/<pid>/mem 的 pread64/pwrite64 从外部读写目标进程内存，
 * 不依赖 ptrace（绕过 Houdini ENOSYS），不注入任何库（绕过 AppGuard 文件校验）。
 * 需要 root（adb root 或 su）。
 */
#ifndef RUBIMEM_H
#define RUBIMEM_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 一段内存映射（/proc/pid/maps 的一行） */
typedef struct {
    uint64_t start;
    uint64_t end;
    char     perms[5];   /* 例如 "r-xp" */
    uint64_t offset;
    char     path[256];  /* 映射文件路径，匿名为空 */
} rm_map_t;

/* 引擎句柄 */
typedef struct {
    pid_t pid;
    int   mem_fd;        /* /proc/pid/mem 的打开 fd，-1 未打开 */
} rm_handle_t;

/* ---- 进程发现 ---- */
/* 按包名/进程名查找 pid，返回 0 表示未找到 */
pid_t rm_find_pid_by_name(const char *name);

/* ---- 句柄生命周期 ---- */
int  rm_open(rm_handle_t *h, pid_t pid);   /* 成功 0 */
void rm_close(rm_handle_t *h);

/* ---- 内存读写（基于 pread64/pwrite64） ---- */
/* 返回实际字节数，<0 出错 */
ssize_t rm_read(rm_handle_t *h, uint64_t addr, void *buf, size_t len);
ssize_t rm_write(rm_handle_t *h, uint64_t addr, const void *buf, size_t len);

/* 定宽便捷读取，成功 0 */
int rm_read_u32(rm_handle_t *h, uint64_t addr, uint32_t *out);
int rm_read_u64(rm_handle_t *h, uint64_t addr, uint64_t *out);
int rm_read_ptr(rm_handle_t *h, uint64_t addr, uint64_t *out);   /* arm64 指针 = 8 字节 */
int rm_read_f32(rm_handle_t *h, uint64_t addr, float *out);

/* 指针链解引用：base -> [+off0] -> [+off1] -> ... 返回最终地址，0 出错 */
uint64_t rm_chain(rm_handle_t *h, uint64_t base, const int32_t *offsets, int n);

/* ---- maps 解析 ---- */
/* 读取全部 maps 到 out 数组（最多 max 个），返回条目数，<0 出错 */
int rm_read_maps(pid_t pid, rm_map_t *out, int max);

/* 找到包含 name 子串、且权限含 want_perm（如 "r-xp"，传 NULL 不限）的第一段，
   返回该段 start 地址（即模块基址），0 未找到 */
uint64_t rm_module_base(pid_t pid, const char *name, const char *want_perm);

#ifdef __cplusplus
}
#endif

#endif /* RUBIMEM_H */
