#ifndef EQUIP_CORE_H
#define EQUIP_CORE_H
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 导出装备 JSON 到 out(cap)。返回件数, <0 出错(-1 open / -3 找不到klass / -4 无容器) */
int ec_export_equipment(pid_t pid, char *out, int cap);

/* 导出登录账号信息 JSON。返回 1 找到 / 0 未填充 / <0 出错 */
int ec_export_account(pid_t pid, char *out, int cap);

/* 试连接: 找到 EquipmentData 或 il2cpp 基址即视为可读。返回 1 成功 0 失败 */
int ec_can_connect(pid_t pid);

/* 秒杀开关: enable=1 全部觉醒攻击增量改大, 0 还原(20/30)。返回修改条目数, <0 出错。 */
int ec_set_instakill(pid_t pid, int enable);

/* 预热: 后台扫描填充秒杀地址缓存(不修改),使后续开关瞬间完成。返回条目数, <0 出错。 */
int ec_prime_instakill(pid_t pid);

/* 预热: 后台填充导出用 klass 缓存(装备/装备图共用)。返回 1 成功 0 失败。 */
int ec_prime_export(pid_t pid);

#ifdef __cplusplus
}
#endif
#endif
