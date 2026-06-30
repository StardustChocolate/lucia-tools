package com.openrubi.luciatools.feature

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RadialGradient
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import kotlin.math.cos
import kotlin.math.sin

/**
 * 装备分析卡片(横向：信息列表在左，雷达图在右)。柔和现代配色。
 * 横版 1600×900。
 */
object CardRenderer {

    private const val W = 1600
    private const val H = 900

    // 柔和配色(深蓝底 + 香槟紫/暖金点缀)
    private val cBg0 = Color.parseColor("#161A26")
    private val cBg1 = Color.parseColor("#222838")
    private val cAccent = Color.parseColor("#9D8CFF")   // 香槟紫
    private val cAccent2 = Color.parseColor("#F2C879")  // 暖金
    private val cText = Color.parseColor("#EDEFF5")
    private val cSub = Color.parseColor("#9098AC")
    private val cCard = Color.parseColor("#2A3145")
    private val cLine = Color.parseColor("#3A4258")

    fun render(account: AccountInfo, r: StatCalc.Result, avatar: Bitmap? = null): Bitmap {
        val bmp = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888)
        val c = Canvas(bmp)
        drawBackground(c)
        drawInfoPanel(c, account, r, left = 70f, avatar = avatar)   // 左：信息列表
        drawRadar(c, r, cx = 1180f, cy = 470f, radius = 290f)  // 右：雷达图
        // 右下角水印(两行)：落款在上，Q群在下
        c.drawText("璐茜娅的魔方 · LuciaTools", W - 40f, H - 64f,
            textPaint(26f, cSub).apply { textAlign = Paint.Align.RIGHT })
        c.drawText("Q群：1032070842", W - 40f, H - 30f,
            textPaint(26f, cSub).apply { textAlign = Paint.Align.RIGHT })
        return bmp
    }

    private fun p(color: Int, stroke: Boolean = false, sw: Float = 0f, glow: Float = 0f) =
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            this.color = color
            style = if (stroke) Paint.Style.STROKE else Paint.Style.FILL
            if (stroke) strokeWidth = sw
            if (glow > 0f) setShadowLayer(glow, 0f, 0f, color)
        }

    private fun textPaint(size: Float, color: Int, bold: Boolean = false, center: Boolean = false) =
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            this.color = color
            textSize = size
            typeface = Typeface.create(Typeface.DEFAULT, if (bold) Typeface.BOLD else Typeface.NORMAL)
            if (center) textAlign = Paint.Align.CENTER
        }

    private fun drawBackground(c: Canvas) {
        c.drawRect(0f, 0f, W.toFloat(), H.toFloat(), Paint().apply {
            shader = LinearGradient(0f, 0f, W.toFloat(), H.toFloat(), cBg1, cBg0, Shader.TileMode.CLAMP)
        })
        c.drawRect(800f, 0f, W.toFloat(), H.toFloat(), Paint().apply {
            shader = RadialGradient(1180f, 460f, 560f,
                Color.parseColor("#339D8CFF"), Color.TRANSPARENT, Shader.TileMode.CLAMP)
        })
    }

    /** 右侧八维雷达图(允许爆表) */
    private fun drawRadar(c: Canvas, r: StatCalc.Result, cx: Float, cy: Float, radius: Float) {
        c.drawText("装备分析", cx, 92f, textPaint(46f, cText, bold = true, center = true))
        val dims = StatScorer.DIMS
        val n = dims.size
        fun ang(i: Int) = Math.toRadians(-90.0 + 360.0 / n * i)
        for (ring in 1..4) {
            val rr = radius * ring / 4f
            val path = Path()
            for (i in 0 until n) {
                val x = cx + (rr * cos(ang(i))).toFloat()
                val y = cy + (rr * sin(ang(i))).toFloat()
                if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
            }
            path.close()
            c.drawPath(path, p(cLine, stroke = true, sw = 2f))
        }
        for (i in 0 until n) {
            val x = cx + (radius * cos(ang(i))).toFloat()
            val y = cy + (radius * sin(ang(i))).toFloat()
            c.drawLine(cx, cy, x, y, p(cLine, stroke = true, sw = 2f))
            val d = dims[i]
            val lx = cx + ((radius + 54f) * cos(ang(i))).toFloat()
            val ly = cy + ((radius + 54f) * sin(ang(i))).toFloat()
            c.drawText(d.label, lx, ly + 10f, textPaint(30f, cText, bold = true, center = true))
            c.drawText("%.2f".format(r.radar[d.key] ?: 0.0), lx, ly + 44f, textPaint(26f, cAccent2, center = true))
        }
        val dataPath = Path()
        for (i in 0 until n) {
            val full = (r.radarFull[dims[i].key] ?: 1.0).let { if (it > 0) it else 1.0 }
            val ratio = ((r.radar[dims[i].key] ?: 0.0) / full).coerceIn(0.03, 1.0)
            val x = cx + (radius * ratio * cos(ang(i))).toFloat()
            val y = cy + (radius * ratio * sin(ang(i))).toFloat()
            if (i == 0) dataPath.moveTo(x, y) else dataPath.lineTo(x, y)
        }
        dataPath.close()
        c.drawPath(dataPath, Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.FILL; color = Color.parseColor("#559D8CFF")
        })
        c.drawPath(dataPath, p(cAccent, stroke = true, sw = 4f, glow = 12f))
        for (i in 0 until n) {
            val full = (r.radarFull[dims[i].key] ?: 1.0).let { if (it > 0) it else 1.0 }
            val ratio = ((r.radar[dims[i].key] ?: 0.0) / full).coerceIn(0.03, 1.0)
            val x = cx + (radius * ratio * cos(ang(i))).toFloat()
            val y = cy + (radius * ratio * sin(ang(i))).toFloat()
            c.drawCircle(x, y, 7f, p(cAccent2, glow = 6f))
        }
    }

    /** 左侧信息：账号头 + 纵向逐行列表(满强化数量 + 四项极值) */
    private fun drawInfoPanel(c: Canvas, a: AccountInfo, r: StatCalc.Result, left: Float, avatar: Bitmap? = null) {
        val right = 760f
        // 账号头：有头像则画头像，否则回退到队长 SID 文字
        val box = RectF(left, 60f, left + 140f, 200f)
        c.drawRoundRect(box, 18f, 18f, p(cCard))
        if (avatar != null) {
            val save = c.save()
            val clip = Path().apply { addRoundRect(box, 18f, 18f, Path.Direction.CW) }
            c.clipPath(clip)
            c.drawBitmap(avatar, null, box, Paint(Paint.ANTI_ALIAS_FLAG).apply { isFilterBitmap = true })
            c.restoreToCount(save)
        } else {
            val leader = if (a.leaderSid.isNotEmpty()) a.leaderSid else "—"
            c.drawText("队长", box.centerX(), box.centerY() - 14f, textPaint(26f, cSub, center = true))
            c.drawText(leader, box.centerX(), box.centerY() + 30f, textPaint(42f, cAccent, bold = true, center = true))
        }
        c.drawRoundRect(box, 18f, 18f, p(cAccent, stroke = true, sw = 2f, glow = 8f))
        val tx = left + 168f
        c.drawText(a.nickname.ifEmpty { "未知账号" }, tx, 110f, textPaint(52f, cText, bold = true))
        c.drawText("UID: ${a.cuid}", tx, 158f, textPaint(32f, cSub))
        c.drawText("Lv. ${a.lv}", tx, 196f, textPaint(32f, cAccent2))

        // 纵向逐行列表
        data class Row(val bar: Int, val label: String, val value: String, val color: Int, val note: String = "")
        val rows = listOf(
            Row(cAccent2, "75级以上满强化装备", "${r.maxedCount} 件", cAccent2),
            Row(cAccent, "顶配攻击力", r.maxAttack.toString(), cText, r.topAttackNote),
            Row(cAccent, "顶配生命力", r.maxHp.toString(), cText, r.topHpNote),
            Row(cAccent, "顶配防御力", r.maxDefence.toString(), cText, r.topDefNote),
            Row(cAccent, "顶配抗性", r.maxResist.toString(), cText, r.topResistNote),
            Row(cAccent, "一速", r.maxSpeed.toString(), cText, r.topSpeedNote)
        )
        var y = 248f
        val rowH = 100f
        for ((idx, row) in rows.withIndex()) {
            c.drawRoundRect(RectF(left, y + 14f, left + 8f, y + rowH - 14f), 4f, 4f, p(row.bar))
            c.drawText(row.label, left + 34f, y + rowH / 2f + 2f, textPaint(36f, cSub))
            // 备注(小字，在标签下方)
            if (row.note.isNotEmpty())
                c.drawText(row.note, left + 34f, y + rowH / 2f + 36f, textPaint(23f, cAccent2))
            c.drawText(row.value, right, y + rowH / 2f + 14f,
                textPaint(if (idx == 0) 48f else 54f, row.color, bold = true).apply {
                    textAlign = Paint.Align.RIGHT
                })
            if (idx < rows.size - 1)
                c.drawLine(left + 34f, y + rowH, right, y + rowH, p(cLine, stroke = true, sw = 1.5f))
            y += rowH
        }
    }
}

