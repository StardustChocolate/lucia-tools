'use strict';
/*
 * frida_lovelink_probe.js —— 爱情通讯(Love Link) / 造型解锁 相关类的运行时字段发现
 *
 * 背景：本作 global-metadata.dat 被加密，Il2CppDumper 静态拿不到任何类名与字段名；
 *       但 libil2cpp.so 的 il2cpp_* 导出在运行时可用（metadata 已解密进内存），
 *       所以只能运行时问 il2cpp 要"类名 + 字段名 + 偏移 + 类型"。
 *
 * 本脚本纯只读：只调 getter，不 hook、不写内存、不改任何游戏数据。
 *
 * 用法（设备需 root + frida-server）：
 *   frida -U -f com.nerversoft.ark.recode -l tools/frida_lovelink_probe.js --no-pause
 *   frida -U -n com.nerversoft.ark.recode -l tools/frida_lovelink_probe.js   # 附加到已启动的进程
 *
 * 输出：
 *   第一部分：精确类名表（EXACT_CLASSES）命中的类，无论关键词如何都打印全部字段
 *   第二部分：名字命中 KEYWORD 的类，用于发现漏网的类
 *   汇总会给出可直接抄进 lovetalk_core.c 常量区的偏移表
 */

// ======== 可调参数 ========
// 精确查找的类名（不限命名空间）。这些是重点怀疑对象。
const EXACT_CLASSES = [
    'RoleData',           // 角色数据：AwakenLV / Intimacy 等应在此
    'RoleDataBase',
    'LineRoleData',       // 爱情通讯：IntimacyLV / AwakenLV / IsFocus
    'LineDramaData',      // DramaInfoList 元素：UnLockDialogIDs / LastLineDialogID
    'ItemData',           // 图鉴背景 / CGCollection 道具
    'RoleSkinData',       // 造型解锁状态（若存在独立的造型列表）
];

// 关键词发现（类名或命名空间命中即输出）
const KEYWORD = /(lovetalk|love_?talk|line_role|linerole|lineroom|skin|nude|undress)/i;

// 命名空间过滤：null = 不限；/^Game$/ 之类可缩小范围
const NS_FILTER = null;

const MAX_FIELDS = 60;
const MAX_CLASS_HITS = 80;
// ==========================

function findIl2cppModule() {
    for (const m of Process.enumerateModules()) {
        if (/il2cpp/i.test(m.name)) return m;
    }
    return null;
}

const mod = findIl2cppModule();
if (!mod) {
    console.log('[-] 没找到 libil2cpp.so。现有模块：' +
        Process.enumerateModules().map(m => m.name).join(', '));
    throw new Error('libil2cpp not found');
}
console.log('[*] module: ' + mod.name + ' @ ' + mod.base + '  size=' + mod.size);

const NEED = [
    'il2cpp_domain_get',
    'il2cpp_domain_get_assemblies',
    'il2cpp_assembly_get_image',
    'il2cpp_class_get_name',
    'il2cpp_class_get_namespace',
    'il2cpp_class_get_fields',
    'il2cpp_field_get_name',
    'il2cpp_field_get_offset',
    'il2cpp_field_get_type',
    'il2cpp_field_get_flags',
    'il2cpp_type_get_name',
    'il2cpp_class_get_methods',
    'il2cpp_method_get_name',
    'il2cpp_method_get_param_count',
];

const missing = [];
const exp = {};
for (const name of NEED) {
    const p = mod.findExportByName(name);
    if (p === null) { missing.push(name); continue; }
    exp[name] = p;
}
if (missing.length) {
    console.log('[!] 缺失导出: ' + missing.join(', '));
    console.log('    缺 il2cpp_class_get_fields 时拿不到偏移，只能退到"两次 dump 差分"方案。');
}

const P = Process.pointerSize;
const SZ = 'pointer';

const f = {};
if (exp.il2cpp_domain_get) f.domainGet = new NativeFunction(exp.il2cpp_domain_get, SZ, []);
if (exp.il2cpp_domain_get_assemblies)
    f.domainGetAssemblies = new NativeFunction(exp.il2cpp_domain_get_assemblies, SZ, [SZ, SZ]);
if (exp.il2cpp_assembly_get_image)
    f.assemblyGetImage = new NativeFunction(exp.il2cpp_assembly_get_image, SZ, [SZ]);
if (exp.il2cpp_class_get_name)
    f.classGetName = new NativeFunction(exp.il2cpp_class_get_name, 'pointer', [SZ]);
if (exp.il2cpp_class_get_namespace)
    f.classGetNamespace = new NativeFunction(exp.il2cpp_class_get_namespace, 'pointer', [SZ]);
if (exp.il2cpp_class_get_fields)
    f.classGetFields = new NativeFunction(exp.il2cpp_class_get_fields, SZ, [SZ, SZ]);
if (exp.il2cpp_field_get_name)
    f.fieldGetName = new NativeFunction(exp.il2cpp_field_get_name, 'pointer', [SZ]);
if (exp.il2cpp_field_get_offset)
    f.fieldGetOffset = new NativeFunction(exp.il2cpp_field_get_offset, SZ, [SZ]);
if (exp.il2cpp_field_get_type)
    f.fieldGetType = new NativeFunction(exp.il2cpp_field_get_type, 'pointer', [SZ]);
if (exp.il2cpp_field_get_flags)
    f.fieldGetFlags = new NativeFunction(exp.il2cpp_field_get_flags, 'uint32', [SZ]);
if (exp.il2cpp_type_get_name)
    f.typeGetName = new NativeFunction(exp.il2cpp_type_get_name, 'pointer', [SZ]);
if (exp.il2cpp_class_get_methods)
    f.classGetMethods = new NativeFunction(exp.il2cpp_class_get_methods, SZ, [SZ, SZ]);
if (exp.il2cpp_method_get_name)
    f.methodGetName = new NativeFunction(exp.il2cpp_method_get_name, 'pointer', [SZ]);
if (exp.il2cpp_method_get_param_count)
    f.methodGetParamCount = new NativeFunction(exp.il2cpp_method_get_param_count, 'uint32', [SZ]);

const FIELD_ATTRIBUTE_STATIC = 0x10;

function cstr(ptr) {
    if (!ptr || ptr.isNull()) return '';
    try { return ptr.readCString() || ''; } catch (e) { return ''; }
}

function listFields(klass) {
    if (!f.classGetFields) return [];
    const iter = Memory.alloc(P);
    iter.writePointer(ptr(0));
    const out = [];
    for (let guard = 0; guard < 400; guard++) {
        const fi = f.classGetFields(klass, iter);
        if (fi.isNull()) break;
        out.push({
            name: cstr(f.fieldGetName(fi)),
            off: f.fieldGetOffset ? f.fieldGetOffset(fi).toNumber() : -1,
            flags: f.fieldGetFlags ? f.fieldGetFlags(fi) : 0,
            typeName: (f.fieldGetType && f.typeGetName) ? cstr(f.typeGetName(f.fieldGetType(fi))) : '?',
        });
        if (out.length >= MAX_FIELDS) break;
    }
    return out;
}

function listMethods(klass) {
    if (!f.classGetMethods) return [];
    const iter = Memory.alloc(P);
    iter.writePointer(ptr(0));
    const out = [];
    for (let guard = 0; guard < 600; guard++) {
        const mi = f.classGetMethods(klass, iter);
        if (mi.isNull()) break;
        const name = cstr(f.methodGetName(mi));
        if (/unlock|awaken|skin|locket|open|enable|set_/i.test(name)) {
            out.push(name + '/' + (f.methodGetParamCount ? f.methodGetParamCount(mi) : 0));
        }
        if (out.length > 40) break;
    }
    return out;
}

function dumpClass(klass, tag) {
    const name = cstr(f.classGetName(klass));
    const ns = cstr(f.classGetNamespace(klass));
    const full = ns ? ns + '.' + name : name;
    const fields = listFields(klass);
    const methods = listMethods(klass);

    console.log('');
    console.log('[' + tag + '] ' + full + '   klass=' + klass);
    for (const fd of fields) {
        const offStr = fd.off >= 0 ? '0x' + fd.off.toString(16) : '??';
        const isStatic = (fd.flags & FIELD_ATTRIBUTE_STATIC) !== 0;
        console.log('      ' + (isStatic ? 'static ' : '       ') +
            (offStr + '        ').slice(0, 10) +
            (fd.name + '                        ').slice(0, 26) +
            fd.typeName);
    }
    if (methods.length) console.log('      methods: ' + methods.join(', '));
    return { full, fields };
}

function main() {
    if (!f.domainGet || !f.domainGetAssemblies) {
        console.log('[-] 缺 il2cpp_domain_* 导出，无法枚举 assembly。');
        return;
    }
    const domain = f.domainGet();
    if (domain.isNull()) {
        console.log('[-] il2cpp_domain_get 返回 null，il2cpp 还没初始化完，稍后再试。');
        return;
    }
    const countPtr = Memory.alloc(8);
    countPtr.writeU64(0);
    const asms = f.domainGetAssemblies(domain, countPtr);
    const n = countPtr.readU64().toNumber();
    console.log('[*] assemblies = ' + n);

    const exactLeft = new Set(EXACT_CLASSES);
    const found = {};      // full -> klass
    let kwHits = 0;

    for (let i = 0; i < n; i++) {
        const asm = asms.add(i * P).readPointer();
        if (asm.isNull()) continue;
        const image = f.assemblyGetImage(asm);
        if (image.isNull()) continue;

        let getClass = null, classCount = 0;
        try {
            const cd = mod.findExportByName('il2cpp_image_get_class_count');
            const cg = mod.findExportByName('il2cpp_image_get_class');
            if (cd && cg) {
                getClass = new NativeFunction(cg, SZ, [SZ, 'uint32']);
                classCount = new NativeFunction(cd, 'uint32', [SZ])(image);
            }
        } catch (e) { /* ignore */ }

        if (!getClass) {
            console.log('[!] 缺 il2cpp_image_get_class(_count)，无法按 image 枚举类。');
            return;
        }

        for (let c = 0; c < classCount; c++) {
            const klass = getClass(image, c);
            if (klass.isNull()) continue;
            const name = cstr(f.classGetName(klass));
            if (!name) continue;
            const ns = cstr(f.classGetNamespace(klass));
            const full = ns ? ns + '.' + name : name;

            // 1) 精确名（不限命名空间）
            if (exactLeft.has(name) && !found[full]) {
                found[full] = klass;
                exactLeft.delete(name);
            }
            // 2) 关键词
            else if (KEYWORD.test(full) && (!NS_FILTER || NS_FILTER.test(ns))) {
                if (kwHits < MAX_CLASS_HITS && !found[full]) {
                    found[full] = klass;
                    kwHits++;
                }
            }
        }
    }

    console.log('');
    console.log('======== 精确命中 (EXACT_CLASSES) ========');
    if (!Object.keys(found).length) {
        console.log('（无）类名可能带前缀，看下面关键词部分。');
    }
    const table = {};
    for (const full of Object.keys(found)) {
        const r = dumpClass(found[full], 'HIT');
        table[r.full] = r.fields;
    }

    if (exactLeft.size) {
        console.log('');
        console.log('[!] 未找到的精确类名: ' + Array.from(exactLeft).join(', '));
    }

    // ====== 汇总：把关键的 awakening 字段单独提出来 ======
    console.log('');
    console.log('======== 关键字段汇总 ========');
    for (const full of Object.keys(table)) {
        for (const fd of table[full]) {
            if (/awaken|nude|undress|skin|unlock/i.test(fd.name)) {
                console.log('  ' + full + '.' + fd.name +
                    '  @ 0x' + (fd.off >= 0 ? fd.off.toString(16) : '??') +
                    '  ' + fd.typeName);
            }
        }
    }
    console.log('');
    console.log('提示：偏移已含对象头 0x10，与 ec_probe_class 的 w[0] 对应 +0x10。');
    console.log('      拿到 AwakenLV 偏移后，先去游戏里把某角色从 0 觉醒到 1，再差分两次 dump 验证。');
}

let tries = 0;
const timer = setInterval(() => {
    tries++;
    try {
        main();
    } catch (e) {
        console.log('[-] ' + e.message);
    }
    clearInterval(timer);
    if (tries > 3) return;
}, 800);
