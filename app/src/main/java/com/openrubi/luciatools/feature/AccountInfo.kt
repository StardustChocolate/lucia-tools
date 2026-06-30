package com.openrubi.luciatools.feature

import org.json.JSONObject

/** 登录账号信息(来自 NativeBridge.nativeExportAccount) */
data class AccountInfo(
    val nickname: String,
    val cuid: Int,
    val lv: Int,
    val leaderSid: String,
    val found: Boolean
) {
    companion object {
        fun parse(json: String): AccountInfo {
            val o = JSONObject(json)
            return AccountInfo(
                nickname = o.optString("nickname", ""),
                cuid = o.optInt("cuid", 0),
                lv = o.optInt("lv", 0),
                leaderSid = o.optString("leaderSid", ""),
                found = o.optInt("found", 0) == 1
            )
        }
    }
}
