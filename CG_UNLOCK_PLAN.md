# LuciaTools 增加 CG 解锁功能 —— 实施方案（v3：爱情通讯 / Love Link）

> 编写：2026-10-31
> 前置文档：`LUCIA_TOOLS_TECH_NOTES.md`
> v1 假设「存在 IsUnlock 布尔字段」→ 被实测推翻。
> v2 假设「解锁 = 背包持有 Background/CGCollection 道具」→ 被本地化文本推翻。
> **v3 结论：CG 系统就是「爱情通讯」Love Link，解锁杠杆是角色的觉醒星级 `AwakenLV`。**

---

## 0. 结论：你说对了，Love Link 就是 CG 系统

在 APK 的 `Assets/Game/CG/` 下有 **412 个 CG 场景目录**，其中 **364 个带 `_LoveTalk.prefab`**。
也就是说全部 CG 内容的 88% 都是爱情通讯的演出，剩下的才是抽卡动画（122 个 `_Gacha`）和立绘（101 个 `_Source`）。

本地化文本里一条直接证据：

```
UI_LoveTalk            @ Love Link              中文：爱情通讯
UI_LoveTalk_Hero       @ Gallery                中文：图鉴        ← 图鉴是这个 tab
T_Item_Description_Background01 @ Love Link-Gallery Background Kit
                                         中文：爱情通讯-图鉴背景套件
```

所以「CG 图鉴」不是独立模块，而是爱情通讯里的一个 tab。v2 找到的 `Background` / `CGCollection` 道具，
**只是图鉴的背景套件，不是 CG 本体**——难怪它们跟解锁状态对不上。

---

## 1. 系统结构（四层）

```
爱情通讯 Love Link
├── 消息主页      每角色一条聊天线，消耗「通讯电力」推进
├── 剧情章节      每角色最多 3 章（H002_01 / H002_02 / H002_03）
│                 └─ 每章的 CG 演出 = Assets/Game/CG/A00xx/A00xx_LoveTalk.prefab
├── 造型 Skin     爆衣 / 裸体 / 性爱 三套
│                 └─ 立绘 prefab：H007_Dialog_1（爆衣）/ H007_Dialog_2（裸体）/ H007_Sex（性爱）
└── 图鉴 Gallery
    ├── 已获得 / 未获得团员列表（UI_LoveTalk_Acquired / UnAcquired）
    ├── 背景套件 Background × 170（ItemFun = BG/A00xx_Dialog 或 BG/S00xx_Dialog）
    └── CG 合集 CGCollection × 14（ItemFun = BG/CarnivalNNN → Carnival00N_LoveTalk.prefab）
```

CG 资源实测分布（从 `assets/aa/catalog.json` 的 `m_InternalIds` 统计）：

| 类型 | 数量 | 说明 |
|---|---|---|
| `Assets/Game/CG/A0001` ~ `A0374` | 374 | 剧情 CG，其中 364 个有 `_LoveTalk.prefab` |
| `Assets/Game/CG/P0001` ~ `P0030` | 30 | 特殊演出（Camera 控制器为主） |
| `Assets/Game/CG/Carnival001~003` | 3 | CGCollection 对应 |
| 带 `_Gacha.prefab` | 122 | 抽卡动画，不是 CG |
| 带 `_Source.prefab` | 101 | 立绘源文件 |

角色 ↔ CG 的绑定关系藏在动画控制器路径里：

```
Assets/Game/CG/A0001/H007_LoveTalk_03_Controller/Boobs.anim
                              ^^^^ 角色   ^^ 章节号
```

实测有 **115 个 A 系 CG 能这样反查到角色**，涉及 114 个角色；绝大多数编号是 `_03`（第三章＝性爱章）。

---

## 2. 解锁条件（精确到原文）

全部来自 `tools/out/text/CHS.txt`，不是推测：

| 内容 | 解锁条件 | 原文 |
|---|---|---|
| 爆衣造型 | 觉醒 **1 星** | `UI_LoveTalk_IsLock3` 爆衣造型需要完成觉醒1星，即可解锁。 |
| 裸体造型 | 觉醒 **4 星** | `UI_LoveTalk_IsLock4` 裸体造型需要完成觉醒4星，即可解锁。 |
| 性爱造型 | 觉醒 **6 星** | `UI_LoveTalk_IsLock2` 性爱造型需要完成觉醒6星，即可解锁。 |
| 通讯剧情 | 完成上一章 | `UI_LoveTalk_PreconditionTips` 完成上一个章节通讯剧情后解锁 |
| 未持有角色 | 抽到才行 | `UI_LoveTalk_UnGetRole` 尚未取得团员 |

教学文案也印证了这条路：

```
T_TutorialStep_Description__105
完成第一阶段觉醒了！除了战力提升之外，还额外解锁了【爱情通讯】的爆衣造型…
```

**关键**：解锁状态不单独存，是 `AwakenLV` 实时算出来的。账号数据里 41 个容器、350 个字段翻遍了，
没有任何 `SkinList` / `UnlockedCGIDs` / `IsUnlock`。角色对象只有一个 `AwakenLV` 整数。

---

## 3. 账号实测现状（只读，未改任何数据）

账号持有 101 个角色（角色总表 289 个）：

| 造型 | 已解锁 | 占持有 |
|---|---|---|
| 爆衣（觉醒 ≥1） | 27 | 26.7% |
| 裸体（觉醒 ≥4） | 26 | 25.7% |
| 性爱（觉醒 ≥6） | 20 | 19.8% |

`AwakenLV` 分布：`{0: 68, 1: 0, 2: 1, 3: 0, 4: 2, 5: 4, 6: 20}`（101 个持有角色内）

图鉴道具：

| 道具 | 目录 | 持有 |
|---|---|---|
| `Background`（图鉴背景套件） | 170 | 2（基本背景、花园） |
| `CGCollection`（CG 合集） | 14 | 1（夜馆的秘宴） |

也就是说 184 个图鉴条目只开了 3 个，而 101 个角色里只有 20 个能看性爱造型。

---

## 4. 三个可写目标，只有第一个值得做

| 目标 | 内存形态 | 能不能写 | 判断 |
|---|---|---|---|
| **`RoleData.AwakenLV`** | `int`（0~6） | ✅ 纯值写入 | **首选**。单字段、定长、无分配，跟现有秒杀同一套机制 |
| `LineRoleData.DramaInfoList[].UnLockDialogIDs` | `List<string>` | ❌ 要 push 新元素 | 扩容要分配托管数组，从进程外做不到，必崩 |
| `ItemContainer.Items` 里的 `StaticID` | `System.String*` | ⚠️ 指针替换 | 只能改已有条目，最多「牺牲」246 条换 246 个图鉴项，且会破坏真实道具 |

`UnLockDialogIDs` 那条要特别说明：**不能碰**。它是要往 `List<string>` 里塞新元素，
`List` 满了会 `new T[]`，从外部进程无法分配托管对象，一写就 `Fatal signal 11`。
v2 里「只改已有元素」的折中在这里也没意义——改已有元素只是把剧情行替换成别的行，不会新增解锁。

所以路线收敛成一条：**改 `RoleData.AwakenLV`**。

### 为什么这条路能成

1. `AwakenLV` 是 `int`，单字段定长，写 4 字节，不触发任何分配。
2. 造型是否显示完全由客户端这个字段算出来（没有独立的解锁表），改完 UI 立刻变。
3. CG 演出是**纯本地资源**：`Assets/Game/CG/A00xx/` 的 prefab 全在 APK 里，
   客户端点开只是 `AssetBundle` 加载，不走服务器。服务器只知道你的觉醒进度，不知道你在看哪张图。
4. 已有 `ec_set_instakill` 证明了「全内存扫地址 → 缓存 → 开关写值 → 还原」这套流程在本机能稳定跑。

### 代价与红线

- 觉醒星级同时影响**战力数值显示**。改完角色面板会显示虚高战力，这是必然副作用，要在 UI 上写明。
- 只改内存，**不提交服务器**，重启即恢复。绝不能去调 `RoleHandler.Awaken` 之类接口。
- 必须真读原值再写回。**不许学秒杀的坏习惯**——`ec_set_instakill` 关闭时是按等级公式推算 20/30，
  CG 功能如果也这么干，关闭后角色觉醒值会被写成错误数字。

---

## 5. 实施路线

| 阶段 | 产出 | 依赖 |
|---|---|---|
| P1 | `frida_lovelink_probe.js` 拿到 `RoleData` 的类名/命名空间/`AwakenLV` 偏移 | 设备 + root |
| P2 | 设备端确认「改 `AwakenLV` → 造型解锁 → 能播」全链路 | P1 |
| P3 | 抽 `mem_scan.c/h` 公共扫描层（不依赖任何结论，可以先做） | 无 |
| P4 | `lovetalk_core.c`：扫 `RoleData` 实例 → 缓存 `AwakenLV` 地址与原值 → 开关写 6 | P1 |
| P5 | JNI + `NativeBridge.kt` + 悬浮窗功能项 | P4 |
| P6 | 测试 + 版本失效防护 | P5 |

### P1：拿字段偏移（半小时）

```bash
# 设备需 root + frida-server
frida -U -f com.nerversoft.ark.recode -l tools/frida_lovelink_probe.js --no-pause
```

脚本会打印所有精确命中的类（`RoleData` / `LineRoleData` / `RoleSkinData` …）以及每个字段的偏移与类型，
最后单独汇总一行形如：

```
  Game.RoleData.AwakenLV  @ 0x38  System.Int32
```

拿到这个就能写代码了。

> 为什么必须运行时拿：`global-metadata.dat` 头部熵 8.00（完全加密），
> 静态搜 `RoleData`、`AwakenLV` 全 0 命中；而 `libil2cpp.so` 的 `il2cpp_class_get_fields`
> 等导出在运行时是可用的——metadata 此时已解密进内存。

### P2：三步验证（半天，是整个方案唯一的分水岭）

1. 游戏里记下一个 `AwakenLV = 0` 的角色，进爱情通讯图鉴，确认她的造型是「需要完成觉醒 N 星」。
2. 用应用内探针把她的 `AwakenLV` 改成 6（一次只改一个角色）。
3. 退回图鉴重进：
   - 造型解锁且能点开播放 → **通了**，进 P4；
   - 造型解锁但点开黑屏 → 播放还有二次校验，转 §8 兜底；
   - 造型没变 → 客户端有独立缓存，需要再找缓存对象（多花 1~2 天）。

第 2 步改之前必须校验 `obj+0 == RoleData` 的 klass。角色对象被 GC 回收后地址会复用，
不校验就写等于往随机内存写数据。

### P3：抽 `mem_scan`（可并行）

把 `equip_core.c` 的 `ec_rstr` / `ec_find_all_cstr` / `ec_find_klass` 原样搬到 `mem_scan.c`，
改名 `ms_*`、去掉 `static`；`equip_core.c` 里用 `#define` 回指，行为不变。

这一步**不依赖 P1/P2 的任何结论**，现在做不会白做。

### P4：`lovetalk_core.c`

和 v2 §7 的骨架完全一样，只是常量区换成：

```c
/* ===== 对应游戏版本：0717_37_1（APK 日期 2026-07-17）===== */
#define LT_ROLE_CLS    "RoleData"     /* 待 P1 确认，可能带前缀 */
#define LT_ROLE_NS     "Game"
#define LT_OFF_AWAKEN  0x38           /* int32 AwakenLV，待 P1 确认 */
#define LT_MAX_ROLES   512
/* ======================================================== */
```

流程：

1. `ms_find_klass` 定位 `RoleData`
2. 全内存找「对象首字 == `RoleData` klass」的对象，收集地址
3. **过滤条件**：读 `AwakenLV`，只保留 `0 <= v <= 6` 的（防止误命中把随机内存当角色）
4. 记录原值 → 写快照
5. 开关：`enable` 写 6，`disable` 写回原值

对象数校验：本次账号 101 个角色，全量角色 289。扫出来 100~300 之间合理；
为 0 或几千说明偏移/类名错了，**直接拒绝写入并返回错误码**，不要瞎写。

### P5：UI

悬浮窗加一项「解锁 CG 造型」，副标题写明「仅本机显示，会显示虚高战力，重启还原」。
开关逻辑照抄秒杀的 `runInstakillToggle`，把 `ec_set_instakill` 换成 `lt_set_unlock`。

---

## 6. 测试清单

| # | 动作 | 期望 | 失败意味着 |
|---|---|---|---|
| 1 | 进爱情通讯图鉴截图 | 有基准 | — |
| 2 | 只改 1 个角色 `AwakenLV` | 该角色 3 套造型全解锁 | 偏移错 or 有缓存 |
| 3 | 点开性爱造型 | 能播 | 播放有二次校验 → 转 §8 |
| 4 | 退出重进图鉴 | 仍然解锁 | 有缓存需重进 |
| 5 | 点「还原」 | 造型回到锁定态 | 还原失败立刻停手 |
| 6 | 重启游戏 | 全部恢复 | 正常（只改内存） |
| 7 | 反复开关 10 次 | 无崩溃 | 幂等性有问题 |

**第 2 步必须只改 1 个角色。** 一次全改，出问题说不清是哪条。

---

## 7. 风险

1. 内存修改属于游戏辅助，**有封号风险**。只在离线看，不要改完去打 PVP / 公会战。
2. 觉醒星级影响战力显示，改完全账号角色面板虚高。这是设计副作用，不是 bug。
3. 写错偏移 → `Fatal signal 11`。写前校验对象头是硬性要求，不能省。
4. 服务器权威：只影响本机，重登即恢复。
5. `tools/out/` 里有 jwt 和账号 dump，已加 `.gitignore`，不要外传。

---

## 8. 兜底：本地 CG 图库（零风险）

如果 §5-P2 第 3 步失败（造型解锁了但点开不能播），改做独立图库：

- 374 个 A 系 + 30 个 P 系 CG，资源全在 APK 的 `assets/aa/Android/firstbundlegroup_assets_assets/game/cg/`
- 用 UnityPy（已装）读 catalog → 导出 Texture2D 成 PNG
- 打包进 LuciaTools 的 `assets/gallery/`，App 侧做个 GridView 浏览页

缺点：这些 CG 是 Spine 骨骼动画，导出的静态 PNG 会丢掉动作，只能看定格。
如果只求「看得到」，够用；要还原动态效果成本很高。

---

## 附：本结论怎么复现

```powershell
# 1) 解出所有本地化文本（已做，产物在 tools/out/text/）
python -c "import UnityPy; ..."   # 见 tools/out/text/{CHS,ENG,...}.txt

# 2) 查爱情通讯相关文案
Select-String -Path tools\out\text\CHS.txt -Pattern '爱情通讯|造型'

# 3) 账号侧只读判定（需要先 python -m utils.login_helper 人工登录拿 jwt）
python tools\verify_gallery_state.py --jwt-file tools\out\jwt.txt
python tools\verify_gallery_state.py --from-dump tools\out\account_login.json

# 4) 设备端字段探测
frida -U -f com.nerversoft.ark.recode -l tools\frida_lovelink_probe.js --no-pause
```
