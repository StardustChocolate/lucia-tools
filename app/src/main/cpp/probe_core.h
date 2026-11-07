#ifndef PROBE_CORE_H
#define PROBE_CORE_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 调试探针：cmd = cls | obj | dict
 *   cls  a1=类名 a2=命名空间(空则 "Game")
 *   obj  a1=类名 a2=命名空间 n=实例上限
 *   dict a1=字典地址(hex)   n=条目上限
 * 返回写入字节数，<0 出错。 */
int lt_probe(int pid, const char *cmd, const char *a1, const char *a2, int n,
             char *outbuf, int cap);

#ifdef __cplusplus
}
#endif
#endif /* PROBE_CORE_H */
