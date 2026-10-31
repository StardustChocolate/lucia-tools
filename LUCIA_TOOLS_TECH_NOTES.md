# LuciaTools 技术记录与 CG 解锁调研

> 记录时间：2026-10-31  
> 相关目录：`D:\study\lucia-tools-main`、`D:\星陨`  
> 相关项目：`D:\study\arkrecode_gvg_sniffer-main`

---

## 1. 结论摘要

LuciaTools 是一个 Android 内存工具，不是网络协议脚本。它通过 Android 的 `sharedUserId` 机制，让工具 APK 与特殊游戏 APK 共享同一个 Linux UID，然后通过 `/proc/<game_pid>/maps` 和 `/proc/<game_pid>/mem` 读取游戏进程内存，并通过 JNI 调用 native 层完成装备枚举、账号信息读取和部分 PVE 数值修改。

它和 arkrecode_gvg_sniffer 的区别：前者是 Android 同 UID 内存读取/修改，需要特殊重打包游戏 APK；后者是 HTTP/WebSocket 协议自动化，运行在 PC Python 环境，不需要修改游戏 APK。

---

## 2. 当前目录中的安装包

`D:\星陨` 下目前有三个包：

- `0717_37_1_ArkReCode_Repack.apk`：362 MB，特殊重打包游戏包，包名仍为 `com.nerversoft.ark.recode`。
- `lucia-tools.apk`：约 10.8 MB，LuciaTools 编译产物，包名 `com.openrubi.luciatools`，native 库是 `lib/arm64-v8a/liblucia.so`。
- `LucimaTools-android-1.3.0.apk`：约 105 MB，包名 `com.openrubi.lucimatools`，使用 Chaquopy 运行 Python 后端，前端是本地 Web UI，包含角色图鉴、账号管理、游戏数据/API 自动化等功能，不是 LuciaTools 的内存读写模块。

---

## 3. 同 UID 与 native 读写原理

LuciaTools 的 Manifest 中声明：

```xml
android:sharedUserId="com.openrubi.luciatools"
```

特殊游戏 APK 也必须声明同一个 `sharedUserId`：

```xml
<manifest
    package="com.nerversoft.ark.recode"
    android:sharedUserId="com.openrubi.luciatools">
```

同时两个 APK 必须使用同一个 keystore 签名。满足这两个条件后，Android 才会把它们分配到同一个 Linux UID，LuciaTools 才能打开游戏进程的 `/proc/<pid>/mem`。

因此它的特点是：不需要 root、不注入游戏进程、不修改游戏业务代码；但必须替换官方游戏 APK，安装特殊重打包版本。只重新签名而不加 `sharedUserId` 不够，只加 `sharedUserId` 而使用不同签名也不够。

核心文件：

```text
app/src/main/cpp/rubimem.c
app/src/main/cpp/equip_core.c
app/src/main/cpp/lucia_jni.cpp
app/src/main/java/com/openrubi/luciatools/mem/NativeBridge.kt
```

---

## 4. native 扫描流程

```text
扫描 /proc 找到游戏 pid
-> 打开 /proc/<pid>/mem
-> 解析 /proc/<pid>/maps
-> 在可读写内存中扫描 IL2CPP 类名字符串
-> 根据固定偏移定位类和运行时对象
-> 读取或写入字段
-> 通过 JNI 返回 JSON 给 Kotlin
```

README 里的 IL2CPP 解析，实际是内存扫描加硬编码 IL2CPP 对象偏移，不是完整的 IL2CPP 反射。

代码中的典型偏移：

```text
Il2CppClass.name       +0x10
Il2CppClass.namespace  +0x18

System.String.length   +0x10
System.String.chars    +0x14

List<T>._items         +0x10
List<T>._size          +0x18
数组元素起始            +0x20
```

`EquipmentData` 字段读取使用 `+0x24`、`+0x28`、`+0x30`、`+0x40`、`+0x58`、`+0x60` 等偏移。这些偏移依赖具体游戏版本和 IL2CPP 布局，游戏更新后可能失效。

---

## 5. 导出装备表

实现文件：

```text
app/src/main/cpp/equip_core.c
app/src/main/java/com/openrubi/luciatools/feature/ExportEquipment.kt
```

流程：定位 EquipmentContainer，找到最大的装备数组，遍历 EquipmentData，读取 ID、等级、强化、稀有度、套装、主副属性，native 返回 JSON，Kotlin 转成 CSV 并写入 Android 下载目录。

输出文件：

```text
LuciaTools_装备列表_yyyyMMdd_HHmmss.csv
```

CSV 字段：名称、稀有度、部位、等级、强化、套装、主属性、副属性、StaticID。

注意：当前代码实际只导出 CSV，README 中的 CSV/JSON 并未在导出装备表功能中同时实现。

---

## 6. 导出装备图

实现文件：

```text
StatScorer.kt
StatCalc.kt
TopAttack.kt
TopHp.kt
TopDef.kt
TopResist.kt
TopSpeed.kt
CardRenderer.kt
ExportCard.kt
```

流程：native 导出账号信息和装备 JSON；筛选 75 级以上且强化 +15 的装备；计算八维雷达图；回溯求解顶配攻击、生命、防御、抗性、速度；渲染 1600x900 PNG；同时输出 JSON 明细。

八个维度：攻击、生命、防御、暴击率、暴击伤害、状态命中、状态抗性、速度。

评分模型：单件装备贡献 = 固定值 + 百分比 x 百分比基础值；每部位分别排序；第 k 高的部件组合成第 k 套；每套求和；Lp 范数 = (sum(value^P))^(1/P)，默认 P=2；再映射到 0~10 雷达值。

顶配求解使用回溯搜索，并加上部位候选剪枝。

输出文件：

```text
LuciaTools_装备图_yyyyMMdd_HHmmss.png
LuciaTools_装备图明细_yyyyMMdd_HHmmss.json
```

---

## 7. 秒杀原理

实现位置：

```text
app/src/main/cpp/equip_core.c
ec_collect_instakill / ec_set_instakill
```

原理：在内存中寻找 RoleAwakenStaticData；找到 PropertySetting；找到 \_PropertyCloneValueMap；找到 key 为 AttackValue 的 PropertyValue.Value；开启时写入 999999.0；关闭时按觉醒等级恢复为 20.0 或 30.0。

它不是直接修改伤害函数，而是修改觉醒攻击加成，因此只对已觉醒角色生效，只影响部分客户端本地计算的 PVE。PVP、服务器复算或服务器校验的玩法可能无效。

---

## 8. 特殊游戏 APK 的逆向发现

检查 0717_37_1_ArkReCode_Repack.apk 中的：

```text
lib/arm64-v8a/libil2cpp.so
assets/bin/Data/Managed/Metadata/global-metadata.dat
```

标准 IL2CPP metadata 常见 magic 是 0xFAB11BAF，但当前包中的 global-metadata.dat 开头为 5a4922385f8e28...，不是标准 magic，说明 metadata 被加密或混淆。

同时 libil2cpp.so 中没有直接出现 EquipmentData、EquipmentContainer、EquipmentStaticData、RoleAwakenStaticData、AccountData 等类名。这说明 LuciaTools 不是静态读取 APK 元数据，而是在游戏运行时扫描已经解密到内存中的类名字符串。

如果要找 CG/Gallery 字段，不能直接对 APK 里的 metadata 跑 Il2CppDumper。需要运行时 dump，或者使用 Frida / frida-il2cpp-bridge，或者扩展 LuciaTools 自身的运行时字符串和类扫描。

---

## 9. CG 解锁的两条路线

### 路线 A：修改游戏内图鉴/回想解锁状态

目标是让游戏内图鉴中原本锁定的 CG 显示为已解锁，并能播放。

需要：动态枚举游戏 IL2CPP 类；找到 CG/Gallery/Collection 相关类；找到保存解锁状态的字段；判断字段类型；在 LuciaTools 中写入；关闭时恢复原值；测试 UI 显示、CG 播放、重登和重启。

可能涉及的类名：CGData、CgData、GalleryData、GalleryContainer、AlbumData、CollectionData、CollectionContainer、IllustrationData、MemoryData、StoryData、UnlockData。

可能的字段名：IsUnlock、IsUnlocked、IsOpen、IsRead、IsReceived、UnlockFlag、UnlockedCGIDs、UnlockedCgIds。

也可能是 bool[]、byte[] bitset、List<int> 或 HashSet<int>。必须运行时确认，不能盲猜。

### 路线 B：本地 CG 图库

从游戏 asset bundle 中提取 CG 图片，在 LuciaTools 自己的界面中浏览。

优点是：不依赖游戏内存偏移；不需要修改游戏状态；不会因为游戏热更新立即失效；不会触发服务器校验。

缺点是：只是 LuciaTools 内看图；不会改变游戏内图鉴解锁状态；需要解析 Unity asset bundle 和 Texture2D/Sprite。

特殊游戏 APK 中已经能看到 assets/aa/Android/.../game/cg/... 这样的资源路径。如果目标只是自己看 CG，路线 B 更稳妥。

---

## 10. 在 LuciaTools 中增加 CG 解锁的技术方案

### 10.1 先做动态类发现

开发环境建议：有 root 的测试设备或模拟器、Frida server、frida-il2cpp-bridge 或 Zygisk-Il2CppDumper，必要时配合 IDA/Ghidra。GameGuardian 可用于快速差异搜索，但最终仍要回到 IL2CPP 对象结构验证。

枚举类时搜索 CG、Gallery、Album、Collection、Illustration、Memory、Story、Unlock。然后打开图鉴界面，检查哪些类的实例字段发生变化。

### 10.2 建议新增的 native 模块

```text
app/src/main/cpp/gallery_core.c
app/src/main/cpp/gallery_core.h
```

可以把 equip_core.c 中私有的 ec_find_all_cstr()、ec_find_klass() 抽成公共模块，例如 mem_scan.c 和 mem_scan.h，供装备和 CG 模块共用。

### 10.3 建议的 native 接口

```c
int gc_prime_gallery(pid_t pid);
int gc_set_gallery_unlock(pid_t pid, int enable);
int gc_restore_gallery(pid_t pid);
int gc_export_gallery(pid_t pid, char *out, int cap);
```

内部流程：通过运行时类名定位 Gallery/CG 容器；遍历 CG 条目；读取原始解锁字段；保存原始值；写入解锁值；关闭时恢复原始值。

必须保存原始值，不能假设解锁字段一定是 0/1，也不能盲写整个对象。

### 10.4 JNI 和 UI 接入

在 NativeBridge.kt 中增加三个外部函数，分别用于预热、开启和恢复画廊缓存。

需要修改 FloatController.kt、FeatureWindow.kt 和 strings.xml，增加解锁 CG 功能项。状态参考现有秒杀功能。

### 10.5 测试清单

1. 打开游戏图鉴，记录锁定状态。
2. 开启解锁。
3. 关闭并重新打开图鉴。
4. 点击具体 CG，确认是否能进入回想或剧情。
5. 重启游戏，确认是否恢复。
6. 重新登录，确认是否恢复。
7. 关闭解锁，确认原始状态被正确恢复。
8. 观察是否崩溃、卡死或图鉴错乱。

重点验证：图鉴显示已解锁不等于 CG 实际可播放。这两个状态可能不是同一个字段。

---

## 11. CG 解锁是否可行的关键判断

### 情况 1：客户端本地保存解锁状态

如果游戏图鉴 UI 直接读取本地 GalleryData 或 CollectionData，内存修改后重新打开图鉴可能显示解锁，但重新登录后可能恢复。

### 情况 2：解锁状态来自服务器账号数据

如果登录时服务器下发已解锁 CG 列表，本地修改只影响当前客户端，重新登录后会恢复。

### 情况 3：打开 CG 时服务器再次校验

如果点击 CG 时客户端请求服务器，例如 GalleryHandler.OpenCG，服务器返回未解锁，那么本地 UI 改了也无法播放。这种情况下纯内存工具无法做到真正永久解锁。

连接游戏时预热画廊缓存，避免每次点击都进行全内存扫描。

---

## 12. 关于修改游戏 APK 的结论

给 LuciaTools 增加 CG 解锁，不需要修改游戏业务代码。

特殊游戏 APK 的最小要求是：AndroidManifest.xml 增加 sharedUserId，并与 LuciaTools 使用同一个 keystore 重签。可能还需要绕过签名校验、文件完整性校验，处理 AppGuard 等保护。

但 LuciaTools 自身要新增：CG/Gallery 逆向分析、native 扫描模块、内存写入与恢复逻辑、JNI 接口、悬浮窗 UI、测试与版本适配。

真正的工作量在 LuciaTools 的 native 逆向模块，不在游戏 APK 的业务代码修改。

---

## 13. 风险与注意事项

1. 这是内存修改和游戏辅助功能，存在封号风险。
2. 特殊游戏 APK 来源必须可信，重打包包可能被植入恶意代码。
3. 同 UID 会让工具和游戏共享权限边界，需特别小心。
4. 硬编码偏移非常依赖游戏版本，更新后可能失效或写坏内存。
5. 服务器权威数据无法通过本地内存修改永久改变。
6. CG 解锁可能只是修改 UI 显示，实际播放仍可能失败。
7. 修改前应确认账号已绑定，并备份重要信息。
8. 不要把 keystore、特殊 APK、账号数据外传。

---

## 14. 后续工作建议

切换到 D:\study\lucia-tools-main 后，建议按以下顺序推进：

1. 先确认 LucimaTools 是否已有可复用的 CG 资源或图鉴数据。
2. 用 Frida 或 Zygisk-Il2CppDumper 动态枚举 Gallery/CG 相关类。
3. 找到解锁字段和对象关系。
4. 判断是客户端状态还是服务器状态。
5. 先写一个独立的 native 测试函数，只读不写。
6. 读出的 CG 解锁状态与游戏图鉴核对。
7. 再实现单条 CG 的开关。
8. 验证恢复逻辑。
9. 最后扩展到全部图鉴和 UI 按钮。
10. 若服务器权威，转为本地 CG 图库方案。

---

## 15. 快速命令记录

```powershell
# 查看 APK 内容
python -c "import zipfile; z=zipfile.ZipFile(r'D:\星陨\0717_37_1_ArkReCode_Repack.apk'); print(chr(10).join(z.namelist()[:50]))"

# 检查 metadata 头，标准 IL2CPP magic 应为 FAB11BAF
python -c "b=open(r'D:\星陨\_analysis\global-metadata.dat','rb').read(4); print(b.hex())"

# 查看设备信息
adb devices
adb shell getprop ro.product.cpu.abi
```

后续切换目录：

```powershell
cd D:\study\lucia-tools-main
```

---

## 16. 备注

本文档记录当前源码、APK 和会话中的分析结论。

在真正实现 CG 解锁前，必须先确认游戏的解锁字段是否存在、字段类型、对象生命周期以及服务器是否校验。否则直接写内存很可能只能改变本地 UI，甚至导致游戏崩溃。

---

## 17. 实施方案（已产出）

CG 解锁的完整落地步骤见 **`CG_UNLOCK_PLAN.md`**（阶段 P0~P7，含 native/JNI/Kotlin/UI 代码）。

配套工具：

```text
tools/frida_lovelink_probe.js  运行时枚举 RoleData / LineRole 等类的字段名与偏移（v3 主力）
tools/frida_gallery_probe.js   同上的通用版，按关键词扫 CG/Gallery 类
tools/gallery_diff.py          两次实例 dump 差分，定位解锁字段
tools/verify_gallery_state.py  账号侧只读判定（对比道具目录与持有量）
```

补充事实（较第 8 节更新）：

- `libil2cpp.so` 中 `il2cpp_class_from_name`、`il2cpp_class_get_fields`、`il2cpp_field_get_offset`、`il2cpp_domain_get_assemblies` 等导出符号**存在**，所以 Frida 可以在运行时拿到字段名与偏移，不必解开加密的 metadata。
- `assets/aa/catalog.json`（25 MB）中有 **948 条** `game/cg/` 资源地址，对应 bundle 都在 APK 内 `assets/aa/Android/` 下 → 路线 B（本地图库）完全可行。
- `D:\study\arkrecode_gvg_sniffer-main\data\master.json` 中 `CGCollection01..14` 是**道具**（items 表），说明 CG 收集在本作里也有道具形态；但图鉴列表本身更可能由客户端 CG 表驱动。
- 同 UID 还意味着可以直接读写 `/data/data/com.nerversoft.ark.recode/`，若解锁状态落在本地缓存文件，改文件（路线 C）比改内存更稳，值得优先验证。

### 17.1 实测结论：CG 解锁 = 道具持有（已推翻第 9 节的字段假设）

用真实账号跑 `AccountHandler.Login`（只读）验证，结论：

- **不存在** CG 列表容器，也没有 `IsUnlock` 之类的布尔字段。第 9 节假设的 `GalleryData` + `IsUnlock` 在本作里没有对应物。
- CG 图鉴可看性 = 是否持有道具。目录在 master.db 的 `Item` 表：
  - `ItemType='CGCollection'` 共 14 条，ItemFun = `BG/CarnivalNNN`
  - `ItemType='Background'` 共 170 条，ItemFun = `BG/xxx`
- 测试账号持有 `CGCollection13`（夜馆的秘宴）+ `Background01` + `Background157`，其余 181 条全部未持有。
- 持有状态由服务器在 `ItemContainer.Items` 下发，共 246 条 `{StaticID, Count}`。
- 其他容易误认的容器：`CollectBook` 是角色/神器图鉴（202）；`LibraryContainer` 是神器/角色读物（103，Count 全 0）；`LineRoleDataContainer` 是亲密度剧情的 `UnLockDialogIDs`。
- `LineDrama.DramaIcon` 里的 `CG/A0041/...` 中 A0041 是**神器 id**，不是角色 CG，别被 `CG/` 前缀误导。

⇒ 这一版曾得出"改 `ItemData.StaticID` 指针"的结论，**已被 17.2 修正**：`Background` / `CGCollection` 只是图鉴的背景套件，不是 CG 本体。

复现：

```powershell
cd D:\study\arkrecode_gvg_sniffer-main; python -m utils.login_helper   # 浏览器人工登录拿 jwt
cd D:\study\lucia-tools-main; python tools\verify_gallery_state.py --jwt-file tools\out\jwt.txt
```

注意：门户 `/api/v2/login` 强制要求 `captchaSDK.getToken("loginCaptcha")`，账号+密码无法脚本化登录，也不应绕验证码。

### 17.2 关键更正：CG 系统就是「爱情通讯 Love Link」

解出 APK 里的本地化文本表（`assets/aa/Android/.../game/staticdata/text_*.bundle`，内含 ENG/CHS/CHT/JPN/KOR 等 10 种语言，
Key@Value 格式）后，结论彻底变了：

```text
UI_LoveTalk                 @ Love Link     → 爱情通讯
UI_LoveTalk_Hero            @ Gallery       → 图鉴        ← CG 图鉴是这个 tab
T_Item_Description_Background01 @ …         → 爱情通讯-图鉴背景套件
UI_LoveTalk_IsLock3/4/2                     → 爆衣=觉醒1星 / 裸体=觉醒4星 / 性爱=觉醒6星
```

- **解锁杠杆是角色觉醒星级 `RoleData.AwakenLV`（int，0~6）**，不是 bool 开关，也不是道具持有。
- 账号数据里没有任何独立造型/解锁列表（41 容器、350 字段全查过），造型可见性完全由 `AwakenLV` 实时算出。
- 资源侧：`Assets/Game/CG/` 下 412 个目录，其中 **364 个带 `_LoveTalk.prefab`**；
  角色绑定藏在路径 `Assets/Game/CG/A0001/H007_LoveTalk_03_Controller/`（角色 H007，第 03 章）。
- 造型立绘按后缀区分：`_Dialog` 默认 / `_Dialog_1` 爆衣 / `_Dialog_2` 裸体 / `_Sex` 性爱。
- `Background`（170）/ `CGCollection`（14）确认只是图鉴背景套件与 CG 合集，不是 CG 主体。

写目标取舍：`AwakenLV` 可写（int 纯值）；`UnLockDialogIDs` 是 `List<string>` 不可写（push 触发分配必崩）；
`ItemData.StaticID` 指针替换只能"牺牲"已有道具，性价比极低。

⇒ 实施方案见 `CG_UNLOCK_PLAN.md` **v3**；字段探测用 `tools/frida_lovelink_probe.js`。

导出本地化文本的脚本（一次性，产物已存 `tools/out/text/`）：

```python
import UnityPy
env = UnityPy.load('tools/out/bundles/text.bundle')   # 从 APK 里解出的静态文本 bundle
for o in env.objects:
    if o.type.name != 'TextAsset': continue
    d = o.read()
    open('tools/out/text/%s.txt' % d.m_Name, 'wb').write(d.m_Script.encode('utf-8', 'ignore'))
```

> 这张表是排查本作一切 UI 语义的最快入口——比反编译 il2cpp 快一个数量级。

