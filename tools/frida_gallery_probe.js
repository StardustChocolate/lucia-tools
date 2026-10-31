'use strict';
/*
 * frida_gallery_probe.js —— CG 图鉴类/字段发现脚本（只读，不改任何数据）
 *
 * 用法（设备需 root + frida-server）：
 *   frida -U -f com.nerversoft.ark.recode -l tools/frida_gallery_probe.js --no-pause
 *
 * 也可以附加到已运行的进程：
 *   frida -U -n com.nerversoft.ark.recode -l tools/frida_gallery_probe.js
 *
 * 输出内容：
 *   1) 所有名字命中关键词的类：完整名、字段（名字/偏移/类型/是否静态）、命中关键词的方法
 *   2) 汇总一行 "== 候选 N 个 =="，方便你直接抄进 gallery_core.c 的常量区
 *
 * 说明：
 *   - 本作的 global-metadata.dat 被加密，Il2CppDumper 静态 dump 拿不到字段名，
 *     但 libil2cpp.so 的 il2cpp_* 导出符号在运行时可用，所以这里走运行时枚举。
 *   - 只调用 getter，不 hook、不写内存，理论上不会被完整性校验发现。
 *   - 如果某个导出符号缺失（部分版本会 strip），脚本会打印缺失列表并降级。
 */

// ======== 可按需修改的过滤条件 ========
// 类名/命名空间命中任一关键词才输出
const KEYWORD = /(cg|gallery|album|illust|collect|unlock|recall|recollection|story|memory)/i;
// 命名空间过滤：null = 不限；/^Game$/ = 只看 Game 命名空间
const NS_FILTER = /^Game$/;
// 每个类最多打印多少个字段
const MAX_FIELDS = 40;
// =====================================

function findIl2cppModule() {
    const mods = Process.enumerateModules();
    for (const m of mods) {
        if (/il2cpp/i.test(m.name)) return m;
    }
    return null;
}

const mod = findIl2cppModule();
if (!mod) {
    console.log('[-] 没找到 libil2cpp.so，游戏可能被加壳或模块名不同。');
    console.log('    现有模块：' + Process.enumerateModules().map(m => m.name).join(', '));
    // 交给 Frida 退出，避免 Attach 后卡住
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
    console.log('    缺 il2cpp_class_get_fields 时无法拿字段偏移，请改用应用内探针 + 差分方案。');
}

const P = Process.pointerSize;
const SZ = 'pointer';

const f = {};
if (exp.il2cpp_domain_get)
    f.domainGet = new NativeFunction(exp.il2cpp_domain_get, SZ, []);
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
    if (ptr.isNull()) return '';
    try { return ptr.readCString() || ''; } catch (e) { return ''; }
}

function listFields(klass) {
    if (!f.classGetFields) return [];
    const iter = Memory.alloc(P);
    iter.writePointer(ptr(0));
    const out = [];
    for (let guard = 0; guard < 256; guard++) {
        const fi = f.classGetFields(klass, iter);
        if (fi.isNull()) break;
        const name = cstr(f.fieldGetName(fi));
        const off = f.fieldGetOffset ? f.fieldGetOffset(fi).toNumber() : -1;
        const flags = f.fieldGetFlags ? f.fieldGetFlags(fi) : 0;
        let typeName = '?';
        if (f.fieldGetType && f.typeGetName) {
            typeName = cstr(f.typeGetName(f.fieldGetType(fi)));
        }
        out.push({
            name,
            off,
            typeName,
            isStatic: (flags & FIELD_ATTRIBUTE_STATIC) !== 0,
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
    for (let guard = 0; guard < 512; guard++) {
        const mi = f.classGetMethods(klass, iter);
        if (mi.isNull()) break;
        const name = cstr(f.methodGetName(mi));
        if (/unlock|open|read|receive|add|set|enable/i.test(name)) {
            out.push(name + '/' + (f.methodGetParamCount ? f.methodGetParamCount(mi) : 0));
        }
        if (out.length > 40) break;
    }
    return out;
}

function main() {
    if (!f.domainGet || !f.domainGetAssemblies) {
        console.log('[-] 缺 il2cpp_domain_* 导出，无法枚举 assembly。');
        return;
    }
    const domain = f.domainGet();
    if (domain.isNull()) {
        console.log('[-] il2cpp_domain_get 返回 null，il2cpp 可能还没初始化，稍等再试。');
        return;
    }
    const countPtr = Memory.alloc(8);
    countPtr.writeU64(0);
    const asms = f.domainGetAssemblies(domain, countPtr);
    const n = countPtr.readU64().toNumber();
    console.log('[*] assemblies = ' + n);

    let hits = 0;
    for (let i = 0; i < n; i++) {
        const asm = asms.add(i * P).readPointer();
        if (asm.isNull()) continue;
        const image = f.assemblyGetImage(asm);
        if (image.isNull()) continue;

        // 类枚举：优先 il2cpp_image_get_class_count/il2cpp_image_get_class
        // 这两个不是必需导出，用 try 包一层
        let classCount = 0;
        let getClass = null;
        try {
            const cd = mod.findExportByName('il2cpp_image_get_class_count');
            const cg = mod.findExportByName('il2cpp_image_get_class');
            if (cd && cg) {
                getClass = new NativeFunction(cg, SZ, [SZ, 'uint32']);
                classCount = new NativeFunction(cd, 'uint32', [SZ])(image);
            }
        } catch (e) { /* ignore */ }

        if (!getClass) {
            console.log('[!] 缺 il2cpp_image_get_class_count / il2cpp_image_get_class，跳过 image 枚举');
            return;
        }

        for (let c = 0; c < classCount; c++) {
            const klass = getClass(image, c);
            if (klass.isNull()) continue;
            const name = cstr(f.classGetName(klass));
            const ns = cstr(f.classGetNamespace(klass));
            if (!name) continue;
            const full = ns ? ns + '.' + name : name;
            if (NS_FILTER && !NS_FILTER.test(ns)) continue;
            if (!KEYWORD.test(full)) continue;

            hits++;
            const fields = listFields(klass);
            const methods = listMethods(klass);
            console.log('');
            console.log('[HIT] ' + full + '   klass=' + klass);
            for (const fd of fields) {
                const offStr = fd.off >= 0 ? '0x' + fd.off.toString(16) : '??';
                console.log('      ' + (fd.isStatic ? 'static ' : '       ') +
                    (offStr + '      ').slice(0, 9) +
                    (fd.name + '                    ').slice(0, 22) +
                    fd.typeName);
            }
            if (methods.length) {
                console.log('      methods: ' + methods.join(', '));
            }
        }
    }
    console.log('');
    console.log('== 候选 ' + hits + ' 个 ==');
    console.log('提示：字段偏移已含对象头 0x10，与 gc_probe_class 的 w[0] 对应 +0x10。');
}

// il2cpp 可能还没初始化完，轮询几次
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
