/*
 * cg_unlock_core.h —— 未获得角色 CG 解锁
 */
#ifndef CG_UNLOCK_CORE_H
#define CG_UNLOCK_CORE_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int lt_cg_unlock(pid_t pid, const char *rolesCsv, const char *donorId,
                 int dry, const char *snapPath, char *out, int cap);
int lt_cg_restore(pid_t pid, const char *snapPath, char *out, int cap);

#ifdef __cplusplus
}
#endif
#endif
