/*
 * probe_core.c —— 运行时结构探针（仅供调试，不影响正常功能）
 *
 * 能力：
 *   cls   a1=类名 a2=命名空间   → 字段表（名字 + 实例偏移）
 *   obj   a1=类名 a2=命名空间 n=数量 → 实例字段值
 *   roles n=数量                → RoleStaticData 的 PropertySetting 字典
 *   dict  a1=字典地址(hex) a2="+18" 可先解引用 → 字典键值
 *   u64   a1=地址(hex)          → 读 8 字节并按 指针/字符串/数值 解释
 *
 * 实例枚举必须过滤 ms_scan_objects 的假阳性：内存里到处都有
 * "某个 8 字节等于 klass 指针" 的表结构。这里用三条硬校验：
 *   1) 对象头 +0x08(monitor) 必须为 0
 *   2) 指定 ID 字段能解出长度合法的字符串
 *   3) 多个字符串字段指针不能全是同一个地址
 */
#include "probe_core.h"
#include "mem_scan.h"
#include "rubimem.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>

#define PB_MAXFLD 128
#define PB_MAXOBJ 256

typedef struct {
    char  name[40];
    int32_t off;
} pb_field_t;

typedef struct {
    pb_field_t f[PB_MAXFLD];
    int n;
    uint64_t arr;
} pb_fields_t;

typedef struct {
    char *buf;
    int cap;
    int len;
} pb_out_t;

typedef struct {
    uint64_t *arr;
    int n;
    int max;
    const pb_fields_t *fs;
    int32_t id_off;     /* >=0 时要求该字段是合法字符串 */
    int need_distinct;  /* 字符串字段不能全同 */
} pb_scan_t;

static void put(pb_out_t *o, const char *fmt, ...) {
    if (o->len >= o->cap - 2) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(o->buf + o->len, (size_t) (o->cap - o->len), fmt, ap);
    va_end(ap);
    if (w > 0) {
        o->len += w;
        if (o->len >= o->cap) o->len = o->cap - 1;
        o->buf[o->len] = 0;
    }
}

static int pb_cstr(rm_handle_t *h, uint64_t p, char *out, int cap) {
    out[0] = 0;
    if (p < 0x1000) return 0;
    unsigned char b[64];
    ssize_t g = rm_read(h, p, b, sizeof(b));
    if (g <= 0) return 0;
    int k = 0;
    for (; k < g && k < cap - 1; k++) {
        unsigned char c = b[k];
        if (c == 0) break;
        if (c < 0x20 || c > 0x7e) return 0;
        out[k] = (char) c;
    }
    if (k == 0) return 0;
    out[k] = 0;
    return k;
}

static int pb_field_at(rm_handle_t *h, uint64_t e, pb_field_t *out) {
    uint64_t np = 0;
    if (rm_read_ptr(h, e, &np) || np < 0x1000) return 0;
    char nm[40];
    if (pb_cstr(h, np, nm, sizeof(nm)) < 1) return 0;
    int32_t off = 0;
    if (rm_read(h, e + 0x18, &off, 4) != 4) return 0;
    if (off < -1 || off > 0x4000) return 0;
    if (out) {
        snprintf(out->name, sizeof(out->name), "%s", nm);
        out->off = off;
    }
    return 1;
}

static uint64_t pb_find_fields(rm_handle_t *h, uint64_t klass) {
    /* 两轮：先按 klass 常规布局找，找不到再扩大范围。
     * 某些纯设置类（如 Game.BattlePlayerSetting）的字段表指针不在常规区段，
     * 宽范围搜索时多校验一个 FieldInfo 条目以降低误报。 */
    for (int pass = 0; pass < 2; pass++) {
        int lo = pass ? 0x10 : 0x40;
        int hi = pass ? 0x220 : 0xb0;
        for (int off = lo; off <= hi; off += 8) {
            if (pass && off >= 0x40 && off <= 0xb0) continue;  /* 第一轮已试过 */
            uint64_t p = 0;
            if (rm_read_ptr(h, klass + (uint64_t) off, &p) || p < 0x10000) continue;
            pb_field_t a, b, c;
            if (!pb_field_at(h, p, &a)) continue;
            if (!pb_field_at(h, p + 0x20, &b)) continue;
            if (a.off < 0 && b.off < 0) continue;
            if (pass && !pb_field_at(h, p + 0x40, &c)) continue;
            return p;
        }
    }
    return 0;
}

static void pb_read_fields(rm_handle_t *h, uint64_t klass, pb_fields_t *o) {
    memset(o, 0, sizeof(*o));
    uint64_t arr = pb_find_fields(h, klass);
    if (!arr) return;
    o->arr = arr;
    for (int i = 0; i < PB_MAXFLD; i++) {
        pb_field_t f;
        if (!pb_field_at(h, arr + (uint64_t) i * 0x20, &f)) break;
        int dup = 0;
        for (int k = 0; k < o->n; k++) if (!strcmp(o->f[k].name, f.name)) dup = 1;
        if (dup) break;
        o->f[o->n++] = f;
    }
}

static int32_t pb_field_off(const pb_fields_t *fs, const char *name, const char *prefix) {
    for (int k = 0; k < fs->n; k++) {
        if (name && !strcmp(fs->f[k].name, name)) return fs->f[k].off;
        if (prefix && !strncmp(fs->f[k].name, prefix, strlen(prefix))) return fs->f[k].off;
    }
    return -1;
}

static int pb_obj_class(rm_handle_t *h, uint64_t p, char *out, int cap) {
    out[0] = 0;
    if (p < 0x100000) return 0;
    uint64_t k = 0;
    if (rm_read_ptr(h, p, &k) || k < 0x1000) return 0;
    uint64_t np = 0;
    if (rm_read_ptr(h, k + 0x10, &np) || np < 0x1000) return 0;
    return pb_cstr(h, np, out, cap) > 0;
}

static int pb_scan_cb(rm_handle_t *h, uint64_t obj, void *user) {
    pb_scan_t *c = (pb_scan_t *) user;
    if (c->n >= c->max) return 1;
    uint64_t mon = 1;
    if (rm_read_ptr(h, obj + 8, &mon)) return 0;
    if (mon) return 0;

    if (c->id_off == -2) {
        /* 只校验 monitor */
    } else if (c->id_off >= 0) {
        uint64_t sp = 0;
        char s[64];
        if (rm_read_ptr(h, obj + (uint64_t) c->id_off, &sp)) return 0;
        int L = ms_read_rstr(h, sp, s, sizeof(s));
        if (L < 2 || L > 48) return 0;
    } else if (c->fs) {
        uint64_t first = 0;
        int good = 0, distinct = 0;
        for (int k = 0; k < c->fs->n && k < 14; k++) {
            int32_t off = c->fs->f[k].off;
            if (off < 0x10 || off > 0x400) continue;
            uint64_t v = 0;
            char s[64];
            if (rm_read_ptr(h, obj + (uint64_t) off, &v)) continue;
            if (ms_read_rstr(h, v, s, sizeof(s)) > 3) {
                good++;
                if (!first) first = v;
                else if (v != first) distinct = 1;
            }
        }
        if (good < 1) return 0;
        if (c->need_distinct && !distinct) return 0;
    }
    c->arr[c->n++] = obj;
    return 0;
}

static int pb_collect(rm_handle_t *h, rm_map_t *maps, int nmaps, uint64_t klass,
                      const pb_fields_t *fs, int32_t id_off, int need_distinct,
                      uint64_t *out, int max) {
    pb_scan_t c;
    c.arr = out; c.n = 0; c.max = max; c.fs = fs;
    c.id_off = id_off; c.need_distinct = need_distinct;
    /* ms_scan_objects 的 maxn 计的是"原始命中数"（含被过滤的假阳性），
       这里要给大值，真正的上限由回调里的 c.max 控制。 */
    ms_scan_objects(h, maps, nmaps, klass, pb_scan_cb, &c, 500000);
    return c.n;
}

static void pb_dump_dict(pb_out_t *o, rm_handle_t *h, uint64_t addr, int maxn) {
    if (addr < 0x10000) { put(o, "  dict addr invalid\n"); return; }
    uint64_t ent = 0;
    int32_t cnt = 0;
    if (rm_read_ptr(h, addr + 0x18, &ent) || ent < 0x1000) {
        put(o, "  dict @0x%llx no entries\n", (unsigned long long) addr);
        return;
    }
    rm_read(h, addr + 0x20, &cnt, 4);
    int32_t alen = 0;
    rm_read(h, ent + 0x18, &alen, 4);
    put(o, "  dict @0x%llx count=%d alen=%d\n", (unsigned long long) addr, cnt, alen);
    int lim = alen < maxn ? alen : maxn;
    if (lim < 0) lim = 0;
    for (int x = 0; x < lim; x++) {
        uint64_t ep = ent + 0x20 + (uint64_t) x * 0x18;
        uint64_t kp = 0, vp = 0;
        if (rm_read_ptr(h, ep + 8, &kp)) continue;
        if (rm_read_ptr(h, ep + 0x10, &vp)) continue;
        char key[64] = {0};
        ms_read_rstr(h, kp, key, sizeof(key));
        char tbuf[48] = {0};
        double dv = 0;
        if (vp >= 0x1000) {
            uint64_t tp = 0;
            if (rm_read_ptr(h, vp + 0x10, &tp) == 0) ms_read_rstr(h, tp, tbuf, sizeof(tbuf));
            rm_read(h, vp + 0x18, &dv, 8);
        }
        float fv;
        memcpy(&fv, &dv, 4);
        put(o, "   [%d] %-26s raw=0x%llx type=%-14s dbl=%.6g f32=%.6g\n",
            x, key, (unsigned long long) vp, tbuf, dv, (double) fv);
    }
}

/* 在 PropertySetting 的克隆字典里查键，返回 PropertyValue 的 double 地址（value+0x18）。
 * 与 battle_core.c 的 bt_clone_val_addr() 同一套布局，key 必须按 Il2CppString 读。 */
static uint64_t pb_dict_val_addr(rm_handle_t *h, uint64_t ps, const char *key) {
    if (ps < 0x1000) return 0;
    uint64_t cm = 0;
    if (rm_read_ptr(h, ps + 0x18, &cm) || cm < 0x1000) return 0;
    uint64_t ent = 0; int32_t cnt = 0;
    if (rm_read_ptr(h, cm + 0x18, &ent) || ent < 0x1000) return 0;
    if (rm_read(h, cm + 0x20, &cnt, 4) != 4 || cnt <= 0 || cnt > 128) return 0;
    int32_t alen = 0;
    if (rm_read(h, ent + 0x18, &alen, 4) != 4 || alen <= 0 || alen > 128) return 0;
    for (int x = 0; x < alen; x++) {
        uint64_t ep = ent + 0x20 + (uint64_t) x * 0x18;
        uint64_t kp = 0, pv = 0;
        if (rm_read_ptr(h, ep + 8, &kp)) continue;
        if (rm_read_ptr(h, ep + 0x10, &pv)) continue;
        if (kp < 0x1000 || pv < 0x1000) continue;
        char ky[40];
        if (ms_read_rstr(h, kp, ky, sizeof(ky)) <= 0) continue;
        if (!strcmp(ky, key)) return pv + 0x18;
    }
    return 0;
}

/* RoleStaticData 实例校验：ID(+0x10) 必须是 "H###" 形式的角色 ID。
 * 直接按 klass 指针扫会命中大量配置表/数组里的假阳性（ID 全指向同一字符串）。 */
static int pb_rs_list_n = 0;
static uint64_t pb_rs_obj[768];
static char pb_rs_id[768][16];

static int pb_rs_cb(rm_handle_t *h, uint64_t obj, void *user) {
    (void) user;
    if (pb_rs_list_n >= 768) return 1;
    uint64_t idp = 0;
    if (rm_read_ptr(h, obj + 0x10, &idp)) return 0;
    char id[16] = {0};
    if (ms_read_rstr(h, idp, id, sizeof(id)) < 4) return 0;
    if (id[0] != 'H') return 0;
    for (int k = 1; id[k]; k++)
        if (id[k] < '0' || id[k] > '9') return 0;
    uint64_t ps = 0;
    if (rm_read_ptr(h, obj + 0x190, &ps) || ps < 0x1000) return 0;
    snprintf(pb_rs_id[pb_rs_list_n], sizeof(pb_rs_id[0]), "%s", id);
    pb_rs_obj[pb_rs_list_n] = obj;
    pb_rs_list_n++;
    return 0;
}

static void pb_dump_obj(pb_out_t *o, rm_handle_t *h, uint64_t obj, const pb_fields_t *fs) {
    put(o, "--- obj @0x%llx\n", (unsigned long long) obj);
    for (int k = 0; k < fs->n; k++) {
        int32_t off = fs->f[k].off;
        if (off < 0) continue;
        uint64_t v = 0;
        if (rm_read_ptr(h, obj + (uint64_t) off, &v)) continue;
        char s1[64] = {0}, s2[64] = {0};
        int isstr = ms_read_rstr(h, v, s1, sizeof(s1)) > 0;
        int isobj = isstr ? 0 : pb_obj_class(h, v, s2, sizeof(s2));
        if (isstr) put(o, "   %-30s @0x%-3x = \"%s\"\n", fs->f[k].name, off, s1);
        else if (isobj) put(o, "   %-30s @0x%-3x = obj:%s @0x%llx\n", fs->f[k].name, off, s2, (unsigned long long) v);
        else {
            int32_t i32 = (int32_t) (v & 0xffffffffu);
            float f32; memcpy(&f32, &v, 4);
            put(o, "   %-30s @0x%-3x = 0x%llx (i32=%d f32=%.4g)\n",
                fs->f[k].name, off, (unsigned long long) v, i32, (double) f32);
        }
    }
}

typedef struct { uint64_t a; uint32_t v; } pb_iv_t;
static int pb_cmp_iv(const void *x, const void *y) {
    uint64_t p = ((const pb_iv_t *) x)->a, q = ((const pb_iv_t *) y)->a;
    return p < q ? -1 : (p > q ? 1 : 0);
}

int lt_probe(int pid, const char *cmd, const char *a1, const char *a2, int n,
             char *outbuf, int cap) {
    rm_handle_t h;
    if (rm_open(&h, pid)) return -1;
    static rm_map_t maps[8192];
    int nmaps = rm_read_maps(pid, maps, 8192);
    if (nmaps <= 0) { rm_close(&h); return -2; }

    pb_out_t o;
    o.buf = outbuf; o.cap = cap; o.len = 0;
    if (cap > 0) outbuf[0] = 0;

    const char *ns = (a2 && a2[0] == '-') ? "" : ((a2 && a2[0]) ? a2 : "Game");

    if (!strcmp(cmd, "cls")) {
        uint64_t td = 0;
        uint64_t klass = ms_find_klass2(&h, maps, nmaps, a1, ns, &td);
        put(&o, "cls %s klass=0x%llx td=0x%llx\n",
            a1, (unsigned long long) klass, (unsigned long long) td);
        if (klass) {
            pb_fields_t fs;
            pb_read_fields(&h, klass, &fs);
            put(&o, "fields_arr=0x%llx n=%d\n", (unsigned long long) fs.arr, fs.n);
            for (int i = 0; i < fs.n; i++)
                put(&o, "  +%-3d %-34s off=0x%x\n", i, fs.f[i].name, fs.f[i].off);
        }
    } else if (!strcmp(cmd, "maps")) {
        const char *filt = (a1 && a1[0] && strcmp(a1, "-")) ? a1 : NULL;
        for (int i = 0; i < nmaps; i++) {
            if (filt && !strstr(maps[i].path, filt)) continue;
            put(&o, "%llx-%llx %s %8lluK %s\n", (unsigned long long)maps[i].start, (unsigned long long)maps[i].end, maps[i].perms, (unsigned long long)((maps[i].end - maps[i].start) / 1024), maps[i].path);
        }
    } else if (!strcmp(cmd, "bscan")) {
        unsigned char pat[128]; int pn = 0;
        if (a1) for (const char *q = a1; q[0] && q[1] && pn < (int)sizeof(pat); q += 2) {
            char hx[3] = {q[0], q[1], 0}; pat[pn++] = (unsigned char)strtoul(hx, NULL, 16);
        }
        int limit = n > 0 ? n : 256; int hits = 0;
        for (int i = 0; i < nmaps && hits < limit; i++) {
            if (maps[i].perms[0] != 114 || maps[i].perms[1] != 119) continue;
            uint64_t s = maps[i].start, e = maps[i].end;
            for (uint64_t a = s; a + (uint64_t)pn <= e && hits < limit; a += 4096) {
                static unsigned char pb[4096 + 128];
                size_t w = (e - a) < 4096 ? (size_t)(e - a) : 4096;
                ssize_t g = rm_read(&h, a, pb, w);
                if (g < pn) continue;
                for (ssize_t j = 0; j + pn <= g && hits < limit; j++) {
                    if (memcmp(pb + j, pat, (size_t)pn)) continue;
                    uint64_t addr = a + (uint64_t)j;
                    unsigned char cb[32] = {0}; rm_read(&h, addr, cb, sizeof(cb));
                    put(&o, "hit @0x%llx:", (unsigned long long)addr);
                    for (int z = 0; z < 32; z++) put(&o, " %02x", cb[z]);
                    put(&o, "\n");
                    hits++;
                }
            }
        }
        put(&o, "bscan hits=%d\n", hits);
    } else if (!strcmp(cmd, "qscan")) {
        uint64_t target = strtoull(a1 ? a1 : "0", NULL, 16);
        int limit = n > 0 ? n : 256;
        int hits = 0;
        for (int i = 0; i < nmaps && hits < limit; i++) {
            if (maps[i].perms[0] != 114 || maps[i].perms[1] != 119) continue;
            uint64_t s = maps[i].start, e = maps[i].end;
            for (uint64_t a = s; a + 8 <= e && hits < limit; a += 4096) {
                unsigned char pb[4096];
                size_t w = (e - a) < sizeof(pb) ? (size_t)(e - a) : sizeof(pb);
                ssize_t g = rm_read(&h, a, pb, w);
                if (g < 8) continue;
                for (ssize_t j = 0; j + 8 <= g; j += 8) {
                    uint64_t v; memcpy(&v, pb + j, 8);
                    if (v != target) continue;
                    uint64_t ctx = a + (uint64_t)j;
                    unsigned char cb[32] = {0};
                    rm_read(&h, ctx, cb, sizeof(cb));
                    put(&o, "hit @0x%llx:", (unsigned long long)ctx);
                    for (int q = 0; q < 32; q++) put(&o, " %02x", cb[q]);
                    put(&o, "\n");
                    if (++hits >= limit) break;
                }
            }
        }
        put(&o, "qscan target=0x%llx hits=%d\n", (unsigned long long)target, hits);
    } else if (!strcmp(cmd, "blscan")) {
        uint64_t target = strtoull(a1 ? a1 : "0", NULL, 16);
        int limit = n > 0 ? n : 256;
        int hits = 0;
        for (int i = 0; i < nmaps && hits < limit; i++) {
            if (maps[i].perms[0] != 114) continue;
            uint64_t s = maps[i].start, e = maps[i].end;
            for (uint64_t a = s; a + 4 <= e && hits < limit; a += 4096) {
                unsigned char pb[4096];
                size_t w = (e - a) < sizeof(pb) ? (size_t)(e - a) : sizeof(pb);
                ssize_t g = rm_read(&h, a, pb, w);
                if (g < 4) continue;
                for (ssize_t j = 0; j + 4 <= g; j += 4) {
                    uint32_t ins; memcpy(&ins, pb + j, 4);
                    if ((ins & 0xFC000000u) != 0x94000000u && (ins & 0xFC000000u) != 0x14000000u) continue;
                    int64_t imm = (int64_t)(ins & 0x03FFFFFFu);
                    if (imm & 0x02000000) imm -= 0x04000000;
                    uint64_t pc = a + (uint64_t)j;
                    uint64_t dst = pc + (uint64_t)(imm << 2);
                    if (dst != target) continue;
                    put(&o, "call @0x%llx -> 0x%llx is_b=%d\n", (unsigned long long)pc, (unsigned long long)dst, (int)(ins >> 31));
                    if (++hits >= limit) break;
                }
            }
        }
        put(&o, "blscan target=0x%llx hits=%d\n", (unsigned long long)target, hits);
    } else if (!strcmp(cmd, "save")) {
        uint64_t addr = strtoull(a1 ? a1 : "0", NULL, 16);
        int len = n > 0 ? n : 256;
        if (len > (1 << 20)) len = (1 << 20);
        unsigned char *mb = (unsigned char *) malloc((size_t) len);
        if (!mb) put(&o, "save oom\n");
        else {
            ssize_t got = rm_read(&h, addr, mb, (size_t) len);
            FILE *fp = fopen(a2, "wb");
            if (!fp) put(&o, "save open fail %s\n", a2);
            else {
                if (got > 0) fwrite(mb, 1, (size_t) got, fp);
                fclose(fp);
                put(&o, "saved %lld bytes @0x%llx -> %s\n", (long long) got, (unsigned long long) addr, a2);
            }
            free(mb);
        }
    } else if (!strcmp(cmd, "cstr")) {
        uint64_t addr = strtoull(a1, NULL, 16);
        char s[256] = {0};
        int L = pb_cstr(&h, addr, s, sizeof(s));
        put(&o, "cstr @0x%llx len=%d = \"%s\"\n", (unsigned long long) addr, L, s);
    } else if (!strcmp(cmd, "str")) {
        uint64_t addr = strtoull(a1, NULL, 16);
        char s[256] = {0};
        int L = ms_read_rstr(&h, addr, s, sizeof(s));
        put(&o, "str @0x%llx len=%d = \"%s\"\n", (unsigned long long) addr, L, s);
    } else if (!strcmp(cmd, "cname")) {
        uint64_t klass = strtoull(a1, NULL, 16);
        int depth = n > 0 ? n : 4;
        for (int d = 0; d < depth && klass >= 0x1000; d++) {
            uint64_t np = 0, nsp = 0, par = 0, td = 0;
            rm_read_ptr(&h, klass + 0x10, &np);
            rm_read_ptr(&h, klass + 0x18, &nsp);
            rm_read_ptr(&h, klass + 0x58, &par);
            rm_read_ptr(&h, klass + 0x68, &td);
            char nm[128] = {0}, ns[128] = {0};
            pb_cstr(&h, np, nm, sizeof(nm));
            pb_cstr(&h, nsp, ns, sizeof(ns));
            put(&o, "class[%d] @0x%llx %s.%s parent=0x%llx td=0x%llx\n", d, (unsigned long long) klass, ns, nm, (unsigned long long) par, (unsigned long long) td);
            for (int off = 0; off < 0x140; off += 8) {
                uint64_t v = 0;
                if (rm_read_ptr(&h, klass + (uint64_t) off, &v)) continue;
                char cn[128] = {0}, cs[128] = {0};
                int iscls = 0;
                if (v >= 0x1000) {
                    uint64_t p1 = 0, p2 = 0;
                    if (!rm_read_ptr(&h, v + 0x10, &p1) && !rm_read_ptr(&h, v + 0x18, &p2) && !pb_cstr(&h, p1, cn, sizeof(cn)) && !pb_cstr(&h, p2, cs, sizeof(cs))) iscls = 1;
                }
                put(&o, "  +0x%03x = 0x%016llx", off, (unsigned long long) v);
                if (iscls) put(&o, "  class=%s.%s", cs, cn);
                put(&o, "\n");
            }
            klass = par;
        }
    } else if (!strcmp(cmd, "rolesd")) {
        /* rolesd：列出 RoleStaticData 真实例（ID 形如 H###）及各自 Speed 值/可写地址 */
        uint64_t klass = ms_find_klass2(&h, maps, nmaps, "RoleStaticData", "Game", NULL);
        put(&o, "rolesd klass=0x%llx\n", (unsigned long long) klass);
        if (klass) {
            pb_rs_list_n = 0;
            ms_scan_objects(&h, maps, nmaps, klass, pb_rs_cb, NULL, 2000000);
            put(&o, "instances=%d\n", pb_rs_list_n);
            for (int i = 0; i < pb_rs_list_n; i++) {
                uint64_t ps = 0;
                if (rm_read_ptr(&h, pb_rs_obj[i] + 0x190, &ps)) continue;
                uint64_t sa = pb_dict_val_addr(&h, ps, "Speed");
                double sv = 0;
                if (sa) rm_read(&h, sa, &sv, 8);
                put(&o, "  %-8s obj=0x%llx ps=0x%llx saddr=0x%llx speed=%.6g\n",
                    pb_rs_id[i], (unsigned long long) pb_rs_obj[i],
                    (unsigned long long) ps, (unsigned long long) sa, sv);
            }
        }
    } else if (!strcmp(cmd, "rolesset")) {
        /* rolesset a1=速度值：把所有 RoleStaticData 真实例的 Speed 改成该值（调试用，不还原） */
        double v = (a1 && a1[0]) ? strtod(a1, NULL) : 280.0;
        uint64_t klass = ms_find_klass2(&h, maps, nmaps, "RoleStaticData", "Game", NULL);
        put(&o, "rolesset klass=0x%llx want=%.6g\n", (unsigned long long) klass, v);
        if (klass) {
            pb_rs_list_n = 0;
            ms_scan_objects(&h, maps, nmaps, klass, pb_rs_cb, NULL, 2000000);
            int ok = 0;
            for (int i = 0; i < pb_rs_list_n; i++) {
                uint64_t ps = 0;
                if (rm_read_ptr(&h, pb_rs_obj[i] + 0x190, &ps)) continue;
                uint64_t sa = pb_dict_val_addr(&h, ps, "Speed");
                if (!sa) continue;
                if (rm_write(&h, sa, &v, 8) == 8) ok++;
            }
            put(&o, "instances=%d wrote=%d\n", pb_rs_list_n, ok);
        }
    } else if (!strcmp(cmd, "obj") || !strcmp(cmd, "objr")) {
        int raw = !strcmp(cmd, "objr");
        uint64_t klass = ms_find_klass2(&h, maps, nmaps, a1, ns, NULL);
        put(&o, "obj %s klass=0x%llx\n", a1, (unsigned long long) klass);
        if (klass) {
            pb_fields_t fs;
            pb_read_fields(&h, klass, &fs);
            int32_t id_off = pb_field_off(&fs, "ID", NULL);
            if (id_off < 0) id_off = pb_field_off(&fs, "_id", NULL);
            if (raw) id_off = -2;
            put(&o, "fields_arr=0x%llx n=%d id_off=0x%x\n",
                (unsigned long long) fs.arr, fs.n, id_off);
            static uint64_t objs[PB_MAXOBJ];
            int want = n > 0 ? (n < PB_MAXOBJ ? n : PB_MAXOBJ) : 6;
            int nk = pb_collect(&h, maps, nmaps, klass, &fs, id_off, 1, objs, want);
            put(&o, "instances=%d\n", nk);
            for (int i = 0; i < nk; i++) pb_dump_obj(&o, &h, objs[i], &fs);
        }
    } else if (!strcmp(cmd, "roles")) {
        uint64_t klass = ms_find_klass2(&h, maps, nmaps, "RoleStaticData", "Game", NULL);
        put(&o, "RoleStaticData klass=0x%llx\n", (unsigned long long) klass);
        if (klass) {
            pb_fields_t fs;
            pb_read_fields(&h, klass, &fs);
            int32_t id_off = pb_field_off(&fs, "ID", NULL);
            int32_t ps_off = pb_field_off(&fs, "PropertySetting", "Propert");
            put(&o, "id_off=0x%x ps_off=0x%x\n", id_off, ps_off);
            static uint64_t objs[PB_MAXOBJ];
            int want = n > 0 ? (n < PB_MAXOBJ ? n : PB_MAXOBJ) : 3;
            int nk = pb_collect(&h, maps, nmaps, klass, &fs, id_off, 1, objs, want);
            put(&o, "instances=%d\n", nk);
            for (int i = 0; i < nk; i++) {
                char id[48] = {0};
                if (id_off >= 0) {
                    uint64_t sp = 0;
                    if (!rm_read_ptr(&h, objs[i] + (uint64_t) id_off, &sp)) ms_read_rstr(&h, sp, id, sizeof(id));
                }
                uint64_t ps = 0;
                if (ps_off >= 0) rm_read_ptr(&h, objs[i] + (uint64_t) ps_off, &ps);
                put(&o, "=== %s obj=0x%llx ps=0x%llx\n", id,
                    (unsigned long long) objs[i], (unsigned long long) ps);
                uint64_t dict = 0;
                if (ps >= 0x1000) rm_read_ptr(&h, ps + 0x18, &dict);
                if (dict >= 0x1000) pb_dump_dict(&o, &h, dict, 40);
            }
        }
    } else if (!strcmp(cmd, "dict")) {
        uint64_t addr = strtoull(a1, NULL, 16);
        if (a2 && a2[0] == '+') {
            uint64_t off = strtoull(a2 + 1, NULL, 16);
            uint64_t p = 0;
            if (rm_read_ptr(&h, addr + off, &p) == 0 && p >= 0x1000) addr = p;
            put(&o, "deref +0x%llx -> 0x%llx\n", (unsigned long long) off, (unsigned long long) addr);
        }
        pb_dump_dict(&o, &h, addr, n > 0 ? n : 64);
    } else if (!strcmp(cmd, "u64")) {
        uint64_t addr = strtoull(a1, NULL, 16);
        uint64_t v = 0;
        if (rm_read_ptr(&h, addr, &v)) {
            put(&o, "u64 @0x%llx read fail\n", (unsigned long long) addr);
        } else {
            char s1[64] = {0}, s2[64] = {0};
            int isstr = ms_read_rstr(&h, v, s1, sizeof(s1)) > 0;
            int isobj = isstr ? 0 : pb_obj_class(&h, v, s2, sizeof(s2));
            put(&o, "u64 @0x%llx = 0x%llx", (unsigned long long) addr, (unsigned long long) v);
            if (isstr) put(&o, " str=\"%s\"", s1);
            else if (isobj) put(&o, " obj:%s", s2);
            float f32; memcpy(&f32, &v, 4);
            put(&o, " i32=%d f32=%.4g\n", (int32_t) (v & 0xffffffffu), (double) f32);
        }
    } else if (!strcmp(cmd, "fscan") || !strcmp(cmd, "fscanin")) {
        /* fscan  a1=浮点值            → 全 rw 段找该 float 值
           fscanin a1=模块名子串 a2=浮点值 → 只在匹配的映射里找 */
        const char *mod = NULL;
        float want = 0;
        if (!strcmp(cmd, "fscanin")) {
            mod = a1;
            want = (float) strtod(a2 && a2[0] ? a2 : "0", NULL);
        } else {
            want = (float) strtod(a1 && a1[0] ? a1 : "0", NULL);
        }
        uint32_t wbits;
        memcpy(&wbits, &want, 4);
        int limit = n > 0 ? n : 4000;
        int hits = 0, scanned = 0;
        static unsigned char sbuf[1 << 20];
        for (int i = 0; i < nmaps && hits < limit; i++) {
            if (maps[i].perms[0] != 'r' || maps[i].perms[1] != 'w') continue;
            if (maps[i].path[0] == '/' &&
                (strstr(maps[i].path, "/dev/") || strstr(maps[i].path, "/dmabuf"))) continue;
            if (mod && mod[0] && !strstr(maps[i].path, mod)) continue;
            uint64_t s = maps[i].start, e = maps[i].end;
            for (uint64_t a = s; a < e && hits < limit; a += sizeof(sbuf) - 4) {
                size_t w = (e - a) < sizeof(sbuf) ? (size_t) (e - a) : sizeof(sbuf);
                ssize_t g = rm_read(&h, a, sbuf, w);
                if (g < 4) continue;
                scanned++;
                for (ssize_t j = 0; j + 4 <= g && hits < limit; j += 4) {
                    uint32_t v;
                    memcpy(&v, sbuf + j, 4);
                    if (v != wbits) continue;
                    put(&o, "0x%llx\n", (unsigned long long) (a + (uint64_t) j));
                    hits++;
                }
            }
        }
        put(&o, "hits=%d chunks=%d want=%.6g\n", hits, scanned, (double) want);
    } else if (!strcmp(cmd, "recheck")) {
        /* recheck a1=文件名 a2=浮点值：逐行读地址，报告当前仍是该值的地址 */
        char path[256];
        snprintf(path, sizeof(path), "%s", a1);
        FILE *fp = fopen(path, "r");
        if (!fp) { put(&o, "recheck open fail %s\n", path); }
        else {
            float want = (float) strtod(a2 && a2[0] ? a2 : "0", NULL);
            uint32_t wbits; memcpy(&wbits, &want, 4);
            char line[64];
            int kept = 0, total = 0;
            while (fgets(line, sizeof(line), fp)) {
                if (line[0] != '0') continue;
                uint64_t addr = strtoull(line, NULL, 16);
                if (addr < 0x1000) continue;
                total++;
                uint32_t v = 0;
                if (rm_read(&h, addr, &v, 4) != 4) continue;
                if (v == wbits) { put(&o, "0x%llx\n", (unsigned long long) addr); kept++; }
            }
            fclose(fp);
            put(&o, "kept=%d total=%d want=%.6g\n", kept, total, (double) want);
        }
    } else if (!strcmp(cmd, "wint")) {
        /* wint a1=地址(hex) a2=整数 */
        uint64_t addr = strtoull(a1, NULL, 16);
        int32_t v = (int32_t) strtol((a2 && a2[0]) ? a2 : "0", NULL, 0);
        int32_t old = 0;
        if (rm_read(&h, addr, &old, 4) != 4) put(&o, "wint read fail\n");
        else {
            int ok = (rm_write(&h, addr, &v, 4) == 4);
            int32_t nv = 0;
            rm_read(&h, addr, &nv, 4);
            put(&o, "wint @0x%llx old=%d -> new=%d ok=%d readback=%d\n",
                (unsigned long long) addr, old, v, ok, nv);
        }
    } else if (!strcmp(cmd, "wdouble")) {
        /* wdouble a1=地址(hex) a2=double 值 —— PropertyValue 的数值是 8 字节 double */
        uint64_t addr = strtoull(a1, NULL, 16);
        double v = a2 && a2[0] ? strtod(a2, NULL) : 0;
        double old = 0;
        if (rm_read(&h, addr, &old, 8) != 8) put(&o, "wdouble read fail\n");
        else {
            int ok = (rm_write(&h, addr, &v, 8) == 8);
            double nv = 0;
            rm_read(&h, addr, &nv, 8);
            put(&o, "wdouble @0x%llx old=%.6g -> new=%.6g ok=%d readback=%.6g\n",
                (unsigned long long) addr, old, v, ok, nv);
        }
    } else if (!strcmp(cmd, "wfloat")) {
        /* wfloat a1=地址(hex) a2=浮点值 */
        uint64_t addr = strtoull(a1, NULL, 16);
        float v = (float) strtod(a2 && a2[0] ? a2 : "0", NULL);
        uint32_t before = 0;
        rm_read(&h, addr, &before, 4);
        float oldf; memcpy(&oldf, &before, 4);
        int ok = (rm_write(&h, addr, &v, 4) == 4);
        uint32_t after = 0;
        rm_read(&h, addr, &after, 4);
        float newf; memcpy(&newf, &after, 4);
        put(&o, "wfloat @0x%llx old=%.6g -> new=%.6g ok=%d\n",
            (unsigned long long) addr, (double) oldf, (double) newf, ok);
    } else if (!strcmp(cmd, "mod")) {
        uint64_t b = rm_module_base(pid, a1, NULL);
        put(&o, "module %s base=0x%llx\n", a1, (unsigned long long) b);
        if (b && a2 && a2[0]) {
            uint64_t addr = strtoull(a2, NULL, 16);
            put(&o, "delta=0x%llx\n", (unsigned long long) (addr - b));
        }
    } else if (!strcmp(cmd, "mapsrw")) {
        /* mapsrw [过滤串]：列出可读写的映射段（含匿名段），用于找 .bss。
           例：mapsrw 不带参数输出全部，a1 给 "libunity" 只看相关段。 */
        const char *filt = (a1 && a1[0] && strcmp(a1, "-")) ? a1 : NULL;
        for (int i = 0; i < nmaps; i++) {
            if (maps[i].perms[0] != 'r' || maps[i].perms[1] != 'w') continue;
            if (filt && !strstr(maps[i].path, filt)) {
                /* 匿名段（无 path）在过滤时也保留，它们往往是 .bss */
                if (maps[i].path[0]) continue;
            }
            put(&o, "%llx-%llx %s %8lluK  %s\n",
                (unsigned long long) maps[i].start,
                (unsigned long long) maps[i].end,
                maps[i].perms,
                (unsigned long long) ((maps[i].end - maps[i].start) / 1024),
                maps[i].path[0] ? maps[i].path : "<anon>");
        }
    } else if (!strcmp(cmd, "fscanar")) {
        /* fscanar a1="起始地址,结束地址"(hex) a2=float 值
           在任意地址区间内扫 float，用于扫 .bss 这类匿名可写段
           （fscanin 只覆盖带文件名的映射，.bss 通常是紧随其后的 <anon>）。 */
        uint64_t s = 0, e = 0;
        if (a1 && a1[0]) {
            char tmp[96];
            snprintf(tmp, sizeof(tmp), "%s", a1);
            char *cm = strchr(tmp, ',');
            if (cm) { *cm = 0; s = strtoull(tmp, NULL, 16); e = strtoull(cm + 1, NULL, 16); }
            else      s = strtoull(tmp, NULL, 16);
        }
        float want = (a2 && a2[0]) ? (float) strtod(a2, NULL) : 1.0f;
        uint32_t wb; memcpy(&wb, &want, 4);
        if (!s || e <= s) { put(&o, "fscanar bad range\n"); }
        else {
            static unsigned char sbuf[1 << 20];
            int hits = 0;
            for (uint64_t a = s; a < e; a += sizeof(sbuf) - 4) {
                size_t w = (e - a) < sizeof(sbuf) ? (size_t) (e - a) : sizeof(sbuf);
                ssize_t g = rm_read(&h, a, sbuf, w);
                if (g < 4) continue;
                for (ssize_t j = 0; j + 4 <= g; j += 4) {
                    uint32_t v; memcpy(&v, sbuf + j, 4);
                    if (v != wb) continue;
                    put(&o, "0x%llx\n", (unsigned long long) (a + (uint64_t) j));
                    if (++hits >= 3000) break;
                }
                if (hits >= 3000) break;
            }
            put(&o, "fscanar hits=%d want=%.6g\n", hits, (double) want);
        }
    } else if (!strcmp(cmd, "fseq")) {
        /* fseq a1=地址文件：检查 addr-8/addr-4/addr 三个 float 是否为 1.0/2.0/3.0
           （游戏倍速查表 [1,2,3] 的指纹）。命中说明 addr 是表尾。 */
        FILE *fp = fopen(a1, "r");
        if (!fp) { put(&o, "fseq open fail %s\n", a1); }
        else {
            char line[64];
            int hit = 0, n = 0;
            while (fgets(line, sizeof(line), fp)) {
                if (line[0] != '0') continue;
                uint64_t a = strtoull(line, NULL, 16);
                if (a < 0x10) continue;
                n++;
                float f1 = 0, f2 = 0, f3 = 0;
                if (rm_read(&h, a - 8, &f1, 4) != 4) continue;
                if (rm_read(&h, a - 4, &f2, 4) != 4) continue;
                if (rm_read(&h, a,     &f3, 4) != 4) continue;
                if (f1 > 0.999f && f1 < 1.001f &&
                    f2 > 1.999f && f2 < 2.001f &&
                    f3 > 2.999f && f3 < 3.001f) {
                    put(&o, "HIT 0x%llx  [1.0][2.0][3.0]\n", (unsigned long long) (a - 8));
                    hit++;
                }
            }
            fclose(fp);
            put(&o, "fseq checked=%d hit=%d\n", n, hit);
        }
    } else if (!strcmp(cmd, "chg")) {
        /* chg a1=候选文件 a2=原值：报告当前值 != 原值 的地址（用于差分定位变量） */
        FILE *fp = fopen(a1, "r");
        if (!fp) { put(&o, "chg open fail %s\n", a1); }
        else {
            float old = (float) strtod(a2 && a2[0] ? a2 : "0", NULL);
            uint32_t obits; memcpy(&obits, &old, 4);
            char line[96];
            int changed = 0, total = 0;
            while (fgets(line, sizeof(line), fp)) {
                if (line[0] != '0') continue;
                uint64_t addr = strtoull(line, NULL, 16);
                if (addr < 0x1000) continue;
                total++;
                uint32_t v = 0;
                if (rm_read(&h, addr, &v, 4) != 4) continue;
                if (v == obits) continue;
                float nf; memcpy(&nf, &v, 4);
                if (!(nf > -1e6f && nf < 1e6f)) continue;
                put(&o, "0x%llx %.6g\n", (unsigned long long) addr, (double) nf);
                changed++;
            }
            fclose(fp);
            put(&o, "changed=%d total=%d old=%.6g\n", changed, total, (double) old);
        }
    } else if (!strcmp(cmd, "hex")) {
        /* hex a1=地址 a2=长度(hex/dec) → 原始内存，每行 16 字节，附 float 解析 */
        uint64_t addr = strtoull(a1, NULL, 16);
        int len = (int) strtol(a2 && a2[0] ? a2 : "100", NULL, 0);
        if (len < 16) len = 16;
        if (len > 1024) len = 1024;
        unsigned char b[1024];
        ssize_t g = rm_read(&h, addr, b, (size_t) len);
        if (g < 0) g = 0;
        for (int i = 0; i + 16 <= g; i += 16) {
            put(&o, "+%03x  ", i);
            for (int k = 0; k < 16; k += 4) {
                uint32_t w; memcpy(&w, b + i + k, 4);
                put(&o, "%08x ", w);
            }
            for (int k = 0; k < 16; k += 4) {
                float f; memcpy(&f, b + i + k, 4);
                put(&o, "%9.4g", (double) f);
            }
            put(&o, "\n");
        }
    } else if (!strcmp(cmd, "frscanin")) {
        /* frscanin a1=模块名子串 a2="min,max"
           扫该模块 rw 映射中 float 落在区间的地址，写到 files/fr.txt。
           用途：两次快照找「持续增长」的全局量 → Unity TimeManager.m_Time，
           再在它旁边就能看到 timeScale。 */
        const char *outp =
            "/storage/emulated/0/Android/data/com.openrubi.luciatools/files/fr.txt";
        FILE *fp = fopen(outp, "w");
        if (!fp) { put(&o, "frscanin open fail %s\n", outp); }
        else {
            float lo = 0, hi = 0;
            if (a2) sscanf(a2, "%f,%f", &lo, &hi);
            if (hi <= lo) hi = lo + 1.0f;
            int hits = 0;
            static unsigned char sbuf[1 << 20];
            for (int i = 0; i < nmaps; i++) {
                if (maps[i].perms[0] != 'r' || maps[i].perms[1] != 'w') continue;
                if (maps[i].path[0] == '/' &&
                    (strstr(maps[i].path, "/dev/") || strstr(maps[i].path, "/dmabuf"))) continue;
                if (a1 && a1[0] && !strstr(maps[i].path, a1)) continue;
                uint64_t s = maps[i].start, e = maps[i].end;
                for (uint64_t a = s; a < e; a += sizeof(sbuf) - 4) {
                    size_t w = (e - a) < sizeof(sbuf) ? (size_t) (e - a) : sizeof(sbuf);
                    ssize_t g = rm_read(&h, a, sbuf, w);
                    if (g < 4) continue;
                    for (ssize_t j = 0; j + 4 <= g; j += 4) {
                        float v; memcpy(&v, sbuf + j, 4);
                        if (!(v >= lo && v <= hi)) continue;
                        fprintf(fp, "0x%llx %.6g\n",
                                (unsigned long long) (a + (uint64_t) j), (double) v);
                        hits++;
                    }
                }
            }
            fclose(fp);
            put(&o, "frscanin hits=%d range=[%.6g,%.6g] mod=%s\n",
                hits, (double) lo, (double) hi, a1 ? a1 : "");
        }
    } else if (!strcmp(cmd, "fscanw")) {
        /* fscanw a1=输出文件 a2=目标float值 n=上限(0=不限)
           扫所有 rw 映射，把命中的 4 字节对齐地址写入文件（不受返回缓冲限制） */
        FILE *fp = fopen(a1, "w");
        if (!fp) { put(&o, "fscanw open fail %s\n", a1); }
        else {
            float want = (float) strtod(a2 && a2[0] ? a2 : "0", NULL);
            uint32_t wbits; memcpy(&wbits, &want, 4);
            int limit = n > 0 ? n : 0;
            int hits = 0, chunks = 0;
            static unsigned char sbuf[1 << 20];
            for (int i = 0; i < nmaps; i++) {
                if (maps[i].perms[0] != 'r' || maps[i].perms[1] != 'w') continue;
                if (maps[i].path[0] == '/' &&
                    (strstr(maps[i].path, "/dev/") || strstr(maps[i].path, "/dmabuf"))) continue;
                uint64_t st = maps[i].start, e = maps[i].end;
                for (uint64_t a = st; a < e; a += sizeof(sbuf) - 4) {
                    if (limit && hits >= limit) break;
                    size_t w = (e - a) < sizeof(sbuf) ? (size_t) (e - a) : sizeof(sbuf);
                    ssize_t g = rm_read(&h, a, sbuf, w);
                    if (g < 4) continue;
                    chunks++;
                    for (ssize_t j = 0; j + 4 <= g; j += 4) {
                        uint32_t v; memcpy(&v, sbuf + j, 4);
                        if (v != wbits) continue;
                        fprintf(fp, "0x%llx\n", (unsigned long long) (a + (uint64_t) j));
                        hits++;
                        if (limit && hits >= limit) break;
                    }
                }
                if (limit && hits >= limit) break;
            }
            fclose(fp);
            put(&o, "fscanw hits=%d chunks=%d want=%.6g file=%s\n",
                hits, chunks, (double) want, a1);
        }
    } else if (!strcmp(cmd, "wlist")) {
        /* wlist a1=地址文件 a2=float值：把文件里所有地址写成该值 */
        FILE *fp = fopen(a1, "r");
        if (!fp) { put(&o, "wlist open fail %s\n", a1); }
        else {
            float v = (float) strtod(a2 && a2[0] ? a2 : "0", NULL);
            char line[96];
            int ok = 0, total = 0;
            while (fgets(line, sizeof(line), fp)) {
                if (line[0] != '0') continue;
                uint64_t addr = strtoull(line, NULL, 16);
                if (addr < 0x1000) continue;
                total++;
                if (rm_write(&h, addr, &v, 4) == 4) ok++;
            }
            fclose(fp);
            put(&o, "wlist ok=%d total=%d val=%.6g\n", ok, total, (double) v);
        }
    } else if (!strcmp(cmd, "fscanr")) {
        /* fscanr a1=输出文件 a2=目标float值
           只扫 libunity.so / libil2cpp.so 数据段附近(base+[0,0x2000000])，秒级完成 */
        FILE *fp = fopen(a1, "w");
        if (!fp) { put(&o, "fscanr open fail %s\n", a1); }
        else {
            float want = (float) strtod(a2 && a2[0] ? a2 : "0", NULL);
            uint32_t wbits; memcpy(&wbits, &want, 4);
            uint64_t bases[2];
            bases[0] = rm_module_base(pid, "libunity.so", NULL);
            bases[1] = rm_module_base(pid, "libil2cpp.so", NULL);
            int hits = 0;
            static unsigned char sbuf[1 << 20];
            for (int m = 0; m < 2; m++) {
                if (!bases[m]) continue;
                uint64_t lo = bases[m], hi = bases[m] + 0x2000000ULL;
                for (int i = 0; i < nmaps; i++) {
                    if (maps[i].perms[0] != 'r' || maps[i].perms[1] != 'w') continue;
                    uint64_t st = maps[i].start > lo ? maps[i].start : lo;
                    uint64_t e  = maps[i].end   < hi ? maps[i].end   : hi;
                    if (st >= e) continue;
                    for (uint64_t a = st; a < e; a += sizeof(sbuf) - 4) {
                        size_t w = (e - a) < sizeof(sbuf) ? (size_t) (e - a) : sizeof(sbuf);
                        ssize_t g = rm_read(&h, a, sbuf, w);
                        if (g < 4) continue;
                        for (ssize_t j = 0; j + 4 <= g; j += 4) {
                            uint32_t v; memcpy(&v, sbuf + j, 4);
                            if (v != wbits) continue;
                            fprintf(fp, "0x%llx\n", (unsigned long long) (a + (uint64_t) j));
                            hits++;
                        }
                    }
                }
            }
            fclose(fp);
            put(&o, "fscanr hits=%d want=%.6g base_u=0x%llx base_i=0x%llx\n",
                hits, (double) want, (unsigned long long) bases[0], (unsigned long long) bases[1]);
        }
    } else if (!strcmp(cmd, "intscan")) {
        /* intscan a1=输出文件：扫高位托管映射，记录 4 字节值 1..3 的地址 */
        FILE *fp = fopen(a1, "w");
        if (!fp) { put(&o, "intscan open fail %s\n", a1); }
        else {
            int hits = 0;
            static unsigned char sbuf[1 << 20];
            for (int i = 0; i < nmaps; i++) {
                if (maps[i].perms[0] != 'r' || maps[i].perms[1] != 'w') continue;
                if (maps[i].start < 0x700000000000ULL) continue;
                uint64_t st = maps[i].start, e = maps[i].end;
                for (uint64_t a = st; a < e; a += sizeof(sbuf) - 4) {
                    size_t w = (e - a) < sizeof(sbuf) ? (size_t) (e - a) : sizeof(sbuf);
                    ssize_t g = rm_read(&h, a, sbuf, w);
                    if (g < 4) continue;
                    for (ssize_t j = 0; j + 4 <= g; j += 4) {
                        uint32_t v; memcpy(&v, sbuf + j, 4);
                        if (v < 1 || v > 3) continue;
                        fprintf(fp, "0x%llx %u\n", (unsigned long long) (a + (uint64_t) j), v);
                        hits++;
                    }
                }
            }
            fclose(fp);
            put(&o, "intscan hits=%d\n", hits);
        }
    } else if (!strcmp(cmd, "iscan")) {
        /* iscan a1=输出文件 a2=值[,值]：扫高位托管映射里的 int32。
           a2 给两个值时，只输出「相邻两个 int32 分别等于这两个值」的地址
           （用于定位界面上成对显示的 18/18 这种计数）。 */
        uint32_t want1 = (uint32_t) strtoul(a2, NULL, 10);
        uint32_t want2 = want1;
        int pair = 0;
        const char *cm = a2 ? strchr(a2, ',') : NULL;
        if (cm) { want2 = (uint32_t) strtoul(cm + 1, NULL, 10); pair = 1; }

        FILE *fp = fopen(a1, "w");
        if (!fp) { put(&o, "iscan open fail %s\n", a1); }
        else {
            int hits = 0;
            static unsigned char sbuf[1 << 20];
            for (int i = 0; i < nmaps; i++) {
                if (maps[i].perms[0] != 'r' || maps[i].perms[1] != 'w') continue;
                if (maps[i].start < 0x700000000000ULL) continue;
                uint64_t st = maps[i].start, e = maps[i].end;
                for (uint64_t a = st; a < e; a += sizeof(sbuf) - 8) {
                    size_t w = (e - a) < sizeof(sbuf) ? (size_t) (e - a) : sizeof(sbuf);
                    ssize_t g = rm_read(&h, a, sbuf, w);
                    if (g < 8) continue;
                    for (ssize_t j = 0; j + 8 <= g; j += 4) {
                        uint32_t v1, v2;
                        memcpy(&v1, sbuf + j, 4);
                        if (v1 != want1) continue;
                        if (pair) {
                            memcpy(&v2, sbuf + j + 4, 4);
                            if (v2 != want2) continue;
                        }
                        fprintf(fp, "0x%llx\n", (unsigned long long) (a + (uint64_t) j));
                        hits++;
                    }
                }
            }
            fclose(fp);
            put(&o, "iscan want=%u,%u pair=%d hits=%d\n", want1, want2, pair, hits);
        }
    } else if (!strcmp(cmd, "owho")) {
        /* owho a1=地址文件：对每个地址向上试探对象头，输出所属类型名与字段偏移。
           用于判断某个数值地址是属于哪个类的哪个字段。 */
        FILE *fp = fopen(a1, "r");
        if (!fp) { put(&o, "owho open fail %s\n", a1); }
        else {
            char line[64];
            int shown = 0;
            while (fgets(line, sizeof(line), fp) && shown < 4000) {
                if (line[0] != '0') continue;
                uint64_t a = strtoull(line, NULL, 16);
                int got = 0;
                for (int off = 0x10; off <= 0x400; off += 8) {
                    if (a < (uint64_t) off) break;
                    uint64_t k = 0;
                    if (rm_read_ptr(&h, a - (uint64_t) off, &k)) continue;
                    if (k < 0x7f0000000000ULL || k > 0x800000000000ULL) continue;
                    char nm[80];
                    uint64_t npp = 0;
                    if (rm_read_ptr(&h, k + 0x10, &npp) || npp < 0x1000) continue;
                    if (pb_cstr(&h, npp, nm, sizeof(nm)) < 2) continue;
                    int ok = 1;
                    for (int i = 0; nm[i]; i++) {
                        unsigned char c = (unsigned char) nm[i];
                        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                              (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '`' || c == '<' || c == '>')) { ok = 0; break; }
                    }
                    if (!ok) continue;
                    put(&o, "0x%llx  %-42s +0x%x\n", (unsigned long long) a, nm, off);
                    got = 1; shown++;
                    break;
                }
                if (!got) { put(&o, "0x%llx  <no klass>\n", (unsigned long long) a); shown++; }
            }
            fclose(fp);
            put(&o, "owho done %d\n", shown);
        }
    } else if (!strcmp(cmd, "iread")) {
        /* iread a1=地址文件 a2=输出文件：读每个地址的 int32 及 +4 的 int32，输出 "addr v1 v2"。
           用于对旧候选集做二次核对（值变了就说明是同一个变量）。 */
        FILE *fp = fopen(a1, "r");
        FILE *op = fopen(a2, "w");
        if (!fp || !op) { put(&o, "iread open fail\n"); if (fp) fclose(fp); if (op) fclose(op); }
        else {
            char line[64];
            int n = 0;
            while (fgets(line, sizeof(line), fp)) {
                if (line[0] != '0') continue;
                uint64_t a = strtoull(line, NULL, 16);
                int32_t v1 = 0, v2 = 0;
                if (rm_read(&h, a, &v1, 4) != 4) continue;
                if (rm_read(&h, a + 4, &v2, 4) != 4) v2 = 0x7fffffff;
                fprintf(op, "0x%llx %d %d\n", (unsigned long long) a, v1, v2);
                n++;
            }
            fclose(fp); fclose(op);
            put(&o, "iread n=%d\n", n);
        }
    } else if (!strcmp(cmd, "seqv")) {
        /* seqv a1=输出文件 a2=低,中,高 值（默认 16,18,20）
           扫描「同一数组里以固定步长排列的 低→中→高」的三个 int，
           用于定位按等级递增的配置表列（如 VIP 各等级的 AutoBattleCount）。 */
        uint32_t lo = 16, mid = 18, hi = 20;
        if (a2 && *a2) {
            char tmp[96];
            snprintf(tmp, sizeof(tmp), "%s", a2);
            char *p1 = strchr(tmp, ','); if (p1) *p1 = 0;
            char *p2 = p1 ? strchr(p1 + 1, ',') : NULL; if (p2) *p2 = 0;
            lo = (uint32_t) strtoul(tmp, NULL, 10);
            if (p1) mid = (uint32_t) strtoul(p1 + 1, NULL, 10);
            if (p2) hi = (uint32_t) strtoul(p2 + 1, NULL, 10);
        }
        FILE *fp = fopen(a1, "w");
        if (!fp) { put(&o, "seqv open fail\n"); }
        else {
            int hits = 0;
            static unsigned char sbuf[1 << 20];
            for (int i = 0; i < nmaps; i++) {
                if (maps[i].perms[0] != 'r' || maps[i].perms[1] != 'w') continue;
                if (maps[i].start < 0x700000000000ULL) continue;
                uint64_t st = maps[i].start, e = maps[i].end;
                for (uint64_t a = st; a < e; a += sizeof(sbuf) - 8) {
                    size_t w = (e - a) < sizeof(sbuf) ? (size_t) (e - a) : sizeof(sbuf);
                    ssize_t g = rm_read(&h, a, sbuf, w);
                    if (g < 16) continue;
                    for (ssize_t j = 4; j + 4 <= g; j += 4) {
                        uint32_t v; memcpy(&v, sbuf + j, 4);
                        if (v != mid) continue;
                        for (ssize_t d = 4; d <= 0x1000; d += 4) {
                            if (j - d < 0 || j + d + 4 > g) break;
                            uint32_t a1v, a3v;
                            memcpy(&a1v, sbuf + j - d, 4);
                            if (a1v != lo) continue;
                            memcpy(&a3v, sbuf + j + d, 4);
                            if (a3v != hi) continue;
                            fprintf(fp, "0x%llx d=0x%x\n",
                                    (unsigned long long) (a + (uint64_t) j), (unsigned) d);
                            hits++;
                            break;
                        }
                    }
                }
            }
            fclose(fp);
            put(&o, "seqv %u,%u,%u hits=%d\n", lo, mid, hi, hits);
        }
    } else if (!strcmp(cmd, "wlisti")) {
        /* wlisti a1=地址文件 a2=整数值：批量写 int32 */
        FILE *fp = fopen(a1, "r");
        if (!fp) { put(&o, "wlisti open fail %s\n", a1); }
        else {
            int32_t v = (int32_t) strtol(a2, NULL, 10);
            char line[64];
            int ok = 0, bad = 0;
            while (fgets(line, sizeof(line), fp)) {
                if (line[0] != '0') continue;
                uint64_t a = strtoull(line, NULL, 16);
                if (rm_write(&h, a, &v, 4) == 4) ok++; else bad++;
            }
            fclose(fp);
            put(&o, "wlisti val=%d ok=%d bad=%d\n", v, ok, bad);
        }
    } else if (!strcmp(cmd, "intinter")) {
        /* intinter a1=文件A a2=文件B：输出两文件同地址但值不同的项 */
        FILE *fa = fopen(a1, "r");
        if (!fa) { put(&o, "intinter open A fail %s\n", a1); }
        else {
            size_t cap = 1 << 20, n = 0;
            pb_iv_t *arr = (pb_iv_t *) malloc(cap * sizeof(pb_iv_t));
            char line[96];
            while (arr && fgets(line, sizeof(line), fa)) {
                if (line[0] != '0') continue;
                char *sp = strchr(line, ' ');
                if (!sp) continue;
                if (n == cap) { cap <<= 1; pb_iv_t *t = (pb_iv_t *) realloc(arr, cap * sizeof(pb_iv_t));
                                if (!t) break; arr = t; }
                arr[n].a = strtoull(line, NULL, 16);
                arr[n].v = (uint32_t) strtoul(sp + 1, NULL, 10);
                n++;
            }
            fclose(fa);
            if (arr) qsort(arr, n, sizeof(pb_iv_t), pb_cmp_iv);
            FILE *fb = fopen(a2, "r");
            int shown = 0;
            if (fb && arr) {
                while (fgets(line, sizeof(line), fb) && shown < 4000) {
                    if (line[0] != '0') continue;
                    char *sp = strchr(line, ' ');
                    if (!sp) continue;
                    uint64_t key = strtoull(line, NULL, 16);
                    uint32_t v2 = (uint32_t) strtoul(sp + 1, NULL, 10);
                    size_t lo = 0, hi = n;
                    while (lo < hi) { size_t mid = (lo + hi) >> 1;
                        if (arr[mid].a < key) lo = mid + 1; else hi = mid; }
                    if (lo < n && arr[lo].a == key && arr[lo].v != v2) {
                        put(&o, "0x%llx %u -> %u\n", (unsigned long long) key, arr[lo].v, v2);
                        shown++;
                    }
                }
            }
            if (fb) fclose(fb);
            free(arr);
            put(&o, "intinter nA=%zu shown=%d\n", n, shown);
        }
    } else {
        put(&o, "unknown cmd %s\n", cmd);
    }

    rm_close(&h);
    return o.len;
}
