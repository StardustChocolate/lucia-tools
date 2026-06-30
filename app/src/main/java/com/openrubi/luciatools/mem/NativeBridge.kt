package com.openrubi.luciatools.mem

/** JNI 桥：调用 liblucia.so 的内存读取/装备导出。 */
object NativeBridge {
    init {
        System.loadLibrary("lucia")
    }

    /** 目标游戏包名（与游戏同 sharedUserId 同 uid 运行，故可读其内存） */
    const val GAME_PACKAGE = "com.nerversoft.ark.recode"

    /** 按包名查游戏 pid，0 表示未找到 */
    external fun nativeFindPid(pkg: String): Int

    /** 是否能读到游戏内存（读 il2cpp ELF 头验证） */
    external fun nativeCanConnect(pid: Int): Boolean

    /** 导出装备为 JSON 字符串；失败返回 {"error":n} */
    external fun nativeExportEquipment(pid: Int): String

    /** 导出登录账号信息 JSON：{nickname,cuid,lv,leaderSid,found} */
    external fun nativeExportAccount(pid: Int): String

    /** 秒杀开关：enable=true 觉醒攻击增量改大, false 还原。返回修改条目数, <0 出错。 */
    external fun nativeSetInstakill(pid: Int, enable: Boolean): Int

    /** 预热：后台扫描填充秒杀地址缓存(不修改)，使后续开关瞬间完成。返回条目数, <0 出错。 */
    external fun nativePrimeInstakill(pid: Int): Int

    /** 预热：后台填充导出用 klass 缓存(装备表/装备图共用)。返回 1 成功 0 失败。 */
    external fun nativePrimeExport(pid: Int): Int

    /** 综合：查 pid 并验证可连接 */
    fun scan(): Int {
        val pid = nativeFindPid(GAME_PACKAGE)
        if (pid <= 0) return 0
        return if (nativeCanConnect(pid)) pid else 0
    }
}
