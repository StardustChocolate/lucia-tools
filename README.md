
<div align="center">

# 璐茜娅的魔方 · LuciaTools

<img src="./app/src/main/assets/avatars/H006.png" width="200" alt="Lucia">

星陨计划安卓辅助工具 App，本地、轻量、强拓展✨

[![License](https://img.shields.io/badge/License-GPLv3-blue.svg?style=flat-square)](./LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Android-3DDC84.svg?style=flat-square&logo=android&logoColor=white)](#)
[![Kotlin](https://img.shields.io/badge/Kotlin-7F52FF.svg?style=flat-square&logo=kotlin&logoColor=white)](#)
[![API](https://img.shields.io/badge/API-24%2B-orange.svg?style=flat-square)](#)
[![Arch](https://img.shields.io/badge/ABI-arm64--v8a-FA7343.svg?style=flat-square)](#)
[![QQ Group](https://img.shields.io/badge/QQ%20Group-1032070842-5865F2.svg?style=flat-square)](https://qm.qq.com/q/YsvXane7Wq)

> 本项目仅作技术研究，请勿用于任何破坏游戏公平性、商业化或其他违反游戏条款的用途。

</div>

## ✨功能

- **导出装备表** — 读取背包全部装备，导出到下载文件夹（CSV/JSON）。
- **导出装备图** — 读取背包全部装备，计算八维强度雷达图（Lp 范数评分）+ 顶配攻/生/防/抗/速求解，渲染成统计卡片（PNG）。
- **秒杀** — 实时修改已觉醒角色觉醒攻击加成，仅在部分 PVE 中生效。
- **我方速度 280** — 战斗中把我方全部角色的最终速度写为 280（按快照还原）。
- **战斗9倍速（暂不可用）** — 目标是让战斗画面/演出整体快 9 倍（全局时间缩放，**不改角色速度**）。会与游戏切档冲突的实现已撤下，正在定位游戏的全局时间缩放变量。
- **解锁未获得角色 CG** — 将未获得角色的图鉴条目伪装成主线 CG 条件，让未拥有角色也能查看并播放爱情通讯 CG。仅本机内存修改，重启游戏自动恢复。

## 🖼️ 解锁未获得角色 CG

### 使用方式

1. 启动游戏，进入 **爱情通讯 → 图鉴**，先打开一次，让未获得角色列表加载。
2. 打开 LuciaTools 悬浮窗 → **菜单 → 解锁CG**。
3. 退出图鉴后重新进入，未获得角色的 CG 会显示 `NEW` 角标，可直接点击播放。
4. 再次点击 **恢复未获得CG**，按快照还原所有被修改的图鉴条目。

解锁判定使用模板条目的条件字段；但「观看剧情」使用的 `DramaID` 不抄模板，而是取目标角色自己的最后一个非空剧情 ID。因此爱情通讯动态 CG 会关联到该角色通讯里的最后一个 CG 视频，而不是模板角色的剧情。目标角色自己的 `ID / RoleID / Prefab / PrefabIcon / RoleMotions / ActivityIDs` 全部保留。

### 边界与注意事项

- 仅修改当前游戏进程内存，**重启游戏即恢复**，不会写入账号数据或服务器。
- 修改后未获得角色的图鉴会大量出现 `NEW` 角标，属于正常现象。
- 点击动态 CG 的「观看剧情」会播放目标角色自己的最后一段通讯视频；没有剧情 ID 的角色不会借用模板角色的剧情。
- 默认使用 `H150_04` 作为主线条件模板；游戏版本更新后如模板失效，可通过 ADB 参数替换 donor。
- 需要和游戏使用同一签名、同一 `sharedUserId` 的特殊游戏包，否则无法读取游戏内存。
- 请勿在修改后参与 PVP、公会战等可能被服务器复算的玩法。

### ADB 调试

```bash
# 预览：只统计，不写内存
adb shell am start -n com.openrubi.luciatools/.MainActivity --ez cgUnlock true --es roles @UNAcquired --ei dry 1

# 解锁全部未获得角色
adb shell am start -n com.openrubi.luciatools/.MainActivity --ez cgUnlock true --es roles @UNAcquired --es donor H150_04 --ei dry 0

# 只解锁指定角色
adb shell am start -n com.openrubi.luciatools/.MainActivity --ez cgUnlock true --es roles H133,H160 --es donor H150_04 --ei dry 0

# 按快照恢复
adb shell am start -n com.openrubi.luciatools/.MainActivity --ez cgUnlock true --ei restore 1
```

调试结果写入 `Android/data/com.openrubi.luciatools/files/cg_unlock.txt` 和 `cg_unlock.snap`。

## ⚔️ 战斗内功能

| 功能 | 作用 | 说明 |
|------|------|------|
| 秒杀 | 把每个觉醒等级的攻击增量改成 999999 | 改的是 `RoleAwakenStaticData.PropertySetting` 的活副本，改完立刻生效，重启游戏恢复 |
| 最大重复战斗次数 999 | 改 **VIP 配置表** `Game.VIPStaticData` 每行的 `+0x40`（`AutoBattleCount`）为 999，**不动 VIP 等级** | 队伍编成界面「重复战斗次数上限」从 `x/18` 变成 `x/999`；<br>⚠️ `RepeatBattleSetting.Count` 与界面上的次数**无关**，写它 999 会让游戏真的排 999 场请求而卡死 |
| 角色高速（原「我方速度 280」） | **双路写入**：① `RoleStaticData.PropertySetting["Speed"]` 倍率（非战斗可改、角色面板可见）；② `AbilityData.PropertySetting["Speed"]` 绝对速度（战斗内即时生效） | 面板速度 = `100 × 倍率 + 装备加成`，所以倍率取 `280/100 = 2.8`；两条是**不同数据**，必须一起写 |
| 战斗9倍速 | ⚠️ **暂不可用**（点击只提示，不写内存） | 曾用「全场 `PropertySetting.Speed` ×9」，玩家点游戏自带倍速按钮切档时**进程崩溃**，已撤下 |

> **★ 速度类数值必须改 `PropertySetting` 活副本，不是 `AbilityData` 的展示字段。**
> 这是「我方速度 280」此前两轮无效的真正根因：`AbilityData.Speed`（+0x28）改完
> 内存读回确实是 280，但游戏行动条读的是 `AbilityData → PropertySetting(+0x30)
> → _PropertyCloneValueMap(+0x18)["Speed"]` 里的 **double**（`PropertyValue+0x18`）。
> 两者可以不同（实测 280 vs 130）。秒杀能生效也是同一原因 —— 它改的是同一字典里的
> `AttackValue`。
>
> 两个功能都要**在战斗中**点。内存里会残留前几场战斗的角色对象，旧版本用「出现次数
> 最多的 BattleData」猜当前战斗，结果稳定改到残留对象上。
>
> **★ 9 倍速的语义（用户澄清）**：是「战斗画面/演出整体快 9 倍」的**全局时间缩放**，
> 不是把角色速度堆 9 倍；角色速度与 280 那个功能互不相干。
>
> **踩过的坑**：曾把全场 `PropertySetting["Speed"]` 乘以 9（130→1170），玩家在战斗里
> 点游戏自带的倍速按钮（x1→x3）时**游戏进程直接崩溃** —— 数值超出游戏允许范围，
> 切档重算时崩。该实现已撤下，`lt_battle_speed_set()` 现在只返回 -5 不写内存。
>
> 游戏重启后内存改动失效，需要重新点一次开关。

### 结构备忘（运行时实测，Unity IL2CPP / metadata v31）

- `Game.BattleRoleData`：`+0x28` AbilityData、`+0x38` ID(`1-0-3`)、`+0x40` Camp、`+0x80` MonsterSetting（我方为空）
- `Game.AbilityData`：`+0x14` MaxHP、`+0x1c` Attack、`+0x20` Defence、`+0x24` DodgeRate、
  `+0x28` Speed(float，**只是展示副本，行动条不读它**)、`+0x30` PropertySetting
- `Game.BattleRoleTimeInfo`：`+0x10` NowTimeLength(float，行动条进度 0~1)
- `Game.PropertySetting`：`+0x10` SourceValues、`+0x18` `_PropertyCloneValueMap`（**战斗数值真源**）
  - 字典布局：`dict+0x18`=entries 指针、`dict+0x20`=count、`entries+0x18`=容量、
    `entries+0x20` 起每项 `0x18` 字节 `{ hash, key*, value* }`
  - key 是 **Il2CppString**（`+0x10` 长度 / `+0x14` UTF-16），必须用 `ms_read_rstr` 读，
    当裸 C 字符串读会永远匹配不上
  - `PropertyValue` 的数值在 `value+0x18`（**double**，8 字节）
  - 键含 `HP / Attack / Defence / Speed / SpeedValue / AttackValue / HPRate …`
- `Game.RoleStaticData`（角色基础数据，227 个实例）：`+0x10` ID(`H###`)、`+0x190` PropertySetting
  - 这里的 `Speed` 是**成长倍率**（1.0~1.2），**不是绝对速度**：
    面板速度 = `100 × 倍率 + 装备/印记加成`。改成 2.8 → 面板 280（+装备加成）
  - 属于养成配置（和秒杀同一层），所以**非战斗状态也能改，切角色刷新面板即可见**
  - 实例扫描必须用 ID 格式（`H` + 3 位数字）校验，否则会命中大量配置表假阳性
- `Game.RoleAwakenStaticData`（觉醒数据）：`+0x10` RoleID、`+0x18` LV、`+0x30` PropertySetting
  - 字典键只有 `HPValue / AttackValue / AttackRate`（秒杀改的是 `AttackValue`），**没有 Speed**
- `Game.VIPStaticData`（**VIP 配置表**，2026-11-07 定位成功）：整张表是连续数组，
  元素**步长 0xC0**、按等级**倒序**排列（等级越低地址越高）。行内布局：
  - `+0x00` klass（`VIPStaticData`）、`+0x10` VIP 等级(int)、
    `+0x18` / `+0x28` 名字与描述字符串指针、`+0x20` 升级所需经验
  - **`+0x40` = `AutoBattleCount`（每日可重复战斗次数上限）**：VIP4=18、VIP5=20、
    VIP6=22、VIP7=30 —— 这解释了「不同人 16/18/20」
  - 校验一行的可靠条件：`+0x10` ∈ 0..30、`+0x18` 与 `+0x28` 都是合法字符串指针、
    `+0x40` ∈ 0..100000
- `Game.RepeatBattleSetting`（**不是**界面那个 18/18 的来源，别再走弯路）：
  `+0x10` Enable、`+0x14` Count、`+0x38` LimitedCount
  - `Count` 是「这一局实际重复几次」，写 999 游戏会真的排 999 场请求 → **卡在转圈圈**
  - `LimitedCount` 写 50 界面毫无变化（实测），说明它不参与上限显示
  - 界面 `x/y` 的 `x` 与 `y` **不相邻存储**（`iscan 1,18` ∩ `iscan 18,18` = 空集），
    所以按「相邻两个 int」做差分永远找不到它
- `Game.VIPInfo`（玩家 VIP 数据）：`+0x10` 服务器 _id、`+0x18` LV、`+0x20` Score
  （VIP4 实测 `LV=4`、`Score=180`）。**改上限不需要动这个**。
- `libunity.so + 0x12dae28`：**不是** `Time.timeScale`，是音频压缩器参数表
  （0.1/1.0/10.0 是 Release Time 的 min/default/max，后面跟着 "Release Time" 字符串）
- `Game.BattlePlayerSetting`（TypeDef **2673**）：倍速状态类。成员（元数据字符串表
  同一分块 + `MethodDefinition.declaringType` 精确反查确认）：
  `SpeedLevel`(int 档位，含 get/set)、`NowSpeed` / `NowSpeedString`(只读计算属性)、
  `GetTimeScaleByLevel`、`ChangeSpeedToNext`(x1/x2/x3 按钮调用)、
  `IsAutoFight` / `isAutoFight`。另有 `BattleSpeedUpLv`(VIP 倍速上限)。

### 9 倍速定位记录（未完成）

**当前状态**：功能已停用，点击只提示、不写任何游戏内存。需要找到游戏的全局时间缩放变量。

**本轮新增的证伪（2026-11-07 傍晚）**：

- Unity `Time.timeScale` 不在 `libunity.so` 的可写段：该段 float 1.0 共 194 个候选，
  整体写成 0.05 后画面变化率反而**变大**（0.025 → 0.081），没有任何"冻结"效果。
- `maximumDeltaTime(0.33333334)` 全内存 2411 个命中，**libunity.so 范围内 0 个**。
  两个低地址命中是动画缓动曲线查找表（0.0667→0.9333 均匀序列），
  `0x4000611a2d4c` 落在 native 代码段（x86 机器码），都不是 TimeManager。
- `libunity.so` 里 float ∈ [5,100000] 只有 262 个，两次快照**没有任何持续增长的量**
  → `TimeManager` 不在这块可写数据区。
- ★ **不要再放大 `PropertySetting["Speed"]` 来做倍速**：×9（130→1170）超出游戏允许
  范围，玩家点游戏自带倍速按钮（x1→x3）时进程崩溃。速度类数值只能在合理区间内调整。

**已证伪的方向**（不要再重复）：

- `Time.timeScale` 不在 `libunity.so` 数据段：该段 float 1.0 只有 194 个候选，
  集中在 `0x4000220d8xxx ~ 0x4000220edxxx`（音频参数表、`±1.0` 静态矩阵），
  切档与暂停前后 `chg` 均为 `changed=0`。
- `libunity.so`/`libil2cpp.so` 数据段里 `0.33333334`(maximumDeltaTime) 只命中 2 个地址，
  `0.02` 只命中 10 个，全是 libil2cpp 常量表；附近没有 Time 状态结构。
- `BattleRoleTimeInfo.NowTimeLength` 增量放大无效：`act=battle9 rc=3` 但 `tickstat rc=0`，
  说明该字段不按「累加型行动条」变化。
- 全内存 **float** 1.0 / 2.0 / 3.0 三档交集只剩 `0x7f8f668922e8`，
  但战斗外读到 `-1`/`0`、周边内存被 GC 反复复用 → 假阳性。
- 全内存 **int** 1 / 2 / 3 三态闭环（x2→x3→x1）得 628 个候选，其中 ~500 个密集落在
  `0x7f8f02c…0x7f8f043` 对象池；把其余 48 个分散地址批量写成 int 9 后，
  倍速按钮仍显示 x1、游戏无变化 → `SpeedLevel` 不在其中。
  推测档位可能是 **0/1/2** 而非 1/2/3（`intscan` 只记录 1..3，漏掉了 0）。

**元数据布局校准**（`tools`/`mdparse.py` 与临时脚本）：

- metadata v31 `TypeDefinition` 88B：`+8` byvalTypeIndex、`+36` fieldStart、
  `+68` 起依次 method_count / property_count / field_count。
- `MethodDefinition` stride = **24**；`declaringType`(+4) 直接是 TypeDef 索引。
- 探针 `cls` 的字段表搜索已放宽到 klass `+0x10..0x220`（两轮，宽范围多校验一个
  FieldInfo 防误报）；但对 `BattlePlayerSetting` 仍会命中别的配置表字段，
  说明它很可能是静态/单例类，状态在 `static_fields` 数据区而非普通堆实例。

**下一步**：需要确认「全局速度」变量的真实取值（含 0 档）或拿到同类工具的说明，
再针对 `BattlePlayerSetting` 的 `static_fields` 区做定位；实现时必须加战斗检测，
主页/大厅一律拒绝写入。

### ADB 调试

```bash
# 直接执行动作，结果写 files/act.txt
adb shell am start -n com.openrubi.luciatools/.MainActivity --es act speed280   # 我方速度 280
adb shell am start -n com.openrubi.luciatools/.MainActivity --es act speed0     # 还原速度
adb shell am start -n com.openrubi.luciatools/.MainActivity --es act battle9    # 9 倍速
adb shell am start -n com.openrubi.luciatools/.MainActivity --es act battle0    # 还原倍速
adb shell am start -n com.openrubi.luciatools/.MainActivity --es act kill1      # 开秒杀
adb shell am start -n com.openrubi.luciatools/.MainActivity --es act kill0      # 关秒杀
adb shell am start -n com.openrubi.luciatools/.MainActivity --es act repeat999  # 重复战斗上限改 999
adb shell am start -n com.openrubi.luciatools/.MainActivity --es act repeat0    # 还原重复战斗上限
adb shell am start -n com.openrubi.luciatools/.MainActivity --es act tickstat   # 行动条加速写入累计次数
```

## 📦 开箱即用

- 下载本项目apk和特殊的游戏apk
- 安装 - 运行即可

## ⚙️ 构建（开发者）

需要：Android SDK（compileSdk 34 / build-tools）、NDK（CMake 3.22.1，编 arm64 native）、JDK 17。

```bash
# 1. 配置签名（App 必须和游戏同 keystore）
cp keystore.properties.example keystore.properties
#   编辑 keystore.properties 填入你自己的 keystore 路径和口令
#   生成 keystore：
keytool -genkeypair -v -keystore lucia.keystore -alias lucia \
  -keyalg RSA -keysize 2048 -validity 36500 \
  -storepass <PASS> -keypass <PASS> -dname "CN=lucia"

# 2. 构建 release 并校验签名
bash tools/build_signed.sh

# 3. 安装
adb install -r dist/LuciaTools-lovetalk-release.apk
```

构建产物已配置为用 `keystore.properties` 指定的 keystore 自动签名，无需额外 zipalign/apksigner。

## 📋 目录

| 路径 | 说明 |
|------|------|
| `app/src/main/cpp/` | native 内存引擎（IL2CPP 扫描、proc-mem 读写、装备枚举、CG 解锁） |
| `app/src/main/cpp/cg_unlock_core.c` | 未获得角色 CG 解锁与快照恢复 |
| `app/src/main/java/.../feature/` | 装备导出、强度评分、卡片渲染、顶配求解 |
| `app/src/main/java/.../float/` | 悬浮窗 / 径向菜单 / 功能窗 UI |
| `app/src/main/java/.../mem/` | JNI bridge |

## 📊 评分模型

每个维度先按"递减套"规则构造排列表，再用 **Lp 范数** 对列表求总评分（`norm = (Σ value^P)^(1/P)`，P 用于平衡峰值与数量的贡献，可调），映射到每维独立满分阈值得 0~10 分绘制雷达图。阈值在 [`StatScorer.kt`](app/src/main/java/com/openrubi/luciatools/feature/StatScorer.kt) 顶部常量，可自行校准。

## 免责声明

本项目为安卓技术学习产物，不会上传任何您的本地数据，若有侵权请联系项目作者删除相关内容。请勿用于任何破坏游戏公平性、商业化或其他违反游戏条款的用途。使用本项目造成的任何后果由使用者自负。

## License

[GNU General Public License v3.0](LICENSE)
