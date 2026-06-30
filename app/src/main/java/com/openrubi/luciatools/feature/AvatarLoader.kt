package com.openrubi.luciatools.feature

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory

/**
 * 角色头像加载：从 assets/avatars/Hxxx.png 按角色 SID 读取。
 * 头像来源：ArkReCode Wiki (arkrecodewiki.miraheze.org)，文件名前缀即游戏内角色 ID。
 */
object AvatarLoader {

    private val cache = HashMap<String, Bitmap?>()

    /** 按角色 SID(如 "H076")取头像；不存在返回 null。结果缓存。 */
    fun load(ctx: Context, sid: String): Bitmap? {
        if (sid.isEmpty()) return null
        cache[sid]?.let { return it }
        if (cache.containsKey(sid)) return null   // 已知缺失
        val bmp = runCatching {
            ctx.assets.open("avatars/$sid.png").use { BitmapFactory.decodeStream(it) }
        }.getOrNull()
        cache[sid] = bmp
        return bmp
    }
}
