package com.openrubi.luciatools.mem

/** JNI 桥：调用 liblucia.so 的内存读取 / 装备导出 / CG 解锁。 */
object NativeBridge {
    init {
        System.loadLibrary("lucia")
    }

    /** 目标游戏包名（与游戏同 sharedUserId 同 uid 运行，故可读其内存） */
    const val GAME_PACKAGE = "com.nerversoft.ark.recode"

    external fun nativeFindPid(pkg: String): Int
    external fun nativeCanConnect(pid: Int): Boolean

    external fun nativeExportEquipment(pid: Int): String
    external fun nativeExportAccount(pid: Int): String

    external fun nativeSetInstakill(pid: Int, enable: Boolean): Int
    external fun nativePrimeInstakill(pid: Int): Int
    external fun nativePrimeExport(pid: Int): Int

    /**
     * 解锁未获得角色的图鉴 CG。
     * roles = 角色 ID 列表（如 "H133,H160"）；传 "@UNAcquired" 表示全部未获得角色。
     * donorId = 主线条件模板条目，默认 "H150_04"。
     * dry != 0 只统计不写。snapPath 为快照路径。
     * 返回 JSON 字符串。
     */
    external fun nativeCgUnlock(pid: Int, roles: String, donorId: String,
                                dry: Int, snapPath: String): String


    /** 按 nativeCgUnlock 的快照恢复。 */
    external fun nativeCgRestore(pid: Int, snapPath: String): String



    /** 综合：查 pid 并验证可连接 */
    fun scan(): Int {
        val pid = nativeFindPid(GAME_PACKAGE)
        if (pid <= 0) return 0
        return if (nativeCanConnect(pid)) pid else 0
    }
}
