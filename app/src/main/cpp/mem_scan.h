/*
 * mem_scan.h —— il2cpp 内存扫描公共层
 *
 * 从 equip_core.c 中抽出的通用能力：读托管字符串、搜 C 字符串、定位 Il2CppClass、
 * 按 klass 枚举实例。装备导出与 CG 解锁共用，避免两份实现分叉。
 */
#ifndef MEM_SCAN_H
#define MEM_SCAN_H

#include <stdint.h>
#include "rubimem.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 读 il2cpp System.String 为 utf8。返回写入字节数，<0 失败。 */
int ms_read_rstr(rm_handle_t *h, uint64_t str, char *out, int cap);

/* 全内存找以 NUL 结束的 C 字符串 s（要求前一个字节也是 NUL，避免命中子串）。
   返回命中数。 */
int ms_find_all_cstr(rm_handle_t *h, rm_map_t *maps, int n,
                     const char *s, uint64_t *out, int maxn);

/* 定位 Il2CppClass*：name@+0x10 命中 cls、ns@+0x18 指向 ns。返回 0 未找到。 */
uint64_t ms_find_klass(rm_handle_t *h, rm_map_t *maps, int n,
                       const char *cls, const char *ns);

/*
 * 邻域版：以已知 klass 为锚点，只在该地址 ±win 的窗口内找。
 * 用于 ms_find_klass 失手的类 —— 同一 image 的 Il2CppClass 通常聚在
 * 相邻几 MB，所以拿一个邻居当锚点比全内存按字符串表匹配更稳。
 */
uint64_t ms_find_klass_near(rm_handle_t *h, rm_map_t *maps, int n,
                            uint64_t anchor, uint64_t win,
                            const char *cls, const char *ns, int *out_strs);

/*
 * 权威版：从 global-metadata 的 TypeDef 表出发反查运行时 Il2CppClass。
 * 路径：字符串表匹配 name/ns → TypeDef 索引 tdi → 全堆扫 8 字节值
 * 等于 td_base+tdi*88（即 klass->typeDefinition）→ 命中处回退 0x68 得
 * klass 候选 → 校验候选 +0x10 的 name 与元数据一致。
 *
 * 与 ms_find_klass 的区别：不依赖"klass+0x10 恰好指向搜到的某份类名字符串"，
 * 也不受空命名空间 namespaze==NULL 的影响 —— 元数据本身是唯一事实源。
 * 返回 klass 地址，0 未找到。out_td 非空时回传 TypeDef 运行时地址。
 */
uint64_t ms_find_klass2(rm_handle_t *h, rm_map_t *maps, int n,
                        const char *cls, const char *ns, uint64_t *out_td);

/*
 * 枚举实例：扫描所有可写堆段，凡首字（对象头 klass 指针）等于 klass 的都回调。
 * 返回回调次数（受 maxn 限制）。cb 返回非 0 可提前终止。
 */
typedef int (*ms_obj_cb)(rm_handle_t *h, uint64_t obj, void *user);
int ms_scan_objects(rm_handle_t *h, rm_map_t *maps, int n, uint64_t klass,
                    ms_obj_cb cb, void *user, int maxn);

#ifdef __cplusplus
}
#endif
#endif /* MEM_SCAN_H */
