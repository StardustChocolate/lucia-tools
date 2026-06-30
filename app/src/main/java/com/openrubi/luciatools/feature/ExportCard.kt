package com.openrubi.luciatools.feature

import android.content.ContentValues
import android.content.Context
import android.graphics.Bitmap
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import com.openrubi.luciatools.mem.NativeBridge
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.OutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** 导出装备统计卡片(PNG) + 明细(JSON) 到下载文件夹。 */
object ExportCard {

    data class Result(val pngName: String, val jsonName: String, val maxedCount: Int)

    fun run(ctx: Context, pid: Int): Result {
        val accJson = NativeBridge.nativeExportAccount(pid)
        val account = AccountInfo.parse(accJson)
        if (!account.found) throw RuntimeException("未读取到账号数据，请回到游戏主界面或打开一次背包后重试")

        val equipJson = NativeBridge.nativeExportEquipment(pid)
        if (equipJson.startsWith("{\"error\"")) throw RuntimeException("native $equipJson")
        val equips = StatScorer.parse(equipJson)
        val result = StatCalc.compute(equips)

        val bmp = CardRenderer.render(account, result, AvatarLoader.load(ctx, account.leaderSid))

        val ts = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val pngName = "LuciaTools_装备图_$ts.png"
        val jsonName = "LuciaTools_装备图明细_$ts.json"

        writePng(ctx, pngName, bmp)
        writeText(ctx, jsonName, buildJson(account, result), "application/json")

        return Result(pngName, jsonName, result.maxedCount)
    }

    private fun buildJson(a: AccountInfo, r: StatCalc.Result): String {
        val o = JSONObject()
        o.put("account", JSONObject().apply {
            put("nickname", a.nickname); put("cuid", a.cuid)
            put("lv", a.lv); put("leaderSid", a.leaderSid); put("found", a.found)
        })
        o.put("radar", JSONObject(r.radar.mapValues { String.format("%.2f", it.value) }))
        o.put("radarFull", JSONObject(r.radarFull.mapValues { String.format("%.2f", it.value) }))
        o.put("firstSetSample", JSONObject(r.firstSetSample.mapValues { "%.0f".format(it.value) }))
        o.put("maxAttack", r.maxAttack)
        o.put("topAttackNote", r.topAttackNote)
        o.put("maxHp", r.maxHp)
        o.put("topHpNote", r.topHpNote)
        o.put("maxDefence", r.maxDefence)
        o.put("topDefNote", r.topDefNote)
        o.put("maxSpeed", r.maxSpeed)
        o.put("topSpeedNote", r.topSpeedNote)
        o.put("maxResist", r.maxResist)
        o.put("topResistNote", r.topResistNote)
        o.put("maxedCount", r.maxedCount)
        o.put("plusCount", r.plusCount)
        o.put("setSums", JSONObject(r.setSums.mapValues { it.value.joinToString(",") { v -> "%.0f".format(v) } }))
        // 每套各部位分量明细：{ dim: [ {Weapon:.., Body:..}, ... ] }
        val partsObj = JSONObject()
        for ((dim, sets) in r.setParts) {
            val arr = JSONArray()
            for (comp in sets) arr.put(JSONObject(comp.mapValues { "%.0f".format(it.value) }))
            partsObj.put(dim, arr)
        }
        o.put("setParts", partsObj)
        return o.toString(2)
    }

    private fun writePng(ctx: Context, name: String, bmp: Bitmap) {
        openDownload(ctx, name, "image/png").use { os ->
            bmp.compress(Bitmap.CompressFormat.PNG, 100, os)
        }
    }

    private fun writeText(ctx: Context, name: String, content: String, mime: String) {
        openDownload(ctx, name, mime).use { it.write(content.toByteArray(Charsets.UTF_8)) }
    }

    private fun openDownload(ctx: Context, name: String, mime: String): OutputStream {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val cv = ContentValues().apply {
                put(MediaStore.Downloads.DISPLAY_NAME, name)
                put(MediaStore.Downloads.MIME_TYPE, mime)
                put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS)
            }
            val uri = ctx.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv)
                ?: throw RuntimeException("无法创建下载文件")
            return ctx.contentResolver.openOutputStream(uri) ?: throw RuntimeException("无法写入")
        } else {
            val dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
            if (!dir.exists()) dir.mkdirs()
            return File(dir, name).outputStream()
        }
    }
}
