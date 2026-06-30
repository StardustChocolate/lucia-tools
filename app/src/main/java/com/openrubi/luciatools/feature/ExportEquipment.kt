package com.openrubi.luciatools.feature

import android.content.ContentValues
import android.content.Context
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import com.openrubi.luciatools.mem.NativeBridge
import org.json.JSONArray
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** 导出装备表到下载文件夹。返回导出件数(>=0)或抛异常。 */
object ExportEquipment {

    /** 结果：件数 + 文件名 */
    data class Result(val count: Int, val fileName: String)

    fun run(ctx: Context, pid: Int): Result {
        Localizer.ensureLoaded(ctx)
        val json = NativeBridge.nativeExportEquipment(pid)
        if (json.startsWith("{\"error\"")) {
            throw RuntimeException("native $json")
        }
        val arr = JSONArray(json)
        val csv = buildCsv(arr)
        val ts = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val fileName = "LuciaTools_装备列表_$ts.csv"
        writeToDownloads(ctx, fileName, csv)
        return Result(arr.length(), fileName)
    }

    private fun buildCsv(arr: JSONArray): String {
        val sb = StringBuilder()
        sb.append('﻿') // UTF-8 BOM, 让 Excel 正确识别中文
        sb.append("名称,稀有度,部位,等级,强化,套装,主属性,副属性,StaticID\r\n")
        for (i in 0 until arr.length()) {
            val o = arr.getJSONObject(i)
            val sid = o.getString("staticId")
            val name = Localizer.itemName(sid)
            val rarity = Localizer.rarity(o.getInt("classLv"))
            val part = Localizer.part(o.getString("part"))
            val equipLv = o.getInt("equipLv")
            val strengthen = "+" + o.getInt("strengthen")
            val set = Localizer.setName(o.getString("set"))
            val main = Localizer.prop(o.getString("mainProp"))
            val subsArr = o.getJSONArray("subProps")
            val subs = (0 until subsArr.length())
                .map { Localizer.prop(subsArr.getString(it)) }
                .filter { it.isNotEmpty() }
                .joinToString(" | ")
            sb.append(csvCell(name)).append(',')
                .append(csvCell(rarity)).append(',')
                .append(csvCell(part)).append(',')
                .append(equipLv).append(',')
                .append(csvCell(strengthen)).append(',')
                .append(csvCell(set)).append(',')
                .append(csvCell(main)).append(',')
                .append(csvCell(subs)).append(',')
                .append(csvCell(sid)).append("\r\n")
        }
        return sb.toString()
    }

    private fun csvCell(s: String): String =
        if (s.contains(',') || s.contains('"') || s.contains('\n'))
            "\"" + s.replace("\"", "\"\"") + "\"" else s

    private fun writeToDownloads(ctx: Context, fileName: String, content: String) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val cv = ContentValues().apply {
                put(MediaStore.Downloads.DISPLAY_NAME, fileName)
                put(MediaStore.Downloads.MIME_TYPE, "text/csv")
                put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS)
            }
            val uri = ctx.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv)
                ?: throw RuntimeException("无法创建下载文件")
            ctx.contentResolver.openOutputStream(uri)?.use {
                it.write(content.toByteArray(Charsets.UTF_8))
            } ?: throw RuntimeException("无法写入下载文件")
        } else {
            val dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
            if (!dir.exists()) dir.mkdirs()
            File(dir, fileName).writeText(content, Charsets.UTF_8)
        }
    }
}
