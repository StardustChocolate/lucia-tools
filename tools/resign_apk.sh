#!/usr/bin/env bash
# 兜底方案：当旧 keystore（CN=Lucia, OU=OpenRubi）已丢失时使用。
#
# 原理：sharedUserId 只要求「两个包的签名证书相同」，不要求必须是某个特定证书。
#      所以可以生成一把新钥匙，把【游戏包】和【本工具】都用它重签，二者仍能同 uid。
#      重签只替换 APK 的签名块，dex / so / 资源一个字节都不动，内存功能不受影响。
#
# 代价：签名变化 → 手机上必须先卸载旧游戏再装，本地缓存（登录态等）会清空，账号数据在服务器不受影响。
#
# 用法：
#   tools/resign_apk.sh <输入apk> <输出apk> <keystore> <alias> <storepass> [keypass]
#
# 例：
#   tools/resign_apk.sh "D:/星陨/0717_37_1_ArkReCode_Repack.apk" \
#       "dist/ArkReCode-game-resigned.apk" "D:/keys/lucia.keystore" lucia pass123
set -euo pipefail

IN="${1:?输入 apk}"
OUT="${2:?输出 apk}"
KS="${3:?keystore 路径}"
ALIAS="${4:?alias}"
KS_PASS="${5:?storePassword}"
KEY_PASS="${6:-$KS_PASS}"

JAVA_HOME="${JAVA_HOME:-D:/Program Files/Java/jdk-17.0.20.1+1}"
ANDROID_HOME="${ANDROID_HOME:-D:/Program Files/Android/Sdk}"
export JAVA_HOME ANDROID_HOME
APKSIGNER="$ANDROID_HOME/build-tools/34.0.0/apksigner.bat"
[ -f "$APKSIGNER" ] || { echo "找不到 apksigner: $APKSIGNER"; exit 1; }
[ -f "$IN" ] || { echo "输入不存在：$IN"; exit 1; }
[ -f "$KS" ] || { echo "keystore 不存在：$KS（先用 keytool 生成）"; exit 1; }

echo "== 生成新 keystore（若不存在）=="
if [ ! -f "$KS" ]; then
  mkdir -p "$(dirname "$KS")"
  "$JAVA_HOME/bin/keytool.exe" -genkeypair -v -keystore "$KS" -alias "$ALIAS" \
    -keyalg RSA -keysize 2048 -validity 36500 \
    -storepass "$KS_PASS" -keypass "$KEY_PASS" \
    -dname "CN=Lucia, OU=OpenRubi, O=OpenRubi, C=CN" >/dev/null
  echo "已生成 $KS"
fi

echo "== 重签 =="
mkdir -p "$(dirname "$OUT")"
# 不重打包、不重压缩：apksigner 只替换 APK Signing Block，条目与对齐保持原样
"$APKSIGNER" sign \
  --ks "$KS" --ks-key-alias "$ALIAS" \
  --ks-pass "pass:$KS_PASS" --key-pass "pass:$KEY_PASS" \
  --v2-signing-enabled true --v3-signing-enabled true \
  --out "$OUT" "$IN"

echo "== 校验 =="
"$APKSIGNER" verify --print-certs "$OUT" | grep -iE "verified|SHA-256 digest" | head -4
echo
echo "完成：$OUT"
echo "接下来：把同一把 keystore 写进 keystore.properties，再跑 tools/build_signed.sh"
echo "（记得把脚本里的 EXPECTED_SHA256 改成这把新钥匙的指纹）"
