/*
 * equip_core.c —— LuciaTools 装备导出核心(JNI 用)
 * 整合: find_klass(定位 EquipmentData/Container/StaticData) + 容器枚举 + 静态数据关联。
 * 输出: JSON 字符串 [{staticId,part,equipLv,strengthen,classLv,set,mainProp,subProps[]}]
 * 仅依赖 rubimem.c + libc。同 uid 即可读游戏内存,无需 root。
 */
#include "rubimem.h"
#include "mem_scan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#define EC_BUF (1<<20)

static const char *PART[] = {"Weapon","Body","Head","Necklace","Ring","Shoes","All","-"};

/* ---- 读 il2cpp System.String -> utf8 ---- */
/* 实现已抽取到 mem_scan.c，装备导出与 CG 解锁共用 */
#define ec_rstr ms_read_rstr

/* PropertyValue -> "Type=Value": 整数原样, 小数→保留. 同时回传是否百分比 */
static void ec_prop(rm_handle_t*h,uint64_t pv,char*o,int n){
    o[0]=0; if(pv<0x1000)return;
    uint64_t pt=0; double v=0;
    rm_read_ptr(h,pv+0x10,&pt); rm_read(h,pv+0x18,&v,8);
    char ty[64]; ec_rstr(h,pt,ty,sizeof(ty)); if(!ty[0])return;
    if(v==(double)(long long)v) snprintf(o,n,"%s=%lld",ty,(long long)v);
    else snprintf(o,n,"%s=%.6g",ty,v);
}

/* JSON 字符串转义(只处理 \" \\ 控制符) */
static void json_esc(const char*in,char*out,int cap){
    int k=0; for(const char*p=in;*p&&k<cap-2;p++){
        unsigned char c=*p;
        if(c=='"'||c=='\\'){ out[k++]='\\'; out[k++]=c; }
        else if(c<0x20){ /* skip control */ }
        else out[k++]=c;
    }
    out[k]=0;
}

/* ---- 找所有匹配 C 字符串地址 ---- */
/* 实现已抽取到 mem_scan.c */
#define ec_find_all_cstr ms_find_all_cstr

/* ---- 定位 Il2CppClass*: name@+0x10 命中 name 串之一 且 ns@+0x18 指向 ns 串 ---- */
/* 实现已抽取到 mem_scan.c */
#define ec_find_klass ms_find_klass

/* 一次内存遍历同时定位 3 个类(EquipmentData/Container/StaticData)，避免 3 趟全扫。 */
static void ec_find_klasses(rm_handle_t*h,rm_map_t*maps,int n,
                            uint64_t*kd,uint64_t*kc,uint64_t*ks){
    *kd=*kc=*ks=0;
    /* 各类名字符串地址集合 */
    uint64_t sd[256],sc[256],ss[256];
    int nd=ec_find_all_cstr(h,maps,n,"EquipmentData",sd,256);
    int nc=ec_find_all_cstr(h,maps,n,"EquipmentContainer",sc,256);
    int ns_=ec_find_all_cstr(h,maps,n,"EquipmentStaticData",ss,256);
    const char*GAME="Game"; size_t glen=4;
    static unsigned char buf[EC_BUF];
    for(int i=0;i<n;i++){
        if(maps[i].perms[0]!='r'||maps[i].perms[1]!='w')continue;
        if(maps[i].path[0]=='/'&&(strstr(maps[i].path,"/dev/")||strstr(maps[i].path,"/dmabuf")))continue;
        uint64_t a0=maps[i].start,e=maps[i].end;
        for(uint64_t a=a0;a<e;a+=sizeof(buf)-64){
            size_t w=(e-a)<sizeof(buf)?(size_t)(e-a):sizeof(buf);
            ssize_t g=rm_read(h,a,buf,w); if(g<0x20)continue;
            for(ssize_t j=0;j+0x20<=g;j+=8){
                if(*kd&&*kc&&*ks)return;
                uint64_t pn; memcpy(&pn,buf+j+0x10,8);
                int hitD=0,hitC=0,hitS=0;
                if(!*kd) for(int k=0;k<nd;k++) if(pn==sd[k]){hitD=1;break;}
                if(!*kc&&!hitD) for(int k=0;k<nc;k++) if(pn==sc[k]){hitC=1;break;}
                if(!*ks&&!hitD&&!hitC) for(int k=0;k<ns_;k++) if(pn==ss[k]){hitS=1;break;}
                if(!hitD&&!hitC&&!hitS)continue;
                uint64_t pns; memcpy(&pns,buf+j+0x18,8);
                char nsbuf[8]={0}; if(rm_read(h,pns,nsbuf,glen+1)!=(ssize_t)glen+1)continue;
                if(memcmp(nsbuf,GAME,glen)!=0||nsbuf[glen]!=0)continue;
                uint64_t k2=a+j;
                if(hitD&&!*kd)*kd=k2;
                else if(hitC&&!*kc)*kc=k2;
                else if(hitS&&!*ks)*ks=k2;
            }
        }
    }
}

#define EC_MAXCAT 4096
static char ec_cid[EC_MAXCAT][40]; static int ec_clv[EC_MAXCAT]; static int ec_cpart[EC_MAXCAT]; static int ec_catn;
static void ec_cat_add(const char*id,int lv,int part){
    for(int i=0;i<ec_catn;i++) if(!strcmp(ec_cid[i],id))return;
    if(ec_catn<EC_MAXCAT){strncpy(ec_cid[ec_catn],id,39);ec_clv[ec_catn]=lv;ec_cpart[ec_catn]=part;ec_catn++;}
}
static void ec_cat_get(const char*id,int*lv,int*part){*lv=-1;*part=7;
    for(int i=0;i<ec_catn;i++) if(!strcmp(ec_cid[i],id)){*lv=ec_clv[i];*part=ec_cpart[i];return;} }

/*
 * 导出装备为 JSON 到 out(cap)。返回装备件数, <0 出错。
 * pid: 游戏进程; 内部自行 find_klass。
 */
/* ---- klass 缓存(导出装备/装备图共用,避免每次全内存搜类名) ---- */
static pid_t kc_pid = 0;
static uint64_t kc_kd=0, kc_kc=0, kc_ks=0, kc_ka=0;
static void ec_cached_klasses(rm_handle_t*h,rm_map_t*maps,int n,
                              uint64_t*kd,uint64_t*kc,uint64_t*ks,uint64_t*ka,pid_t pid){
    if(kc_pid==pid && kc_kd && kc_kc){
        *kd=kc_kd; *kc=kc_kc; *ks=kc_ks; if(ka)*ka=kc_ka; return;
    }
    uint64_t d=0,c=0,s=0;
    ec_find_klasses(h,maps,n,&d,&c,&s);
    uint64_t a=ec_find_klass(h,maps,n,"AccountData","Game");
    kc_pid=pid; kc_kd=d; kc_kc=c; kc_ks=s; kc_ka=a;
    *kd=d; *kc=c; *ks=s; if(ka)*ka=a;
}

/* 预热: 后台填充 klass 缓存(装备/装备图共用)。返回 1 成功 0 失败。 */
int ec_prime_export(pid_t pid){
    rm_handle_t h; if(rm_open(&h,pid))return 0;
    static rm_map_t maps[8192]; int n=rm_read_maps(pid,maps,8192);
    if(n<=0){rm_close(&h);return 0;}
    uint64_t kd=0,kc=0,ks=0,ka=0;
    ec_cached_klasses(&h,maps,n,&kd,&kc,&ks,&ka,pid);
    rm_close(&h);
    return (kd&&kc)?1:0;
}

int ec_export_equipment(pid_t pid, char *out, int cap){
    rm_handle_t h; if(rm_open(&h,pid))return -1;
    static rm_map_t maps[8192]; int n=rm_read_maps(pid,maps,8192);
    if(n<=0){rm_close(&h);return -2;}

    uint64_t kd=0,kc=0,ks=0;
    ec_cached_klasses(&h,maps,n,&kd,&kc,&ks,NULL,pid);
    if(!kd||!kc){rm_close(&h);return -3;}

    static unsigned char buf[EC_BUF];
    /* 单趟遍历同时: 建 EquipmentStaticData 目录 + 找最大 EquipmentContainer.Datas */
    ec_catn=0;
    uint64_t best=0; int bestn=0;
    for(int i=0;i<n;i++){
        if(maps[i].perms[0]!='r'||maps[i].perms[1]!='w')continue;
        if(maps[i].path[0]=='/'&&(strstr(maps[i].path,"/dev/")||strstr(maps[i].path,"/dmabuf")))continue;
        uint64_t s=maps[i].start,e=maps[i].end;
        for(uint64_t a=s;a<e;a+=sizeof(buf)-16){ size_t w=(e-a)<sizeof(buf)?(size_t)(e-a):sizeof(buf);
            ssize_t g=rm_read(&h,a,buf,w); if(g<8)continue;
            for(ssize_t j=0;j+8<=g;j+=8){
                uint64_t q;memcpy(&q,buf+j,8);
                uint64_t o=a+j;
                if(ks&&q==ks){ /* EquipmentStaticData -> 目录 */
                    uint64_t idp=0; int32_t lv=0,part=0;
                    rm_read_ptr(&h,o+0x10,&idp); rm_read(&h,o+0x18,&lv,4); rm_read(&h,o+0x1c,&part,4);
                    char id[40]; if(ec_rstr(&h,idp,id,sizeof(id))<=0)continue;
                    int ok=1; for(char*p=id;*p;p++) if(*p<0x20||(unsigned char)*p>0x7e){ok=0;break;}
                    if(ok&&lv>=0&&lv<=200&&part>=0&&part<=7) ec_cat_add(id,lv,part);
                } else if(q==kc){ /* EquipmentContainer -> 取最大 Datas */
                    uint64_t coll=0; rm_read_ptr(&h,o+0x10,&coll); if(coll<0x1000)continue;
                    uint64_t arr=0; int32_t sz=0; rm_read_ptr(&h,coll+0x10,&arr); rm_read(&h,coll+0x18,&sz,4);
                    if(sz<=0||sz>4096||arr<0x1000)continue;
                    int okc=0; for(int x=0;x<sz&&x<8;x++){uint64_t el=0;rm_read_ptr(&h,arr+0x20+(uint64_t)x*8,&el);
                        uint64_t ek=0;rm_read_ptr(&h,el,&ek); if(ek==kd)okc++;}
                    if(okc>=2&&sz>bestn){bestn=sz;best=coll;}
                }
            }
        }
    }

    if(!best){rm_close(&h);return -4;}

    /* 3) 遍历容器 -> JSON */
    int k=0;
    #define EMIT(...) do{ k+=snprintf(out+k,cap-k,__VA_ARGS__); if(k>=cap){rm_close(&h);return -5;} }while(0)
    EMIT("[");
    uint64_t arr=0; int32_t sz=0; rm_read_ptr(&h,best+0x10,&arr); rm_read(&h,best+0x18,&sz,4);
    int cnt=0;
    for(int x=0;x<sz;x++){
        uint64_t o=0; rm_read_ptr(&h,arr+0x20+(uint64_t)x*8,&o); if(o<0x1000)continue;
        uint64_t ek=0; rm_read_ptr(&h,o,&ek); if(ek!=kd)continue;
        int32_t lv=0,clv=0; uint64_t sidp=0,setp=0,mainp=0,subp=0;
        rm_read(&h,o+0x24,&lv,4); rm_read(&h,o+0x28,&clv,4);
        rm_read_ptr(&h,o+0x30,&sidp); rm_read_ptr(&h,o+0x40,&setp);
        rm_read_ptr(&h,o+0x58,&subp); rm_read_ptr(&h,o+0x60,&mainp);
        char sid[40]; if(ec_rstr(&h,sidp,sid,sizeof(sid))<=0)continue;
        int oksid=1; for(char*p=sid;*p;p++) if(*p<0x20||(unsigned char)*p>0x7e){oksid=0;break;}
        if(!oksid||lv<0||lv>999||clv<0||clv>99)continue;
        char set[40]; ec_rstr(&h,setp,set,sizeof(set));
        char mn[80]; ec_prop(&h,mainp,mn,sizeof(mn));
        int elv,part; ec_cat_get(sid,&elv,&part);

        char subs[5][80]; for(int s2=0;s2<5;s2++)subs[s2][0]=0; int sn=0;
        uint64_t srclist=0; rm_read_ptr(&h,subp+0x10,&srclist);
        if(srclist>0x1000){ uint64_t sarr=0; int32_t ssz=0;
            rm_read_ptr(&h,srclist+0x10,&sarr); rm_read(&h,srclist+0x18,&ssz,4);
            if(ssz>0&&ssz<=16&&sarr>0x1000) for(int s2=0;s2<ssz&&sn<5;s2++){
                uint64_t pv=0; rm_read_ptr(&h,sarr+0x20+(uint64_t)s2*8,&pv);
                ec_prop(&h,pv,subs[sn],sizeof(subs[sn])); if(subs[sn][0])sn++; }
        }
        char esid[80],eset[80],emn[120];
        json_esc(sid,esid,sizeof(esid)); json_esc(set,eset,sizeof(eset)); json_esc(mn,emn,sizeof(emn));
        if(cnt)EMIT(",");
        EMIT("{\"staticId\":\"%s\",\"part\":\"%s\",\"equipLv\":%d,\"strengthen\":%d,\"classLv\":%d,\"set\":\"%s\",\"mainProp\":\"%s\",\"subProps\":[",
             esid,PART[part&7],elv,lv,clv,eset,emn);
        for(int s2=0;s2<sn;s2++){ char e2[120]; json_esc(subs[s2],e2,sizeof(e2));
            EMIT("%s\"%s\"",s2?",":"",e2); }
        EMIT("]}");
        cnt++;
    }
    EMIT("]");
    rm_close(&h);
    return cnt;
}

/* 试连接: 打开 mem 并读 libil2cpp r--p 基址处的 ELF 魔数 */
int ec_can_connect(pid_t pid){
    if(pid<=0)return 0;
    rm_handle_t h; if(rm_open(&h,pid))return 0;
    uint64_t base=rm_module_base(pid,"libil2cpp.so",NULL);
    int ok=0;
    if(base){
        unsigned char m[4]={0};
        if(rm_read(&h,base,m,4)==4 && m[0]==0x7f && m[1]=='E' && m[2]=='L' && m[3]=='F') ok=1;
    }
    rm_close(&h);
    return ok;
}

/*
 * 导出登录账号信息 JSON 到 out(cap)：{"nickname","cuid","lv","leaderSid"}
 * 定位：找已填充的 EquipmentContainer 对象(其 Datas 元素为 EquipmentData) →
 *       找 AccountData(其 EquipmentContainer@0x58 == 该容器对象) → Info@0x10(PlayerInfo)。
 * PlayerInfo: Name(String)@0x10, CUID(int)@0x18, LV(int)@0x1c, LeaderSID(String)@0x30
 */
int ec_export_account(pid_t pid, char *out, int cap){
    rm_handle_t h; if(rm_open(&h,pid))return -1;
    static rm_map_t maps[8192]; int n=rm_read_maps(pid,maps,8192);
    if(n<=0){rm_close(&h);return -2;}

    uint64_t kd=0,kc=0,ks=0,ka=0;
    ec_cached_klasses(&h,maps,n,&kd,&kc,&ks,&ka,pid);
    if(!kd||!kc||!ka){rm_close(&h);return -3;}

    static unsigned char buf[EC_BUF];
    /* 1) 找已填充的 EquipmentContainer 对象(Datas 内是 EquipmentData) */
    uint64_t bestCont=0; int bestn=0;
    for(int i=0;i<n;i++){
        if(maps[i].perms[0]!='r'||maps[i].perms[1]!='w')continue;
        if(maps[i].path[0]=='/'&&(strstr(maps[i].path,"/dev/")||strstr(maps[i].path,"/dmabuf")))continue;
        uint64_t s=maps[i].start,e=maps[i].end;
        for(uint64_t a=s;a<e;a+=sizeof(buf)-16){ size_t w=(e-a)<sizeof(buf)?(size_t)(e-a):sizeof(buf);
            ssize_t g=rm_read(&h,a,buf,w); if(g<8)continue;
            for(ssize_t j=0;j+8<=g;j+=8){ uint64_t q;memcpy(&q,buf+j,8); if(q!=kc)continue;
                uint64_t o=a+j,coll=0; rm_read_ptr(&h,o+0x10,&coll); if(coll<0x1000)continue;
                uint64_t arr=0; int32_t sz=0; rm_read_ptr(&h,coll+0x10,&arr); rm_read(&h,coll+0x18,&sz,4);
                if(sz<=0||sz>4096||arr<0x1000)continue;
                int okc=0; for(int x=0;x<sz&&x<8;x++){uint64_t el=0;rm_read_ptr(&h,arr+0x20+(uint64_t)x*8,&el);
                    uint64_t ek=0;rm_read_ptr(&h,el,&ek); if(ek==kd)okc++;}
                if(okc>=2&&sz>bestn){bestn=sz;bestCont=o;}  /* 记容器对象地址 */
            }
        }
    }

    /* 2) 找 AccountData(EquipmentContainer@0x58 == bestCont) → Info → PlayerInfo */
    uint64_t info=0;
    if(bestCont) for(int i=0;i<n&&!info;i++){
        if(maps[i].perms[0]!='r'||maps[i].perms[1]!='w')continue;
        if(maps[i].path[0]=='/'&&(strstr(maps[i].path,"/dev/")||strstr(maps[i].path,"/dmabuf")))continue;
        uint64_t s=maps[i].start,e=maps[i].end;
        for(uint64_t a=s;a<e&&!info;a+=sizeof(buf)-16){ size_t w=(e-a)<sizeof(buf)?(size_t)(e-a):sizeof(buf);
            ssize_t g=rm_read(&h,a,buf,w); if(g<8)continue;
            for(ssize_t j=0;j+8<=g;j+=8){ uint64_t q;memcpy(&q,buf+j,8); if(q!=ka)continue;
                uint64_t ad=a+j,ec=0; rm_read_ptr(&h,ad+0x58,&ec);
                if(ec==bestCont){ rm_read_ptr(&h,ad+0x10,&info); break; }
            }
        }
    }

    char nick[80]="",lead[40]=""; int32_t cuid=0,lv=0;
    if(info){
        uint64_t namep=0,leadp=0;
        rm_read_ptr(&h,info+0x10,&namep); rm_read(&h,info+0x18,&cuid,4);
        rm_read(&h,info+0x1c,&lv,4); rm_read_ptr(&h,info+0x30,&leadp);
        ec_rstr(&h,namep,nick,sizeof(nick)); ec_rstr(&h,leadp,lead,sizeof(lead));
    }
    char enick[160],elead[80];
    json_esc(nick,enick,sizeof(enick)); json_esc(lead,elead,sizeof(elead));
    int k=snprintf(out,cap,"{\"nickname\":\"%s\",\"cuid\":%d,\"lv\":%d,\"leaderSid\":\"%s\",\"found\":%d}",
                   enick,cuid,lv,elead,info?1:0);
    rm_close(&h);
    return (k>0&&k<cap)?(info?1:0):-5;
}

/* ---- 在 PropertySetting._PropertyCloneValueMap(Dict@0x18) 找 key=="AttackValue" 的
        PropertyValue.Value(double) 地址 ---- */
static uint64_t ec_clone_attack_addr(rm_handle_t*h,uint64_t cm){
    if(cm<0x1000)return 0;
    uint64_t ent=0; int32_t cnt=0; rm_read_ptr(h,cm+0x18,&ent); rm_read(h,cm+0x20,&cnt,4);
    if(ent<0x1000||cnt<=0||cnt>64)return 0;
    int32_t alen=0; rm_read(h,ent+0x18,&alen,4); if(alen<=0||alen>64)return 0;
    for(int x=0;x<alen;x++){
        uint64_t ep=ent+0x20+(uint64_t)x*0x18;
        uint64_t kp=0,pv=0; rm_read_ptr(h,ep+8,&kp); rm_read_ptr(h,ep+0x10,&pv);
        if(kp<0x1000||pv<0x1000)continue;
        char ky[40]; if(ec_rstr(h,kp,ky,sizeof(ky))<=0)continue;
        if(strcmp(ky,"AttackValue")==0) return pv+0x18;
    }
    return 0;
}

/*
 * 秒杀开关: 遍历所有 RoleAwakenStaticData,改其 PropertySetting 活副本(_PropertyCloneValueMap)
 *   里的觉醒攻击增量 AttackValue。
 * enable=1: 全改成 INSTAKILL_VAL(觉醒过的角色攻击暴涨,仅影响本地PVE计算)
 * enable=0: 按觉醒等级还原(LV1-3=20, LV4-6=30, 全角色统一规律)
 * 返回修改条目数, <0 出错。
 *
 * 性能: 首次扫描全内存定位 1356 个 clone AttackValue 地址并缓存(addr+lv),
 *   同一局游戏内复用缓存,后续开关直接写入(毫秒级,无需重扫)。
 */
#define INSTAKILL_VAL 999999.0
#define IK_MAX 4096
static pid_t  ik_pid = 0;
static int    ik_n = 0;
static uint64_t ik_addr[IK_MAX];
static int8_t   ik_lv[IK_MAX];

/* 扫描收集所有 clone AttackValue 地址+觉醒等级到缓存。返回条目数, <0 出错。 */
static int ec_collect_instakill(pid_t pid){
    rm_handle_t h; if(rm_open(&h,pid))return -1;
    static rm_map_t maps[8192]; int n=rm_read_maps(pid,maps,8192);
    if(n<=0){rm_close(&h);return -2;}
    /*
     * 类定位必须走元数据（ms_find_klass2）：ms_find_klass 靠"类名字符串
     * 恰好等于某份命中的 name 指针"来匹配，同一份类名在内存里有上百份，
     * 经常匹配到结构相似但不是 Il2CppClass 的位置，导致 -3（找不到）或
     * 找到假类后扫不到条目（-4）。元数据 TypeDef 是唯一事实源。
     */
    uint64_t klass = ms_find_klass2(&h,maps,n,"RoleAwakenStaticData","Game",NULL);
    if(!klass) klass = ms_find_klass(&h,maps,n,"RoleAwakenStaticData","Game");
    if(!klass){rm_close(&h);return -3;}
    static unsigned char buf[EC_BUF]; int cnt=0;
    for(int i=0;i<n&&cnt<IK_MAX;i++){
        if(maps[i].perms[0]!='r'||maps[i].perms[1]!='w')continue;
        if(maps[i].path[0]=='/'&&(strstr(maps[i].path,"/dev/")||strstr(maps[i].path,"/dmabuf")))continue;
        uint64_t s=maps[i].start,e=maps[i].end;
        for(uint64_t a=s;a<e&&cnt<IK_MAX;a+=sizeof(buf)-16){ size_t w=(e-a)<sizeof(buf)?(size_t)(e-a):sizeof(buf);
            ssize_t g=rm_read(&h,a,buf,w); if(g<8)continue;
            for(ssize_t j=0;j+8<=g;j+=8){ uint64_t q;memcpy(&q,buf+j,8); if(q!=klass)continue;
                uint64_t o=a+j, idp=0, ps=0; int32_t lv=0;
                rm_read_ptr(&h,o+0x10,&idp); rm_read(&h,o+0x18,&lv,4); rm_read_ptr(&h,o+0x30,&ps);
                char id[64]; if(ec_rstr(&h,idp,id,sizeof(id))<=0)continue;
                int ok=1; for(char*p=id;*p;p++) if(*p<0x20||(unsigned char)*p>0x7e){ok=0;break;}
                if(!ok||lv<1||lv>10)continue;
                uint64_t cm=0; rm_read_ptr(&h,ps+0x18,&cm);
                uint64_t va=ec_clone_attack_addr(&h,cm); if(!va)continue;
                int dup=0; for(int k=0;k<cnt;k++) if(ik_addr[k]==va){dup=1;break;}
                if(dup)continue;
                ik_addr[cnt]=va; ik_lv[cnt]=(int8_t)lv; cnt++;
            }
        }
    }
    rm_close(&h);
    ik_pid=pid; ik_n=cnt;
    return cnt;
}

/* 预热: 仅扫描填充缓存,不修改。返回条目数。供后台调用,避免点击时卡顿。 */
int ec_prime_instakill(pid_t pid){
    return ec_collect_instakill(pid);
}

int ec_set_instakill(pid_t pid, int enable){
    /* 缓存失效则重扫(换游戏进程,或缓存首个地址已不是有效 PropertyValue) */
    int valid = (ik_pid==pid && ik_n>0);
    if(valid){
        rm_handle_t hc; if(rm_open(&hc,pid)==0){
            double probe=0;
            if(rm_read(&hc,ik_addr[0],&probe,8)!=8) valid=0;
            rm_close(&hc);
        } else valid=0;
    }
    if(!valid){ int r=ec_collect_instakill(pid); if(r<0)return r; }
    if(ik_n<=0)return -4;
    rm_handle_t h; if(rm_open(&h,pid))return -1;
    int done=0;
    for(int i=0;i<ik_n;i++){
        double nv = enable ? INSTAKILL_VAL : (ik_lv[i]<=3 ? 20.0 : 30.0);
        if(rm_write(&h,ik_addr[i],&nv,8)==8) done++;
    }
    rm_close(&h);
    return done;
}






