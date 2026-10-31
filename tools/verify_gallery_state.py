#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_gallery_state.py —— P0 判定：CG 解锁状态在服务器还是客户端

只读：最多两次请求（查版本 + 登录），不改任何游戏状态。

用法：
    # a) 直接用已保存的 dump 分析（不联网）
    python tools/verify_gallery_state.py --from-dump tools/out/account_login.json
    # b) 用 JWT 登录后分析
    python tools/verify_gallery_state.py --jwt-file tools/out/jwt.txt

JWT 怎么拿（不需要把密码给任何人）：
    在 D:/study/arkrecode_gvg_sniffer-main 下运行
        python -m utils.login_helper
    弹出 Chrome → 手动登录（验证码由人过）→ 终端打印 jwt。
    注意门户 /api/v2/login 强制要求 captcha token，所以"账号+密码"无法脚本化登录。

--- 2026-10-31 实测结论（本脚本跑出来的）---
    CG 图鉴的可看性 = 是否持有对应"道具"：
        Item.ItemType='Background'    共 170 条（ItemFun = BG/xxx）
        Item.ItemType='CGCollection'  共  14 条（ItemFun = BG/CarnivalNNN）
    持有状态由服务器在 AccountHandler.Login 的 ItemContainer.Items 里下发。
    账号里**不存在** IsUnlock 之类的布尔字段，也不存在 CG 列表容器。
    ⇒ P0 = 情况 B：服务器权威，内存只能改本机显示。
"""

import argparse
import base64
import json
import os
import re
import sqlite3
import sys
import time

# 这些容器名是本作登录响应里与"图鉴/收藏/剧情"相关的部分
INTERESTING = [
    'LibraryContainer', 'CollectBook', 'SceneDataContainer',
    'RoleSoundDataContainer', 'LineRoleDataContainer', 'ItemContainer',
]
KEYWORD = re.compile(r'(cg|galler|illust|album|collect|unlock|library|drama|scene)', re.I)

GAME_URL = 'https://game-arkre-labs.ecchi.xxx/Router/RouterHandler.ashx'
TOKEN_URL = 'https://sadpki-portal-v2.ebuajk.com/api/v2/token/access'
GAME_HEADERS = {
    'Content-Type': 'application/octet-stream',
    'User-Agent': 'UnityPlayer/2022.3.62f2 (UnityWebRequest/1.0, libcurl/8.10.1-DEV)',
}


# ---------------- 联网部分 ----------------

def _send(payload, session):
    time.sleep(1.5)
    r = session.post(GAME_URL, json=payload, headers=GAME_HEADERS, verify=False, timeout=30)
    r.encoding = 'utf-8'
    return r.json()


def login_by_jwt(jwt):
    import requests
    requests.packages.urllib3.disable_warnings()
    s = requests.Session()

    try:
        payload = json.loads(base64.urlsafe_b64decode(
            jwt.split('.')[1] + '=' * (-len(jwt.split('.')[1]) % 4)))
    except Exception:
        payload = {}
    is_new_sdk = 'exp' in payload
    login_id = payload.get('user_id')

    if is_new_sdk:
        h = dict(GAME_HEADERS)
        h['Authorization'] = 'Bearer ' + jwt
        r = s.post(TOKEN_URL, headers=h, verify=False, timeout=30)
        r.encoding = 'utf-8'
        d = (r.json() or {}).get('data') or {}
        login_id = d.get('userId', login_id)
        token = d.get('accessToken')
        if not token:
            raise RuntimeError('刷新 token 失败：' + json.dumps(d, ensure_ascii=False)[:300])
    else:
        token = jwt

    bulletin = _send({'data': {}, 'route': 'GameServerDBSettingHandler.QueryBulletinInfoResult'}, s)
    version = bulletin['Info']['AvailableVersions'][-1]

    return _send({
        'data': {
            'LoginID': login_id, 'Token': token, 'Version': version,
            'LoginType': 'Erolabs', 'IsNewSDK': is_new_sdk,
        },
        'route': 'AccountHandler.Login',
    }, s)


# ---------------- 分析部分 ----------------

def brief(v):
    if isinstance(v, (dict, list)):
        return '<%s len=%d>' % (type(v).__name__, len(v))
    s = repr(v)
    return s if len(s) <= 56 else s[:53] + '...'


def containers(dump):
    print('=== 登录响应顶层容器 ===')
    for k, v in dump.items():
        if isinstance(v, dict):
            print('  %-30s %s' % (k, brief(v)))
    print('')


def cg_items(dump, master_db):
    """核心判定：CG 图鉴 = 道具持有"""
    items = ((dump.get('ItemContainer') or {}).get('Items')) or []
    own = {}
    for i in items:
        sid = i.get('StaticID')
        if sid:
            own[sid] = i.get('Count')
    print('=== 背包道具：%d 条 ===' % len(items))

    if not master_db or not os.path.exists(master_db):
        print('  （找不到 master.db，跳过道具类型统计：%s）' % master_db)
        return None

    c = sqlite3.connect(master_db)
    c.row_factory = sqlite3.Row
    verdict = {}
    for t in ('CGCollection', 'Background'):
        try:
            rows = c.execute('select ID,ItemFun from Item where ItemType=?', (t,)).fetchall()
        except sqlite3.Error as e:
            print('  查询 %s 失败：%s' % (t, e))
            continue
        got = [r for r in rows if r['ID'] in own]
        verdict[t] = (len(rows), len(got), got, own)
        print('')
        print('  [%s] 目录共 %d 条，本账号持有 %d 条' % (t, len(rows), len(got)))
        for r in got:
            print('     ✔ %-18s Count=%-4s %s' % (r['ID'], own[r['ID']], r['ItemFun']))
        miss = [r['ID'] for r in rows if r['ID'] not in own]
        if miss:
            print('     ✘ 未持有 %d 条，例如：%s' % (len(miss), ', '.join(miss[:6])))
    c.close()
    return verdict


def key_scan(dump):
    """兜底：找有没有 IsUnlock 之类字段、或 CG id 形态的字符串"""
    hits = []

    def walk(n, p=''):
        if isinstance(n, dict):
            for k, v in n.items():
                pp = '%s.%s' % (p, k) if p else k
                if KEYWORD.search(k):
                    hits.append((pp, brief(v)))
                walk(v, pp)
        elif isinstance(n, list):
            for i, v in enumerate(n[:80]):
                walk(v, '%s[%d]' % (p, i))

    walk(dump)
    print('')
    print('=== 与图鉴/剧情相关的字段（前 25 条）===')
    if not hits:
        print('  （无）')
    for p, v in hits[:25]:
        print('  %-58s %s' % (p, v))
    if len(hits) > 25:
        print('  ... 还有 %d 条' % (len(hits) - 25))
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--jwt')
    ap.add_argument('--jwt-file', default='tools/out/jwt.txt')
    ap.add_argument('--from-dump', help='直接分析已保存的登录响应 JSON')
    ap.add_argument('--dump', default='tools/out/account_login.json')
    ap.add_argument('--master-db',
                    default=r'D:/study/arkrecode_gvg_sniffer-main/data/master.db')
    args = ap.parse_args()

    if args.from_dump:
        path = args.from_dump
    else:
        jwt = args.jwt or os.environ.get('LUCIA_JWT', '').strip()
        if not jwt and os.path.exists(args.jwt_file):
            with open(args.jwt_file, 'r', encoding='utf-8') as f:
                jwt = f.read().strip()
        if not jwt:
            print('没有 JWT。先跑 python -m utils.login_helper 拿 jwt，')
            print('或直接用 --from-dump 分析已有 dump。')
            return 2
        print('[*] 登录中 ...')
        try:
            dump = login_by_jwt(jwt)
        except Exception as e:
            print('[!] 登录失败：%s' % e)
            return 1
        os.makedirs(os.path.dirname(args.dump) or '.', exist_ok=True)
        with open(args.dump, 'w', encoding='utf-8') as f:
            json.dump(dump, f, ensure_ascii=False, indent=1)
        path = args.dump
        print('[*] 已保存：%s（%d KB）' % (path, os.path.getsize(path) // 1024))

    with open(path, 'r', encoding='utf-8') as f:
        dump = json.load(f)
    if not isinstance(dump, dict) or 'Info' not in dump:
        print('[!] 不是登录响应（缺 Info）：%s' % list(dump)[:8] if isinstance(dump, dict) else '')
        return 1

    containers(dump)
    verdict = cg_items(dump, args.master_db)
    key_scan(dump)

    print('')
    print('=== 判定 ===')
    if verdict and any(v[1] > 0 for v in verdict.values()):
        print('  CG/背景 道具由服务器下发，持有状态就在 ItemContainer.Items 里。')
        print('  ⇒ 情况 B：解锁 = 拥有道具，服务器权威。内存里没有 IsUnlock 布尔可翻。')
        print('  ⇒ 可行方向：把某个 ItemData 的 StaticID 指针改指向目标道具的 System.String')
        print('     （指针替换，不新增列表元素），仅影响本机显示。')
    elif verdict:
        print('  道具目录存在但本账号一条都没持有，需要换个已解锁的账号对照。')
    else:
        print('  没找到 master.db，无法判定道具类型，请手动看 ItemContainer。')
    return 0


if __name__ == '__main__':
    sys.exit(main())
