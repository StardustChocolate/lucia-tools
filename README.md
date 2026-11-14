
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
