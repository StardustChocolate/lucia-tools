#ifndef BATTLE_CORE_H
#define BATTLE_CORE_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 战斗内把我方角色（MonsterSetting == null）的 AbilityData.Speed 写为 speed。
   enable=1 开启，enable=0 按快照还原。
   返回处理角色数；<0 出错：-3 定位不到战斗类，-4 当前不在战斗中/没有我方角色。 */
int lt_role_speed_set(pid_t pid, float speed, int enable);

/* 开启期间定时重放目标速度，压制游戏前台运行时的重算覆盖。 */
int lt_role_speed_refresh(void);

/* 上一次设置的速度开关是否仍处于开启状态（同一 pid）。 */
int lt_role_speed_on(pid_t pid);

/* 战斗节奏加速：把行动条推进放大到 mult 倍。enable=0 停止。 */
int lt_battle_speed_set(pid_t pid, float mult, int enable);


/* 单次推进（由常驻线程调用）。 */
int lt_battle_speed_tick(void);

/* 累计成功的行动条加速写入次数（调试/自检用）。 */
long lt_battle_tick_writes(void);

/* 启动常驻压制线程。 */
void lt_battle_ensure_keeper(void);

/* 放开队伍编成界面的「重复战斗次数上限」（改 VIP 配置表里的 AutoBattleCount，不动 VIP 等级）。
   limit 如 999；enable=1 写入，enable=0 按快照还原。
   返回处理行数；<0 出错：-3 定位不到 VIPStaticData，-4 没找到配置行。 */
int lt_repeat_limit_set(pid_t pid, int limit, int enable);

#ifdef __cplusplus
}
#endif
#endif /* BATTLE_CORE_H */
