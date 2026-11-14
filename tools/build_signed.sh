#!/usr/bin/env bash
# 用 keystore.properties 里配置的 keystore 构建 release 包，并校验签名证书与已跑通内存的游戏包完全一致。
#
# 用法： tools/build_signed.sh
#
# 前置：
#   1) 把已验证过的旧 keystore 放到某个路径（建议放在仓库外，如 D:/keys/lucia.keystore）
#   2) cp keystore.properties.example keystore.properties，填入 storeFile / storePassword / keyAlias / keyPassword
#      注意：路径建议用正斜杠，反斜杠会被 .properties 当转义吃掉（脚本与 gradle 也做了兼容）
#
# 退出码：0 成功；1 配置缺失；2 指纹不匹配（装上去读不到内存）；3 构建失败；4 产物验签失败
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# 已验证过的游戏包签名指纹（CN=Lucia, OU=OpenRubi）——D:/星陨/0717_37_1_ArkReCode_Repack.apk 与 旧 lucia-tools.apk 均为它
EXPECTED_SHA256="${EXPECTED_SHA256:-f9772f267a65761bf81cb7942b456f19ae0b398aa5954e72b2fdd91a55dafd12}"
EXPECTED_DN="CN=Lucia, OU=OpenRubi, O=OpenRubi, L=X, ST=X, C=CN"

JAVA_HOME="${JAVA_HOME:-D:/Program Files/Java/jdk-17.0.20.1+1}"
ANDROID_HOME="${ANDROID_HOME:-D:/Program Files/Android/Sdk}"
export JAVA_HOME ANDROID_HOME

KEYTOOL="$JAVA_HOME/bin/keytool.exe"
APKSIGNER="$ANDROID_HOME/build-tools/34.0.0/apksigner.bat"
[ -x "$KEYTOOL" ] || KEYTOOL="keytool"
[ -f "$APKSIGNER" ] || { echo "找不到 apksigner: $APKSIGNER"; exit 1; }

if [ ! -f keystore.properties ]; then
  echo "缺少 keystore.properties"
  echo "  cp keystore.properties.example keystore.properties 然后填入 keystore 路径与口令"
  exit 1
fi

# 解析 properties（去掉 CR，反斜杠归一，忽略注释）
read_prop() {
  local k="$1"
  tr -d '\r' < keystore.properties \
    | grep -E "^[[:space:]]*${k}[[:space:]]*=" \
    | head -1 \
    | sed -E "s/^[[:space:]]*${k}[[:space:]]*=[[:space:]]*//" \
    | sed -E 's/[[:space:]]+$//' \
    | tr '\\' '/' || true
}

KS="$(read_prop storeFile)"
KS_PASS="$(read_prop storePassword)"
ALIAS="$(read_prop keyAlias)"
KEY_PASS="$(read_prop keyPassword)"

[ -n "$KS" ] || { echo "keystore.properties 缺 storeFile"; exit 1; }
case "$KS" in
  /*|[A-Za-z]:/*) : ;;
  *) KS="$ROOT/$KS" ;;
esac
[ -f "$KS" ] || { echo "keystore 文件不存在：$KS"; exit 1; }

echo "== 1/4 校验 keystore 证书指纹 =="
echo "keystore : $KS"
# 强制英文输出，否则中文 locale 下键名被翻译，grep 抓不到
if ! CERT_INFO="$("$KEYTOOL" -J-Duser.language=en -J-Duser.country=US \
      -list -v -keystore "$KS" -storepass "$KS_PASS" -alias "$ALIAS" 2>&1)"; then
  echo "$CERT_INFO" | tail -5
  echo "读取 keystore 失败：检查 storePassword / keyAlias 是否正确"
  exit 1
fi
CERT_SHA="$(echo "$CERT_INFO" | grep -iE "SHA-?256:" | head -1 | sed -E 's/.*SHA-?256:[[:space:]]*//' | tr -d ' :\r' | tr 'A-F' 'a-f' || true)"
CERT_DN="$(echo "$CERT_INFO" | grep -iE "^Owner:" | head -1 | sed -E 's/^Owner:[[:space:]]*//' | tr -d '\r' || true)"

if [ -z "$CERT_SHA" ]; then
  echo "无法从 keystore 解析出证书指纹，输出如下："
  echo "$CERT_INFO" | head -20
  exit 1
fi

echo "DN       : $CERT_DN"
echo "SHA-256  : $CERT_SHA"
if [ "$CERT_SHA" != "$EXPECTED_SHA256" ]; then
  echo
  echo "!! 指纹与已验证的游戏包不一致"
  echo "   游戏包 : $EXPECTED_SHA256  ($EXPECTED_DN)"
  echo "   本keystore: $CERT_SHA"
  echo "   继续构建出的包无法与游戏同 uid，读不到内存。"
  exit 2
fi
echo "OK —— 与游戏包签名完全一致，可以同 uid 读内存"

echo
echo "== 2/4 构建 release =="
./gradlew :app:assembleRelease --no-daemon --console=plain || { echo "构建失败"; exit 3; }

APK="app/build/outputs/apk/release/app-release.apk"
[ -f "$APK" ] || { echo "未找到产物 $APK"; exit 3; }

echo
echo "== 3/4 校验产物签名 =="
OUT_SHA="$("$APKSIGNER" verify --print-certs "$APK" 2>/dev/null \
  | grep -i "SHA-256 digest" | head -1 | sed -E 's/.*digest:[[:space:]]*//' | tr -d ' ' | tr 'A-F' 'a-f')"
echo "产物 SHA-256 : $OUT_SHA"
if [ "$OUT_SHA" != "$EXPECTED_SHA256" ]; then
  echo "!! 产物签名与预期不一致，装配后读不到内存"
  exit 4
fi

echo
echo "== 4/4 归档 =="
mkdir -p dist
OUT="dist/LuciaTools-lovetalk-release.apk"
cp "$APK" "$OUT"
ls -lh "$OUT"
echo
echo "完成：$OUT"
echo "安装前先卸载旧的 lucia-tools（签名相同可 -r 覆盖安装，但建议卸载避免残留）"
