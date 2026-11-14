/*
 * battle_core.c —— 战斗内数值修改
 *
 * 结构（运行时实证，Unity IL2CPP / metadata v31）：
 *   Game.BattleRoleData  +0x28 = AbilityData*,  +0x38 = ID("1-0-3"),
 *                        +0x40 = Camp(int),     +0x80 = MonsterSetting*
 *   我方角色 MonsterSetting == null，敌方不为空（怪物/PVP 都适用）。
 *   Game.AbilityData     +0x28 = Speed(float)，+0x14 MaxHP, +0x1c Attack …
 *
 * 速度修改直接写 AbilityData.Speed（战斗内最终数值），并保留快照用于还原。
 */
#include "battle_core.h"
#include "mem_scan.h"
#include "rubimem.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

static void bt_ensure_keeper(void);

#define BT_MAXROLE 256

typedef struct {
    uint64_t ability;      /* Game.AbilityData 实例 */
    float    old_speed;    /* AbilityData+0x28 的 float（展示副本） */
    uint64_t pspeed;       /* PropertySetting 克隆字典里 Speed 的 double 地址，0=没有 */
    double   old_pspeed;   /* 该 double 的原始值 */
} bt_snap_t;

typedef struct {
    uint64_t *arr;
    int n;
    int max;
} bt_collect_t;

static pid_t    bt_pid = 0;
static int      bt_n = 0;
static int      bt_on = 0;
static int32_t  bt_off_sp = 0x28;
static float    bt_target_speed = 280.0f;
static bt_snap_t bt_snap[BT_MAXROLE];

/* ---- 类字段表（自校验探测） ---- */

static int bt_cstr(rm_handle_t *h, uint64_t p, char *out, int cap) {
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

/*
 * 在 PropertySetting 的活副本克隆字典里查某个键，返回它的可写 double 地址。
 *
 * 结构与秒杀用的 ec_clone_attack_addr() 完全一致（实战验证过）：
 *   PropertySetting(+0x18) -> _PropertyCloneValueMap
 *   dict+0x18 = entries 指针, dict+0x20 = count
 *   entries+0x18 = 容量, entries+0x20 起每项 0x18 字节：{ hash, key*, value* }
 *   PropertyValue 的数值在 value+0x18（double，8 字节）
 *
 * 这是行动条真正读取的数值：AbilityData.Speed 只是它的展示副本，
 * 只改 AbilityData.Speed 战斗里完全没反应（实测）。
 */
static uint64_t bt_clone_val_addr(rm_handle_t *h, uint64_t ps, const char *key) {
    if (ps < 0x1000) return 0;
    uint64_t cm = 0;
    if (rm_read_ptr(h, ps + 0x18, &cm)) return 0;
    if (cm < 0x1000) return 0;
    uint64_t ent = 0; int32_t cnt = 0;
    if (rm_read_ptr(h, cm + 0x18, &ent)) return 0;
    if (rm_read(h, cm + 0x20, &cnt, 4) != 4) return 0;
    if (ent < 0x1000 || cnt <= 0 || cnt > 128) return 0;
    int32_t alen = 0;
    if (rm_read(h, ent + 0x18, &alen, 4) != 4) return 0;
    if (alen <= 0 || alen > 128) return 0;
    for (int x = 0; x < alen; x++) {
        uint64_t ep = ent + 0x20 + (uint64_t) x * 0x18;
        uint64_t kp = 0, pv = 0;
        if (rm_read_ptr(h, ep + 8, &kp)) continue;
        if (rm_read_ptr(h, ep + 0x10, &pv)) continue;
        if (kp < 0x1000 || pv < 0x1000) continue;
        /* 字典的 key 是 Il2CppString 对象（+0x10 长度 / +0x14 UTF-16），
           不能当裸 C 字符串读，否则永远匹配不上。 */
        char ky[40];
        if (ms_read_rstr(h, kp, ky, sizeof(ky)) <= 0) continue;
        if (!strcmp(ky, key)) return pv + 0x18;
    }
    return 0;
}

/* ---- 角色基础速度倍率（面板可见 / 非战斗可改） ----
 *
 * RoleStaticData(+0x190) -> PropertySetting(+0x18) -> _PropertyCloneValueMap["Speed"]
 *
 * 注意这里存的是**成长倍率**（1.0~1.2），不是绝对速度：
 *   面板显示的速度 = 100 × 倍率 + 装备/印记加成
 *   例：倍率 1.16 → 面板 116；倍率 2.8 → 面板 280（+装备加成）
 * 而战斗内的 `AbilityData.PropertySetting["Speed"]` 是绝对速度（如 130）。
 * 两者是两套数据，所以要同时改：面板靠这条、战斗即时值靠那条。
 *
 * 这条数据属于角色的养成配置（和秒杀改 AttackValue 同一层），
 * 所以非战斗状态也能改，改完切一下角色面板就能看到。
 */
#define BT_MAXBASE 512

static uint64_t bt_base_addr[BT_MAXBASE];
static double   bt_base_old[BT_MAXBASE];
static int      bt_base_n = 0;

static int bt_base_cb(rm_handle_t *h, uint64_t obj, void *user) {
    (void) user;
    if (bt_base_n >= BT_MAXBASE) return 1;
    uint64_t idp = 0;
    if (rm_read_ptr(h, obj + 0x10, &idp)) return 0;
    char id[16];
    if (ms_read_rstr(h, idp, id, sizeof(id)) < 4) return 0;
    if (id[0] != 'H') return 0;
    for (int k = 1; id[k]; k++)
        if (id[k] < '0' || id[k] > '9') return 0;   /* 只认 H### 形式的角色 ID */
    uint64_t ps = 0;
    if (rm_read_ptr(h, obj + 0x190, &ps) || ps < 0x1000) return 0;
    uint64_t sa = bt_clone_val_addr(h, ps, "Speed");
    if (!sa) return 0;
    double v = 0;
    if (rm_read(h, sa, &v, 8) != 8) return 0;
    if (!(v > 0.0 && v < 100.0)) return 0;          /* 倍率合理区间 */
    bt_base_addr[bt_base_n] = sa;
    bt_base_old[bt_base_n] = v;
    bt_base_n++;
    return 0;
}

static int bt_base_collect(rm_handle_t *h, rm_map_t *maps, int nmaps) {
    uint64_t rk = ms_find_klass2(h, maps, nmaps, "RoleStaticData", "Game", NULL);
    if (!rk) return -3;
    bt_base_n = 0;
    ms_scan_objects(h, maps, nmaps, rk, bt_base_cb, NULL, 2000000);
    return bt_base_n;
}

static int bt_field_at(rm_handle_t *h, uint64_t e, char *name, int32_t *off) {
    uint64_t np = 0;
    if (rm_read_ptr(h, e, &np) || np < 0x1000) return 0;
    if (bt_cstr(h, np, name, 40) < 1) return 0;
    int32_t o = 0;
    if (rm_read(h, e + 0x18, &o, 4) != 4) return 0;
    if (o < -1 || o > 0x4000) return 0;
    if (off) *off = o;
    return 1;
}

static uint64_t bt_fields_arr(rm_handle_t *h, uint64_t klass) {
    for (int off = 0x40; off <= 0xb0; off += 8) {
        uint64_t p = 0;
        if (rm_read_ptr(h, klass + (uint64_t) off, &p) || p < 0x10000) continue;
        char n1[40], n2[40];
        int32_t o1 = -1, o2 = -1;
        if (!bt_field_at(h, p, n1, &o1)) continue;
        if (!bt_field_at(h, p + 0x20, n2, &o2)) continue;
        if (o1 < 0 && o2 < 0) continue;
        return p;
    }
    return 0;
}

static int32_t bt_find_field(rm_handle_t *h, uint64_t klass, const char *name) {
    if (!klass) return -1;
    uint64_t arr = bt_fields_arr(h, klass);
    if (!arr) return -1;
    for (int i = 0; i < 96; i++) {
        char nm[40];
        int32_t off = 0;
        if (!bt_field_at(h, arr + (uint64_t) i * 0x20, nm, &off)) break;
        if (!strcmp(nm, name)) return off;
    }
    return -1;
}

static int bt_collect_cb(rm_handle_t *h, uint64_t obj, void *user) {
    (void) h;
    bt_collect_t *c = (bt_collect_t *) user;
    if (c->n >= c->max) return 1;
    c->arr[c->n++] = obj;
    return 0;
}

/* 扫描所有 BattleRoleData 实例，收集「我方」AbilityData 地址与原始速度。
 *
 * 注意：内存里会残留前几场战斗的角色对象（实测同一时刻可有 6 批以上），
 * 之前用「出现次数最多的 BattleData」猜当前战斗，结果稳定选中残留批，
 * 导致写入根本没落到当前战斗的角色上。这里不再猜，直接覆盖全部我方实例：
 * 残留对象不参与战斗，多写几个完全无害，但能保证当前这批一定被改到。
 */
static int bt_collect(rm_handle_t *h, rm_map_t *maps, int nmaps) {
    uint64_t rk = ms_find_klass2(h, maps, nmaps, "BattleRoleData", "Game", NULL);
    uint64_t ak = ms_find_klass2(h, maps, nmaps, "AbilityData", "Game", NULL);
    if (!rk || !ak) return -3;

    int32_t off_ab  = bt_find_field(h, rk, "AbilityData");
    int32_t off_mon = bt_find_field(h, rk, "MonsterSetting");
    int32_t off_sp  = bt_find_field(h, ak, "Speed");
    if (off_ab  < 0) off_ab  = 0x28;
    if (off_mon < 0) off_mon = 0x80;
    if (off_sp  < 0) off_sp  = 0x28;
    bt_off_sp = off_sp;

    static uint64_t objs[16384];
    bt_collect_t c;
    c.arr = objs; c.n = 0; c.max = 16384;
    ms_scan_objects(h, maps, nmaps, rk, bt_collect_cb, &c, 1000000);

    int n = 0;
    for (int i = 0; i < c.n && n < BT_MAXROLE; i++) {
        uint64_t obj = objs[i];
        uint64_t mon = 1, ab = 0;
        if (rm_read_ptr(h, obj + (uint64_t) off_mon, &mon)) continue;
        if (mon) continue;                                    /* 敌方 */
        if (rm_read_ptr(h, obj + (uint64_t) off_ab, &ab)) continue;
        if (ab < 0x1000) continue;
        uint64_t akk = 0;
        if (rm_read_ptr(h, ab, &akk) || akk != ak) continue;   /* 必须是 AbilityData */
        float sp = 0;
        if (rm_read(h, ab + (uint64_t) off_sp, &sp, 4) != 4) continue;
        if (!(sp > 0.0f && sp < 100000.0f)) continue;
        int dup = 0;
        for (int k = 0; k < n; k++) if (bt_snap[k].ability == ab) { dup = 1; break; }
        if (dup) continue;

        /* 真正参与行动条计算的是 PropertySetting 活副本里的 Speed(double)。
           同一份 PropertyValue 可能被多个 AbilityData 共享，只登记一次。 */
        uint64_t ps = 0, psa = 0;
        double psv = 0;
        if (!rm_read_ptr(h, ab + 0x30, &ps) && ps > 0x1000) {
            psa = bt_clone_val_addr(h, ps, "Speed");
            if (psa) {
                if (rm_read(h, psa, &psv, 8) != 8) psa = 0;
                else if (!(psv > 0.0 && psv < 100000.0)) psa = 0;
            }
        }
        if (psa) {
            for (int k = 0; k < n; k++)
                if (bt_snap[k].pspeed == psa) { psa = 0; break; }
        }

        bt_snap[n].ability    = ab;
        bt_snap[n].old_speed  = sp;
        bt_snap[n].pspeed     = psa;
        bt_snap[n].old_pspeed = psv;
        n++;
    }
    bt_n = n;
    return n;
}

/* 快速重放：把目标速度重新写到缓存地址，供 Java 层定时压制。
   游戏在前台运行时会周期性按静态数据重算 AbilityData，单次写入会被覆盖，
   所以开启期间需要每隔几十毫秒重写一次。 */
int lt_role_speed_refresh(void) {
    if (!bt_on || bt_n <= 0 || bt_pid <= 0) return 0;
    rm_handle_t h;
    if (rm_open(&h, bt_pid)) return -1;
    double dv = (double) bt_target_speed;
    int done = 0;
    for (int i = 0; i < bt_n; i++) {
        int ok = 0;
        if (bt_snap[i].pspeed &&
            rm_write(&h, bt_snap[i].pspeed, &dv, 8) == 8) ok = 1;
        if (rm_write(&h, bt_snap[i].ability + (uint64_t) bt_off_sp,
                     &bt_target_speed, 4) == 4) ok = 1;
        done += ok;
    }
    rm_close(&h);
    return done;
}

/*
 * 把当前战斗里我方角色的速度写为 speed（enable=1）或按快照还原（enable=0）。
 * 返回处理角色数，<0 出错（-3 找不到战斗类 / -4 不在战斗中或没有我方角色）。
 */
int lt_role_speed_set(pid_t pid, float speed, int enable) {
    rm_handle_t h;
    if (rm_open(&h, pid)) return -1;
    static rm_map_t maps[8192];
    int nmaps = rm_read_maps(pid, maps, 8192);
    if (nmaps <= 0) { rm_close(&h); return -2; }

    /* 还原：基础倍率 + 战斗内绝对速度都写回原值 */
    if (!enable) {
        int done = 0;
        for (int i = 0; i < bt_base_n; i++)
            if (rm_write(&h, bt_base_addr[i], &bt_base_old[i], 8) == 8) done++;
        if (bt_pid == pid) {
            for (int i = 0; i < bt_n; i++) {
                int ok = 0;
                if (bt_snap[i].pspeed &&
                    rm_write(&h, bt_snap[i].pspeed, &bt_snap[i].old_pspeed, 8) == 8) ok = 1;
                if (rm_write(&h, bt_snap[i].ability + (uint64_t) bt_off_sp,
                             &bt_snap[i].old_speed, 4) == 4) ok = 1;
                done += ok;
            }
        }
        rm_close(&h);
        bt_on = 0;
        bt_pid = 0;
        bt_base_n = 0;
        return done;
    }

    /* 1) 角色基础倍率：面板可见、非战斗也能改。
          面板速度 = 100 × 倍率 + 装备加成，所以倍率 = speed/100。 */
    int done = 0;
    int bn  = bt_base_collect(&h, maps, nmaps);
    if (bn < 0) bn = 0;
    double mult = (double) speed / 100.0;
    if (mult < 0.1 || mult > 50.0) mult = 2.8;
    for (int i = 0; i < bt_base_n; i++)
        if (rm_write(&h, bt_base_addr[i], &mult, 8) == 8) done++;

    /* 2) 战斗内的绝对速度（在战斗中才有对象，找不到不算失败） */
    int n = bt_collect(&h, maps, nmaps);
    if (n > 0) {
        for (int i = 0; i < bt_n; i++) {
            float  v  = speed;
            double dv = (double) speed;
            int ok = 0;
            if (bt_snap[i].pspeed && rm_write(&h, bt_snap[i].pspeed, &dv, 8) == 8) ok = 1;
            if (rm_write(&h, bt_snap[i].ability + (uint64_t) bt_off_sp, &v, 4) == 4) ok = 1;
            done += ok;
        }
    }
    rm_close(&h);

    if (done <= 0) return -4;
    bt_target_speed = speed;
    bt_pid = pid;
    bt_on = 1;
    bt_ensure_keeper();
    return done;
}

int lt_role_speed_on(pid_t pid) { return (bt_pid == pid && bt_on) ? 1 : 0; }


/* Battle speed: patch BattlePlayerSetting.NowSpeed / GetTimeScaleByLevel
 * entry points to return 9.0. PropertySetting[Speed] stays untouched. */
static long bt_tick_writes = 0;

/* 9x battle speed: patch BattlePlayerSetting.get_NowSpeed() and
 * GetTimeScaleByLevel() to return 9.0 directly. This changes the real
 * battle playback multiplier without touching PropertySetting[Speed]. */
#define BT_PATCH_MAX 4
#define BT_PATCH_LEN 8

typedef struct {
    uint64_t addr;
    unsigned char orig[BT_PATCH_LEN];
} bt_code_snap_t;

static bt_code_snap_t bt_code_snap[BT_PATCH_MAX];
static int bt_code_n = 0;
static pid_t bt_battle_pid = 0;
static float bt_battle_mult = 9.0f;

static const unsigned char bt_nine_code[BT_PATCH_LEN] = {
    0x00, 0x50, 0x24, 0x1e,   /* fmov s0, #9.0 */
    0xc0, 0x03, 0x5f, 0xd6    /* ret */
};

/*  Il2CppClass+0x98 = MethodInfo* +0x120  16  = 
 * MethodInfo+0x00 = +0x18 =  C +0x20 =  class */
static uint64_t bt_find_method(rm_handle_t *h, uint64_t klass, const char *want) {
    if (klass < 0x1000) return 0;
    uint64_t methods = 0;
    if (rm_read_ptr(h, klass + 0x98, &methods) || methods < 0x1000) return 0;
    uint16_t mc = 0;
    if (rm_read(h, klass + 0x120, &mc, 2) != 2 || mc == 0 || mc > 1024) return 0;
    for (int i = 0; i < mc; i++) {
        uint64_t mi = 0;
        if (rm_read_ptr(h, methods + (uint64_t) i * 8, &mi) || mi < 0x1000) continue;
        uint64_t owner = 0, namep = 0, code = 0;
        if (rm_read_ptr(h, mi + 0x20, &owner) || owner != klass) continue;
        if (rm_read_ptr(h, mi + 0x18, &namep)) continue;
        char nm[64];
        if (bt_cstr(h, namep, nm, sizeof(nm)) < 1 || strcmp(nm, want) != 0) continue;
        if (rm_read_ptr(h, mi, &code) || code < 0x1000) continue;
        return code;
    }
    return 0;
}

static int bt_patch_one(rm_handle_t *h, uint64_t code) {
    if (!code) return 0;
    for (int i = 0; i < bt_code_n; i++) {
        if (bt_code_snap[i].addr != code) continue;
        unsigned char cur[BT_PATCH_LEN];
        if (rm_read(h, code, cur, sizeof(cur)) != BT_PATCH_LEN) return 0;
        return memcmp(cur, bt_nine_code, BT_PATCH_LEN) == 0;
    }
    if (bt_code_n >= BT_PATCH_MAX) return 0;
    unsigned char old[BT_PATCH_LEN];
    if (rm_read(h, code, old, sizeof(old)) != BT_PATCH_LEN) return 0;
    if (rm_write(h, code, bt_nine_code, BT_PATCH_LEN) != BT_PATCH_LEN) return 0;
    unsigned char chk[BT_PATCH_LEN];
    if (rm_read(h, code, chk, sizeof(chk)) != BT_PATCH_LEN ||
        memcmp(chk, bt_nine_code, BT_PATCH_LEN) != 0) return 0;
    bt_code_snap[bt_code_n].addr = code;
    memcpy(bt_code_snap[bt_code_n].orig, old, BT_PATCH_LEN);
    bt_code_n++;
    return 1;
}

int lt_battle_speed_set(pid_t pid, float mult, int enable) {
    if (enable) {
        if (mult < 1.0f) mult = 9.0f;
        rm_handle_t h;
        if (rm_open(&h, pid)) return -1;
        static rm_map_t maps[8192];
        int n = rm_read_maps(pid, maps, 8192);
        if (n <= 0) { rm_close(&h); return -2; }
        uint64_t klass = ms_find_klass2(&h, maps, n, "BattlePlayerSetting", "Game", NULL);
        if (!klass) { rm_close(&h); return -3; }
        uint64_t a = bt_find_method(&h, klass, "get_NowSpeed");
        uint64_t b = bt_find_method(&h, klass, "GetTimeScaleByLevel");
        if (!a && !b) { rm_close(&h); return -4; }
        int done = 0;
        if (a) done += bt_patch_one(&h, a);
        if (b) done += bt_patch_one(&h, b);
        rm_close(&h);
        if (done <= 0) return -5;
        bt_battle_pid = pid;
        bt_battle_mult = mult;
        bt_tick_writes = done;
        return done;
    }

    if (bt_code_n <= 0) { bt_battle_pid = 0; bt_tick_writes = 0; return 1; }
    if (bt_battle_pid != pid) { bt_code_n = 0; bt_battle_pid = 0; bt_tick_writes = 0; return 1; }
    rm_handle_t h;
    if (rm_open(&h, pid)) return -1;
    int done = 0;
    for (int i = 0; i < bt_code_n; i++)
        if (rm_write(&h, bt_code_snap[i].addr, bt_code_snap[i].orig, BT_PATCH_LEN) == BT_PATCH_LEN) done++;
    rm_close(&h);
    bt_code_n = 0;
    bt_battle_pid = 0;
    bt_tick_writes = 0;
    return done;
}

int lt_battle_speed_tick(void) { return bt_code_n; }


/* ---- 常驻压制线程 ----
 * 单次写入在游戏前台运行时可能被重算覆盖，且新战斗会创建新对象。
 * 这里用一条低频线程持续重放速度写入 + 推进行动条。
 */
static pthread_t bt_keep_tid;
static int       bt_keep_started = 0;
static volatile int bt_keep_run = 1;

static void *bt_keep_thread(void *unused) {
    (void) unused;
    int tick = 0;
    while (bt_keep_run) {
        /* 游戏不会每帧重算 PropertySetting，50ms 重放足够保险；
           9 倍速的补扫自带约 2 秒节流，避免高频跨进程写拖慢游戏。 */
        if ((tick & 1) == 0) lt_role_speed_refresh();
        lt_battle_speed_tick();
        tick++;
        usleep(25000);
    }
    return NULL;
}

static void bt_ensure_keeper(void) {
    if (bt_keep_started) return;
    bt_keep_started = 1;
    pthread_t t;
    if (pthread_create(&t, NULL, bt_keep_thread, NULL) == 0) {
        bt_keep_tid = t;
        pthread_detach(t);
    } else {
        bt_keep_started = 0;
    }
}

void lt_battle_ensure_keeper(void) { bt_ensure_keeper(); }

/* ---- 重复战斗次数上限 ----
 *
 * ★ 关键结论（实测定位）：队伍编成界面「重复战斗次数上限」的 18 **不是**
 *   RepeatBattleSetting 的任何字段（改 Count/LimitedCount 都不影响界面，
 *   而把 Count 写 999 会让游戏真的去排 999 场请求，卡在转圈圈里）。
 *   它来自 VIP 配置表：Game.VIPStaticData 的一行 = 一个 VIP 等级，
 *   行内 +0x40 = AutoBattleCount，VIP4=18 / VIP5=20 / VIP6=22 —— 这正好解释了
 *   「不同人 16/18/20」。整张表是连续数组，元素步长 0xC0，按等级倒序排列。
 *
 * 所以这里只改配置表里的 AutoBattleCount（不改 VIP 等级、不改 RepeatBattleSetting），
 * 界面上的「x/18」立刻变成「x/999」，滑块上限跟着放开。
 * 内存改动不持久，重启游戏即恢复；关闭时按快照写回原值。
 */
#define RP_MAX 64
static uint64_t rp_addr[RP_MAX];
static int32_t  rp_old[RP_MAX];
static int      rp_n = 0;

int lt_repeat_limit_set(pid_t pid, int limit, int enable) {
    if (limit <= 0 || limit > 100000) return -5;
    rm_handle_t h;
    if (rm_open(&h, pid)) return -1;

    if (!enable) {
        int done = 0;
        for (int i = 0; i < rp_n; i++)
            if (rm_write(&h, rp_addr[i], &rp_old[i], 4) == 4) done++;
        rm_close(&h);
        rp_n = 0;
        return done > 0 ? done : 1;
    }

    static rm_map_t maps[8192];
    int nmaps = rm_read_maps(pid, maps, 8192);
    if (nmaps <= 0) { rm_close(&h); return -2; }

    uint64_t vk = ms_find_klass2(&h, maps, nmaps, "VIPStaticData", "Game", NULL);
    if (!vk) { rm_close(&h); return -3; }

    static uint64_t objs[4096];
    bt_collect_t c;
    c.arr = objs; c.n = 0; c.max = 4096;
    ms_scan_objects(&h, maps, nmaps, vk, bt_collect_cb, &c, 200000);

    rp_n = 0;
    for (int i = 0; i < c.n && rp_n < RP_MAX; i++) {
        uint64_t o = objs[i];
        uint64_t kk = 0;
        if (rm_read_ptr(&h, o, &kk) || kk != vk) continue;      /* 回验对象头 */
        int32_t lv = 0, cnt = 0;
        if (rm_read(&h, o + 0x10, &lv, 4) != 4) continue;
        if (lv < 0 || lv > 30) continue;                         /* VIP 等级 */
        uint64_t s1 = 0, s2 = 0;
        if (rm_read_ptr(&h, o + 0x18, &s1) || s1 < 0x1000) continue;  /* 名字字符串 */
        if (rm_read_ptr(&h, o + 0x28, &s2) || s2 < 0x1000) continue;  /* 描述字符串 */
        if (rm_read(&h, o + 0x40, &cnt, 4) != 4) continue;
        if (cnt < 0 || cnt > 100000) continue;
        rp_addr[rp_n] = o + 0x40;
        rp_old[rp_n]  = cnt;
        rp_n++;
    }

    int done = 0;
    for (int i = 0; i < rp_n; i++)
        if (rm_write(&h, rp_addr[i], &limit, 4) == 4) done++;
    rm_close(&h);
    return done > 0 ? done : -4;
}

long lt_battle_tick_writes(void) { return bt_tick_writes; }
