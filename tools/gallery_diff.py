#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gallery_diff.py —— 对比两次 gc_probe_class 的 dump，定位"解锁状态"字段

用法：
    python tools/gallery_diff.py dump_before.json dump_after.json
    python tools/gallery_diff.py before.json after.json --offset 0x28
    python tools/gallery_diff.py before.json after.json --only-changed-objects

输入是 NativeBridge.nativeGalleryProbe() 写出的 JSON，形如：
    {"cls":"GalleryData","klass":"0x7f...","objs":[{"a":"0x7f...","w":["0x0","0x1",...]}]}

判读原则（重要）：
  * 只解锁了 1 个 CG 的情况下，真正的"逐条解锁标记"应该只有 1 个对象变化。
  * 所有对象同时变化的偏移，通常是整体刷新时间戳/计数，不是逐条状态。
  * 如果两次 dump 的对象地址集合完全不同，说明对象被重建了
    —— 这通常意味着进图鉴时服务器重新下发了列表，内存写入会被冲掉。
"""

import argparse
import json
import sys
from collections import defaultdict


def load(path):
    with open(path, "r", encoding="utf-8") as f:
        d = json.load(f)
    objs = {}
    for o in d.get("objs", []):
        addr = int(o["a"], 16) if isinstance(o["a"], str) else int(o["a"])
        words = [int(w, 16) if isinstance(w, str) else int(w) for w in o.get("w", [])]
        objs[addr] = words
    return d.get("cls", "?"), d.get("klass", "?"), objs


def fmt(v):
    return "0x%x" % v


def classify(before, after):
    """给变化打个类型猜测，只用于人工判读，不参与逻辑。"""
    if before == 0 and after == 1:
        return "bool 0->1  ★最像解锁标记"
    if before == 1 and after == 0:
        return "bool 1->0  ★最像解锁标记"
    if before == 0 and after not in (0,):
        return "0 -> 非零"
    if 0 < before < 0x10000 and 0 < after < 0x10000:
        return "小整数 %d -> %d (差 %+d)" % (before, after, after - before)
    if before > 0xFFFF_FFFF and after > 0xFFFF_FFFF:
        return "指针变化（对象引用换了）"
    return "其他"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("before")
    ap.add_argument("after")
    ap.add_argument("--offset", default=None,
                    help="只看某个偏移，如 0x28（相对对象头，即 words 下标*8+0x10）")
    ap.add_argument("--only-changed-objects", action="store_true",
                    help="额外打印变化对象前后完整 word 列表")
    ap.add_argument("--max-detail", type=int, default=8,
                    help="每个偏移最多打印多少条明细（默认 8）")
    args = ap.parse_args()

    cls_a, klass_a, A = load(args.before)
    cls_b, klass_b, B = load(args.after)

    print("类: %s" % cls_a)
    print("对象数: before=%d  after=%d" % (len(A), len(B)))
    if cls_a != cls_b:
        print("[!] 两次 dump 的类名不同：%s vs %s，确认是不是传错了参数" % (cls_a, cls_b))
    if klass_a != klass_b:
        print("[!] klass 指针不同（%s vs %s），说明两次之间类被重新加载过，结果的地址不可直接比较"
              % (klass_a, klass_b))

    only_a = set(A) - set(B)
    only_b = set(B) - set(A)
    common = set(A) & set(B)
    if only_a or only_b:
        print("")
        print("[!] 地址集合不一致：before 独有 %d 个，after 独有 %d 个" % (len(only_a), len(only_b)))
        if len(common) == 0:
            print("    完全没有重叠的地址 —— 对象被整体重建了。")
            print("    这通常意味着每次进图鉴服务器都会重发列表，内存写入很可能被覆盖。")
        else:
            print("    可能是 GC 搬动了对象；下面只比较地址相同的 %d 个对象。" % len(common))

    # 偏移 -> [(objaddr, before, after)]
    changes = defaultdict(list)
    obj_changed = defaultdict(list)  # objaddr -> [(idx, before, after)]

    for addr in sorted(common):
        wa, wb = A[addr], B[addr]
        n = min(len(wa), len(wb))
        for i in range(n):
            if wa[i] != wb[i]:
                off = 0x10 + i * 8
                changes[off].append((addr, wa[i], wb[i]))
                obj_changed[addr].append((i, wa[i], wb[i]))

    want_off = None
    if args.offset:
        want_off = int(args.offset, 16) if args.offset.lower().startswith("0x") else int(args.offset, 16)

    if not changes:
        print("")
        print("没有任何字段发生变化。可能原因：")
        print("  1) 两次 dump 之间游戏内状态没变（没真正解锁任何 CG）")
        print("  2) 解锁状态不在这个类里（换个候选类）")
        print("  3) 状态存的是容器/列表而非对象字段（去看容器类的 Datas/_items）")
        return 0

    print("")
    print("变化偏移汇总（按变化对象数升序 —— 越少越可能是逐条标记）:")
    print("  %-10s %-14s %s" % ("offset", "变化对象数", "推测"))
    ordered = sorted(changes.items(), key=lambda kv: (len(kv[1]), kv[0]))
    for off, items in ordered:
        if want_off is not None and off != want_off:
            continue
        sample = items[0]
        print("  +0x%-7x %-12s %s" % (off, "%d/%d" % (len(items), len(common)),
                                      classify(sample[1], sample[2])))

    if want_off is not None and want_off not in changes:
        print("")
        print("偏移 +0x%x 在两次 dump 之间没有变化。" % want_off)

    print("")
    print("明细:")
    for off, items in ordered:
        if want_off is not None and off != want_off:
            continue
        print("  偏移 +0x%x（word #%d，%d/%d 个对象变化）:"
              % (off, (off - 0x10) // 8, len(items), len(common)))
        for addr, b, a in items[: args.max_detail]:
            print("    对象 0x%x : %s -> %s   %s" % (addr, fmt(b), fmt(a), classify(b, a)))
        if len(items) > args.max_detail:
            print("    ... 还有 %d 条" % (len(items) - args.max_detail))

    if args.only_changed_objects:
        print("")
        print("变化对象的完整 word 表（便于看相邻字段）:")
        for addr in sorted(obj_changed):
            print("  对象 0x%x:" % addr)
            wa, wb = A[addr], B[addr]
            for i in range(min(len(wa), len(wb))):
                mark = " <-- 变化" if wa[i] != wb[i] else ""
                print("    +0x%-6x %-20s %-20s%s"
                      % (0x10 + i * 8, fmt(wa[i]), fmt(wb[i]), mark))

    # 自动推荐
    print("")
    cands = [(len(v), off) for off, v in changes.items() if len(v) <= 3]
    if cands:
        cands.sort()
        print("建议优先验证的偏移: " + ", ".join("+0x%x(%d个对象)" % (off, c) for c, off in cands[:5]))
        print("下一步：用 gc_probe_class 的同偏移，只改其中一个地址试写，观察图鉴是否只有一张变可点。")
    else:
        print("没有'少数对象变化'的偏移 —— 可能解锁的是整体状态而非逐条标记，")
        print("或者两次 dump 之间你解锁了多个 CG。建议只解锁 1 个再重跑。")

    return 0


if __name__ == "__main__":
    sys.exit(main())
