/*
 * cg_unlock_core.c —— 未获得角色 CG 解锁
 */
#include "cg_unlock_core.h"
#include "mem_scan.h"
#include "rubimem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ================= 路线2：把条目伪装成主线 CG 条件 ================= */

#define SU_MAX_TARGETS 2048
#define SU_NFIELDS 7
#define SU_SNAP_MAGIC "STY1"
#define SU_SNAP_VER 1u

static const uint64_t SU_OFFS[SU_NFIELDS] = {
    0x28, /* Type + IntimacyLV */
    0x30, /* LineRewardID */
    0x38, /* AwakenLV need */
    0x40, /* SceneID */
    0x48, /* ItemID */
    0x60, /* DramaIDs */
    0x70  /* HighLight + IsSafeMode */
};

typedef struct {
    uint64_t obj;
    uint64_t orig[SU_NFIELDS];
    char     id[40];
    int32_t  old_need;
} su_target_t;

typedef struct {
    uint64_t sc_klass;
    const char *roles;
    const char *donor_id;
    uint64_t donor;
    int donor_found;
    int scanned;
    int n_targets;
    su_target_t *targets;
} su_ctx_t;

static int su_role_match(const char *role, const char *csv) {
    if (!role || !csv) return 0;
    size_t rl = strlen(role);
    const char *p = csv;
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == '\t') p++;
        const char *st = p;
        while (*p && *p != ',' && *p != ' ' && *p != '\t') p++;
        size_t len = (size_t) (p - st);
        if (len == rl && strncmp(st, role, rl) == 0) return 1;
        while (*p && *p != ',') p++;
    }
    return 0;
}


/* rolesCsv == "@UNAcquired" 时：从 RoleData._id 统计已拥有角色，
 * 再把 RoleCollectionStaticData 中 RoleID 不在已拥有集合的条目当作未获得。 */
typedef struct {
    char *owned;
    int ownedCap;
    int ownedLen;
} su_owned_t;

static int su_csv_has(const char *csv, const char *id) {
    if (!csv || !id || !id[0]) return 0;
    size_t idl = strlen(id);
    const char *p = csv;
    while (p && *p) {
        while (*p == ',') p++;
        const char *st = p;
        while (*p && *p != ',') p++;
        size_t l = (size_t) (p - st);
        if (l == idl && strncmp(st, id, idl) == 0) return 1;
    }
    return 0;
}

static int su_owned_add(su_owned_t *u, const char *id) {
    if (!id || !id[0] || id[0] != 'H') return 0;
    if (su_csv_has(u->owned, id)) return 0;
    int idl = (int) strlen(id);
    int need = idl + (u->ownedLen ? 1 : 0);
    if (u->ownedLen + need + 1 >= u->ownedCap) return -1;
    if (u->ownedLen) u->owned[u->ownedLen++] = ',';
    memcpy(u->owned + u->ownedLen, id, (size_t) idl);
    u->ownedLen += idl;
    u->owned[u->ownedLen] = 0;
    return 1;
}

static int su_is_hex_id(const char *s) {
    size_t n = strlen(s);
    if (n < 20 || n > 40) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

static int su_owned_cb(rm_handle_t *h, uint64_t obj, void *user) {
    su_owned_t *u = (su_owned_t *) user;
    uint64_t idp = 0, sidp = 0;
    if (rm_read_ptr(h, obj + 0x10, &idp) || idp < 0x1000) return 0;
    char id[64] = {0};
    if (ms_read_rstr(h, idp, id, sizeof(id)) <= 0 || !su_is_hex_id(id)) return 0;
    if (rm_read_ptr(h, obj + 0x18, &sidp) || sidp < 0x1000) return 0;
    char sid[40] = {0};
    if (ms_read_rstr(h, sidp, sid, sizeof(sid)) > 0) su_owned_add(u, sid);
    return 0;
}

typedef struct {
    const char *owned;
    char *out;
    int cap;
    int len;
    int count;
} su_unacq_t;

static int su_unacq_cb(rm_handle_t *h, uint64_t obj, void *user) {
    su_unacq_t *u = (su_unacq_t *) user;
    uint64_t rolep = 0;
    if (rm_read_ptr(h, obj + 0x18, &rolep) || rolep < 0x1000) return 0;
    char role[40] = {0};
    if (ms_read_rstr(h, rolep, role, sizeof(role)) <= 0) return 0;
    if (su_csv_has(u->owned, role)) return 0;
    if (su_csv_has(u->out, role)) return 0;

    int rl = (int) strlen(role);
    int need = rl + (u->len ? 1 : 0);
    if (u->len + need + 1 >= u->cap) return 1;
    if (u->len) u->out[u->len++] = ',';
    memcpy(u->out + u->len, role, (size_t) rl);
    u->len += rl;
    u->out[u->len] = 0;
    u->count++;
    return 0;
}

static int su_collect_unacquired(rm_handle_t *h, rm_map_t *maps, int nmaps,
                                 char *out, int cap) {
    if (!out || cap <= 0) return -1;
    out[0] = 0;

    uint64_t role_klass = ms_find_klass2(h, maps, nmaps, "RoleData", "Game", NULL);
    uint64_t sc_klass = ms_find_klass2(h, maps, nmaps, "RoleCollectionStaticData", "Game", NULL);
    if (!role_klass || !sc_klass) return -1;

    char owned[8192] = {0};
    su_owned_t o;
    memset(&o, 0, sizeof(o));
    o.owned = owned;
    o.ownedCap = sizeof(owned);
    ms_scan_objects(h, maps, nmaps, role_klass, su_owned_cb, &o, 200000);

    su_unacq_t u;
    memset(&u, 0, sizeof(u));
    u.owned = owned;
    u.out = out;
    u.cap = cap;
    ms_scan_objects(h, maps, nmaps, sc_klass, su_unacq_cb, &u, 200000);
    return u.count;
}

static int su_scan_cb(rm_handle_t *h, uint64_t obj, void *user) {
    su_ctx_t *c = (su_ctx_t *) user;
    c->scanned++;

    uint64_t idp = 0, rolep = 0;
    if (rm_read_ptr(h, obj + 0x10, &idp) || idp < 0x1000) return 0;
    if (rm_read_ptr(h, obj + 0x18, &rolep) || rolep < 0x1000) return 0;

    char id[64] = {0}, role[64] = {0};
    if (ms_read_rstr(h, idp, id, sizeof(id)) <= 0) return 0;
    if (ms_read_rstr(h, rolep, role, sizeof(role)) <= 0) return 0;

    if (!c->donor_found && strcmp(id, c->donor_id) == 0) {
        c->donor = obj;
        c->donor_found = 1;
    }

    if (!su_role_match(role, c->roles)) return 0;

    /* 去重（同一个对象可能被重复扫描） */
    for (int i = 0; i < c->n_targets; i++) {
        if (c->targets[i].obj == obj) return 0;
    }
    if (c->n_targets >= SU_MAX_TARGETS) return 0;

    su_target_t *t = &c->targets[c->n_targets++];
    memset(t, 0, sizeof(*t));
    t->obj = obj;
    snprintf(t->id, sizeof(t->id), "%s", id);
    int32_t need = 0;
    if (rm_read(h, obj + 0x38, &need, 4) == 4) t->old_need = need;
    return 0;
}

static int su_open_snap(const char *path, int pid, int count, FILE **out) {
    if (!path || !path[0]) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t ver = SU_SNAP_VER, nf = SU_NFIELDS;
    int32_t p32 = pid;
    if (fwrite(SU_SNAP_MAGIC, 1, 4, f) != 4 ||
        fwrite(&ver, sizeof(ver), 1, f) != 1 ||
        fwrite(&p32, sizeof(p32), 1, f) != 1 ||
        fwrite(&count, sizeof(count), 1, f) != 1 ||
        fwrite(&nf, sizeof(nf), 1, f) != 1) {
        fclose(f);
        return -1;
    }
    *out = f;
    return 0;
}

int lt_cg_unlock(pid_t pid, const char *rolesCsv, const char *donorId,
                    int dry, const char *snapPath, char *out, int cap) {
    if (pid <= 0 || !rolesCsv || !rolesCsv[0] || !donorId || !donorId[0]) return -1;

    rm_handle_t h;
    if (rm_open(&h, pid)) return -2;

    static rm_map_t maps[8192];
    int nmaps = rm_read_maps(pid, maps, 8192);
    if (nmaps <= 0) { rm_close(&h); return -3; }

    uint64_t klass = ms_find_klass2(&h, maps, nmaps, "RoleCollectionStaticData", "Game", NULL);
    if (!klass) { rm_close(&h); return -11; }

    char resolvedRoles[8192] = {0};
    const char *useRoles = rolesCsv;
    int roleCount = -1;
    if (strcmp(rolesCsv, "@UNAcquired") == 0) {
        roleCount = su_collect_unacquired(&h, maps, nmaps, resolvedRoles, sizeof(resolvedRoles));
        if (roleCount <= 0) { rm_close(&h); return -12; }
        useRoles = resolvedRoles;
    }

    su_ctx_t *ctx = (su_ctx_t *) calloc(1, sizeof(su_ctx_t));
    if (!ctx) { rm_close(&h); return -99; }
    ctx->targets = (su_target_t *) calloc(SU_MAX_TARGETS, sizeof(su_target_t));
    if (!ctx->targets) { free(ctx); rm_close(&h); return -99; }
    ctx->sc_klass = klass;
    ctx->roles = useRoles;
    ctx->donor_id = donorId;

    ms_scan_objects(&h, maps, nmaps, klass, su_scan_cb, ctx, 200000);

    if (!ctx->donor_found) { free(ctx->targets); free(ctx); rm_close(&h); return -11; }
    if (ctx->n_targets == 0) { free(ctx->targets); free(ctx); rm_close(&h); return -12; }

    uint64_t donor_vals[SU_NFIELDS];
    for (int k = 0; k < SU_NFIELDS; k++) {
        if (rm_read_ptr(&h, ctx->donor + SU_OFFS[k], &donor_vals[k])) {
            free(ctx->targets); free(ctx); rm_close(&h); return -13;
        }
    }

    for (int i = 0; i < ctx->n_targets; i++) {
        su_target_t *t = &ctx->targets[i];
        for (int k = 0; k < SU_NFIELDS; k++) {
            if (rm_read_ptr(&h, t->obj + SU_OFFS[k], &t->orig[k])) {
                free(ctx->targets); free(ctx); rm_close(&h); return -14;
            }
        }
    }

    int wrote = 0;
    if (!dry) {
        if (snapPath && snapPath[0]) {
            FILE *f = NULL;
            if (su_open_snap(snapPath, pid, ctx->n_targets, &f)) {
                free(ctx->targets); free(ctx); rm_close(&h); return -20;
            }
            for (int i = 0; i < ctx->n_targets; i++) {
                if (fwrite(&ctx->targets[i].obj, sizeof(uint64_t), 1, f) != 1 ||
                    fwrite(ctx->targets[i].orig, sizeof(uint64_t), SU_NFIELDS, f) != SU_NFIELDS) {
                    fclose(f);
                    free(ctx->targets); free(ctx); rm_close(&h); return -20;
                }
            }
            fclose(f);
        }

        for (int i = 0; i < ctx->n_targets; i++) {
            su_target_t *t = &ctx->targets[i];
            if (t->obj == ctx->donor) continue;
            for (int k = 0; k < SU_NFIELDS; k++) {
                if (rm_write(&h, t->obj + SU_OFFS[k], &donor_vals[k], 8) == 8) wrote++;
            }
        }
    }

    int L = snprintf(out, (size_t) cap,
                     "{\"mode\":\"cgUnlock\",\"roles\":\"%s\",\"donor\":\"%s\",\"dry\":%d,"
                     "\"scanned\":%d,\"targets\":%d,\"roleCount\":%d,"
                     "\"donorAddr\":\"0x%llx\",\"wrote\":%d,"
                     "\"snap\":\"%s\",\"items\":[",
                     useRoles, donorId, dry ? 1 : 0, ctx->scanned, ctx->n_targets,
                     roleCount, (unsigned long long) ctx->donor, wrote,
                     snapPath ? snapPath : "");

    int maxShow = ctx->n_targets < 48 ? ctx->n_targets : 48;
    for (int i = 0; i < maxShow; i++) {
        if (L < 0) L = 0;
        if (L >= cap - 160) break;
        su_target_t *t = &ctx->targets[i];
        int32_t new_need = (int32_t) donor_vals[2];
        int w = snprintf(out + L, (size_t) (cap - L),
                         "%s{\"id\":\"%s\",\"obj\":\"0x%llx\",\"oldNeed\":%d,\"newNeed\":%d}",
                         i ? "," : "", t->id, (unsigned long long) t->obj,
                         t->old_need, new_need);
        if (w < 0) break;
        if (w >= cap - L) { L = cap - 3; break; }
        L += w;
    }
    if (L < 0) L = 0;
    if (L > cap - 3) L = cap - 3;
    L += snprintf(out + L, (size_t) (cap - L), "]}");

    free(ctx->targets);
    free(ctx);
    rm_close(&h);
    return L;
}

int lt_cg_restore(pid_t pid, const char *snapPath, char *out, int cap) {
    if (pid <= 0 || !snapPath || !snapPath[0]) return -1;

    FILE *f = fopen(snapPath, "rb");
    if (!f) return -20;

    char magic[4] = {0};
    uint32_t ver = 0, count = 0, nf = 0;
    int32_t p32 = 0;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, SU_SNAP_MAGIC, 4) != 0 ||
        fread(&ver, sizeof(ver), 1, f) != 1 || ver != SU_SNAP_VER ||
        fread(&p32, sizeof(p32), 1, f) != 1 || p32 != (int32_t) pid ||
        fread(&count, sizeof(count), 1, f) != 1 ||
        fread(&nf, sizeof(nf), 1, f) != 1 || nf != SU_NFIELDS) {
        fclose(f);
        return -21;
    }

    rm_handle_t h;
    if (rm_open(&h, pid)) { fclose(f); return -2; }
    static rm_map_t maps[8192];
    int nmaps = rm_read_maps(pid, maps, 8192);
    if (nmaps <= 0) { rm_close(&h); fclose(f); return -3; }
    uint64_t klass = ms_find_klass2(&h, maps, nmaps, "RoleCollectionStaticData", "Game", NULL);
    if (!klass) { rm_close(&h); fclose(f); return -11; }

    int restored = 0, ok_objs = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t obj = 0, vals[SU_NFIELDS];
        if (fread(&obj, sizeof(obj), 1, f) != 1 ||
            fread(vals, sizeof(uint64_t), SU_NFIELDS, f) != SU_NFIELDS) break;

        uint64_t k = 0;
        if (rm_read_ptr(&h, obj, &k) || k != klass) continue;
        ok_objs++;
        for (int j = 0; j < SU_NFIELDS; j++) {
            if (rm_write(&h, obj + SU_OFFS[j], &vals[j], 8) == 8) restored++;
        }
    }
    fclose(f);
    rm_close(&h);

    int L = snprintf(out, (size_t) cap,
                     "{\"mode\":\"cgRestore\",\"pid\":%d,\"snapCount\":%u,"
                     "\"validObjs\":%d,\"restoredFields\":%d,\"snap\":\"%s\"}",
                     (int) pid, count, ok_objs, restored, snapPath);
    return L;
}
