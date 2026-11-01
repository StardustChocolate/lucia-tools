/*
 * mem_scan.c —— il2cpp 内存扫描公共层实现（逻辑原样取自 equip_core.c）
 */
#include "mem_scan.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MS_BUF (1 << 20)
#define MS_MAXNAME 8192   /* 类名字符串最多收集多少份 */

static int ms_u64cmp(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *) a, y = *(const uint64_t *) b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

int ms_read_rstr(rm_handle_t *h, uint64_t s, char *o, int n) {
    o[0] = 0;
    if (s < 0x1000) return -1;
    uint32_t L = 0;
    if (rm_read_u32(h, s + 0x10, &L) || L == 0 || L > 200) return -1;
    int k = 0;
    for (uint32_t i = 0; i < L && k < n - 4; i++) {
        uint16_t c = 0;
        if (rm_read(h, s + 0x14 + i * 2, &c, 2) != 2) break;
        if (c < 0x80) o[k++] = (char) c;
        else if (c < 0x800) { o[k++] = (char) (0xC0 | (c >> 6)); o[k++] = (char) (0x80 | (c & 0x3F)); }
        else { o[k++] = (char) (0xE0 | (c >> 12)); o[k++] = (char) (0x80 | ((c >> 6) & 0x3F)); o[k++] = (char) (0x80 | (c & 0x3F)); }
    }
    o[k] = 0;
    return k;
}

int ms_find_all_cstr(rm_handle_t *h, rm_map_t *maps, int n, const char *s,
                     uint64_t *out, int maxn) {
    size_t slen = strlen(s);
    static unsigned char buf[MS_BUF];
    int cnt = 0;
    for (int i = 0; i < n && cnt < maxn; i++) {
        if (maps[i].perms[0] != 'r') continue;
        if (maps[i].path[0] == '/' &&
            (strstr(maps[i].path, "/dev/") || strstr(maps[i].path, "/dmabuf"))) continue;
        uint64_t a0 = maps[i].start, e = maps[i].end;
        for (uint64_t a = a0; a < e && cnt < maxn; a += sizeof(buf) - 256) {
            size_t w = (e - a) < sizeof(buf) ? (size_t) (e - a) : sizeof(buf);
            ssize_t g = rm_read(h, a, buf, w);
            if (g < (ssize_t) slen + 1) continue;
            for (ssize_t j = 0; j + (ssize_t) slen + 1 <= g; j++) {
                if (buf[j] == (unsigned char) s[0] && memcmp(buf + j, s, slen) == 0 && buf[j + slen] == 0) {
                    if (j == 0 || buf[j - 1] == 0) out[cnt++] = a + j;
                    if (cnt >= maxn) break;
                }
            }
        }
    }
    return cnt;
}

uint64_t ms_find_klass(rm_handle_t *h, rm_map_t *maps, int n, const char *cls, const char *ns) {
    static uint64_t namestrs[MS_MAXNAME];
    int nn = ms_find_all_cstr(h, maps, n, cls, namestrs, MS_MAXNAME);
    if (!nn) return 0;
    /*
     * 内存里类名字符串会出现很多份（metadata 明文映射、运行时字符串堆、
     * 各处缓存），"RoleData" 实测就有 190+ 份。原来只取前 256 个，
     * klass->name 指向的那份经常被截断掉，于是同一个类时而找到时而找不到。
     * 这里把上限提到 4096，并改成"排序 + 二分"，避免 O(nn) 线性比较拖慢扫描。
     */
    qsort(namestrs, (size_t) nn, sizeof(uint64_t), ms_u64cmp);
    size_t nslen = strlen(ns);
    static unsigned char buf[MS_BUF];
    for (int i = 0; i < n; i++) {
        if (maps[i].perms[0] != 'r' || maps[i].perms[1] != 'w') continue;
        if (maps[i].path[0] == '/' &&
            (strstr(maps[i].path, "/dev/") || strstr(maps[i].path, "/dmabuf"))) continue;
        uint64_t a0 = maps[i].start, e = maps[i].end;
        for (uint64_t a = a0; a < e; a += sizeof(buf) - 64) {
            size_t w = (e - a) < sizeof(buf) ? (size_t) (e - a) : sizeof(buf);
            ssize_t g = rm_read(h, a, buf, w);
            if (g < 0x20) continue;
            for (ssize_t j = 0; j + 0x20 <= g; j += 8) {
                uint64_t pn;
                memcpy(&pn, buf + j + 0x10, 8);
                if (pn < 0x1000) continue;
                if (!bsearch(&pn, namestrs, (size_t) nn, sizeof(uint64_t), ms_u64cmp)) continue;
                uint64_t pns;
                memcpy(&pns, buf + j + 0x18, 8);
                char nsbuf[64] = {0};
                rm_read(h, pns, nsbuf, nslen + 1);
                if (memcmp(nsbuf, ns, nslen) == 0 && nsbuf[nslen] == 0) return a + j;
            }
        }
    }
    return 0;
}

int ms_scan_objects(rm_handle_t *h, rm_map_t *maps, int n, uint64_t klass,
                    ms_obj_cb cb, void *user, int maxn) {
    if (!klass) return 0;
    static unsigned char buf[MS_BUF];
    int cnt = 0;
    for (int i = 0; i < n && cnt < maxn; i++) {
        if (maps[i].perms[0] != 'r' || maps[i].perms[1] != 'w') continue;
        if (maps[i].path[0] == '/' &&
            (strstr(maps[i].path, "/dev/") || strstr(maps[i].path, "/dmabuf"))) continue;
        uint64_t s = maps[i].start, e = maps[i].end;
        for (uint64_t a = s; a < e && cnt < maxn; a += sizeof(buf) - 16) {
            size_t w = (e - a) < sizeof(buf) ? (size_t) (e - a) : sizeof(buf);
            ssize_t g = rm_read(h, a, buf, w);
            if (g < 8) continue;
            for (ssize_t j = 0; j + 8 <= g; j += 8) {
                uint64_t q;
                memcpy(&q, buf + j, 8);
                if (q != klass) continue;
                if (cb && cb(h, a + j, user)) return cnt + 1;
                if (++cnt >= maxn) break;
            }
        }
    }
    return cnt;
}

/*
 * 邻域扫描：在 anchor ± win 内找 name==cls && ns==ns 的 Il2CppClass。
 * 不依赖字符串表，直接把 +0x10 指向的内容当 C 字符串比对，因此不受
 * "名字字符串没被 ms_find_all_cstr 收集到"的影响。
 * out_strs 非空时回传 ms_find_all_cstr 对该类名的命中数，便于判断是
 * 名字没搜到、还是搜到了但没对上 klass。
 */
uint64_t ms_find_klass_near(rm_handle_t *h, rm_map_t *maps, int n,
                            uint64_t anchor, uint64_t win,
                            const char *cls, const char *ns, int *out_strs) {
    if (out_strs) *out_strs = 0;
    if (!anchor) return 0;

    uint64_t lo = anchor > win ? anchor - win : 0;
    uint64_t hi = anchor + win;
    size_t clen = strlen(cls);
    size_t nslen = strlen(ns);
    if (clen > 30 || nslen > 30) return 0;

    /*
     * 关键优化：类名字符串只可能来自 metadata 字符串堆。
     * 先定位 global-metadata.dat 的映射区间，后面只对该区间内的候选指针
     * 做 rm_read。否则窗口里每个像指针的 8 字节都要读一次内存（几十万次
     * 系统调用），会把整机 CPU 吃满 —— 实测能到 280%，游戏直接被拖死。
     */
    uint64_t md_lo = 0, md_hi = 0;
    for (int i = 0; i < n; i++) {
        if (!maps[i].path[0]) continue;
        if (!strstr(maps[i].path, "global-metadata")) continue;
        if (!md_lo || maps[i].start < md_lo) md_lo = maps[i].start;
        if (maps[i].end > md_hi) md_hi = maps[i].end;
    }

    uint64_t key = 0;
    memcpy(&key, cls, clen + 1 <= 8 ? clen + 1 : 8);
    int budget = 300000;   /* 读取预算，超了就放弃，绝不无限读下去 */

    static unsigned char buf[MS_BUF];
    for (int i = 0; i < n; i++) {
        if (maps[i].perms[0] != 'r') continue;
        if (maps[i].end <= lo || maps[i].start >= hi) continue;
        if (maps[i].path[0] == '/' &&
            (strstr(maps[i].path, "/dev/") || strstr(maps[i].path, "/dmabuf"))) continue;

        uint64_t s = maps[i].start < lo ? lo : maps[i].start;
        uint64_t e = maps[i].end > hi ? hi : maps[i].end;
        for (uint64_t a = s; a < e; a += sizeof(buf) - 0x40) {
            size_t w = (e - a) < sizeof(buf) ? (size_t) (e - a) : sizeof(buf);
            ssize_t g = rm_read(h, a, buf, w);
            if (g < 0x20) continue;
            for (ssize_t j = 0; j + 0x20 <= g; j += 8) {
                uint64_t pn;
                memcpy(&pn, buf + j + 0x10, 8);
                if (pn < 0x1000 || pn > 0x0000FFFFFFFFFFFFull) continue;
                /* metadata 区间外的不可能是类名字符串，直接跳过 */
                if (md_lo && (pn < md_lo || pn >= md_hi)) continue;
                if (budget-- <= 0) goto done;
                uint64_t got = 0;
                if (rm_read(h, pn, &got, 8) != 8 || got != key) continue;
                if (clen + 1 > 8) {
                    char tb[32] = {0};
                    if (rm_read(h, pn, tb, clen + 1) != (ssize_t) (clen + 1)) continue;
                    if (memcmp(tb, cls, clen + 1) != 0) continue;
                }
                uint64_t pns;
                memcpy(&pns, buf + j + 0x18, 8);
                char nsbuf[32] = {0};
                if (rm_read(h, pns, nsbuf, nslen + 1) != (ssize_t) (nslen + 1)) continue;
                if (memcmp(nsbuf, ns, nslen) != 0) continue;
                return a + (uint64_t) j;
            }
        }
    }
done:
    if (out_strs) {
        static uint64_t tmp[MS_MAXNAME];
        *out_strs = ms_find_all_cstr(h, maps, n, cls, tmp, MS_MAXNAME);
    }
    return 0;
}

/* ===================== 权威版类定位（元数据驱动） ===================== */

/*
 * 元数据布局常量（v31 实测）：
 *   头部：u32 sanity(0xFAB11BAF) + i32 version + 26 张表的 (i32 偏移, i32 大小)
 *   表序号：2 = string 堆（类名/字段名 C 字符串），19 = typeDefinitions（88B/项）
 *   TypeDefinition：u32 nameIndex(字符串堆偏移), u32 nsIndex, ...
 *   Il2CppClass：+0x10 name, +0x18 namespaze, +0x68 typeDefinition(→元数据内)
 */
#define MS_TD_TBL 19
#define MS_STR_TBL 2
#define MS_TD_SZ 88

static uint64_t ms_md_range(rm_map_t *maps, int n, uint64_t *hi) {
    uint64_t lo = 0;
    *hi = 0;
    for (int i = 0; i < n; i++) {
        if (!maps[i].path[0]) continue;
        if (!strstr(maps[i].path, "global-metadata")) continue;
        if (!lo || maps[i].start < lo) lo = maps[i].start;
        if (maps[i].end > *hi) *hi = maps[i].end;
    }
    return lo;
}

/* 有界 C 字符串比较：str 在 [strbuf, strbuf+sz) 内，须以 0 结束且与 want 相等 */
static int ms_streq_b(const char *strbuf, size_t sz, uint32_t off,
                      const char *want) {
    size_t wl = strlen(want);
    if ((size_t) off + wl + 1 > sz) return 0;
    return memcmp(strbuf + off, want, wl + 1) == 0;
}

uint64_t ms_find_klass2(rm_handle_t *h, rm_map_t *maps, int n,
                        const char *cls, const char *ns, uint64_t *out_td) {
    if (!h || !cls) return 0;
    if (!ns) ns = "";

    uint64_t md_hi = 0;
    uint64_t md_lo = ms_md_range(maps, n, &md_hi);
    if (!md_lo) return 0;

    uint32_t sanity = 0, tdoff = 0, tdsz = 0, stroff = 0, strsz = 0;
    if (rm_read_u32(h, md_lo, &sanity) || sanity != 0xFAB11BAFu) return 0;
    if (rm_read(h, md_lo + 8 + MS_TD_TBL * 8, &tdoff, 4) != 4) return 0;
    if (rm_read(h, md_lo + 8 + MS_TD_TBL * 8 + 4, &tdsz, 4) != 4) return 0;
    if (rm_read(h, md_lo + 8 + MS_STR_TBL * 8, &stroff, 4) != 4) return 0;
    if (rm_read(h, md_lo + 8 + MS_STR_TBL * 8 + 4, &strsz, 4) != 4) return 0;
    if (!tdoff || !stroff || !tdsz || !strsz) return 0;

    /* 一次性把两张表读进来，19554 项在内存里筛，避免几万次 pread */
    unsigned char *tdbuf = (unsigned char *) malloc((size_t) tdsz);
    unsigned char *strbuf = (unsigned char *) malloc((size_t) strsz);
    if (!tdbuf || !strbuf) { free(tdbuf); free(strbuf); return 0; }
    if (rm_read(h, md_lo + tdoff, tdbuf, (size_t) tdsz) != (ssize_t) tdsz ||
        rm_read(h, md_lo + stroff, strbuf, (size_t) strsz) != (ssize_t) strsz) {
        free(tdbuf); free(strbuf);
        return 0;
    }

    uint64_t td_base = md_lo + tdoff;
    int ntd = tdsz / MS_TD_SZ;
    int tdi = -1;
    for (int i = 0; i < ntd; i++) {
        uint32_t name_i, ns_i;
        memcpy(&name_i, tdbuf + (size_t) i * MS_TD_SZ, 4);
        memcpy(&ns_i, tdbuf + (size_t) i * MS_TD_SZ + 4, 4);
        if (!ms_streq_b((const char *) strbuf, strsz, name_i, cls)) continue;
        if (!ms_streq_b((const char *) strbuf, strsz, ns_i, ns)) continue;
        tdi = i;
        break;
    }
    free(tdbuf);
    free(strbuf);
    if (tdi < 0) return 0;

    uint64_t td_addr = td_base + (uint64_t) tdi * MS_TD_SZ;
    if (out_td) *out_td = td_addr;

    /* 全堆扫 klass->typeDefinition == td_addr 的位置，回退 0x68 得 klass 候选。
       同一个 td 地址还会被 byval_arg(+0x20)/this_arg(+0x30) 等字段引用，
       所以对每个命中点把候选校验（+0x10 处 name 与元数据一致）作为硬过滤。 */
    static unsigned char buf[1 << 20];
    for (int i = 0; i < n; i++) {
        if (maps[i].perms[0] != 'r' || maps[i].perms[1] != 'w') continue;
        if (maps[i].path[0] == '/' &&
            (strstr(maps[i].path, "/dev/") || strstr(maps[i].path, "/dmabuf"))) continue;
        uint64_t s = maps[i].start, e = maps[i].end;
        for (uint64_t a = s; a < e; a += sizeof(buf) - 16) {
            size_t w = (e - a) < sizeof(buf) ? (size_t) (e - a) : sizeof(buf);
            ssize_t g = rm_read(h, a, buf, w);
            if (g < 8) continue;
            for (ssize_t j = 0; j + 8 <= g; j += 8) {
                uint64_t q;
                memcpy(&q, buf + j, 8);
                if (q != td_addr) continue;
                static const uint32_t cand_offs[] = {0x68, 0x20, 0x30};
                for (size_t c = 0; c < sizeof(cand_offs) / sizeof(cand_offs[0]); c++) {
                    uint64_t k = a + (uint64_t) j - cand_offs[c];
                    if (k < 0x1000) continue;
                    uint64_t np = 0;
                    if (rm_read_ptr(h, k + 0x10, &np)) continue;
                    /* name 只可能来自元数据字符串堆 */
                    if (np < md_lo + stroff || np >= md_lo + stroff + strsz) continue;
                    char nb[64] = {0};
                    if (rm_read(h, np, nb, 63) <= 0) continue;
                    nb[63] = 0;
                    size_t wl = strlen(cls);
                    if (strlen(nb) != wl || memcmp(nb, cls, wl) != 0) continue;
                    return k;
                }
            }
        }
    }
    return 0;
}
