package com.openrubi.luciatools.float

import android.animation.ObjectAnimator
import android.content.Context
import android.graphics.Color
import android.graphics.PixelFormat
import android.os.Build
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.view.animation.DecelerateInterpolator
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import com.openrubi.luciatools.R

/**
 * 功能选择窗：居中玻璃卡片 + 功能列表。点击卡片外区域关闭。
 */
class FeatureWindow(
    private val ctx: Context,
    private val wm: WindowManager,
    controller: FloatController,
    private val onExport: () -> Unit,
    private val onExportCard: () -> Unit,
    private val onInstakillToggle: () -> Unit,
    private val isInstakillOn: () -> Boolean,
    private val isExportBusy: () -> Boolean,
    private val isCardBusy: () -> Boolean,
    private val isInstakillBusy: () -> Boolean,
    private val onCgUnlockToggle: () -> Unit,
    private val isCgUnlockOn: () -> Boolean,
    private val isCgUnlockBusy: () -> Boolean,
    private val onSpeed280Toggle: () -> Unit,
    private val isSpeed280On: () -> Boolean,
    private val isSpeed280Busy: () -> Boolean,
    private val onBattleSpeedToggle: () -> Unit,
    private val isBattleSpeedOn: () -> Boolean,
    private val isBattleSpeedBusy: () -> Boolean,
    private val onRepeat999: () -> Unit,
    private val isRepeat999On: () -> Boolean,
    private val isRepeat999Busy: () -> Boolean
) {
    private val c = controller
    private lateinit var root: FrameLayout
    private lateinit var card: LinearLayout
    private lateinit var params: WindowManager.LayoutParams

    fun show() {
        root = FrameLayout(ctx).apply {
            setBackgroundColor(Color.parseColor("#99000000")) // 遮罩
            // 点击遮罩(卡片外)关闭
            setOnClickListener { dismiss() }
        }

        card = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            background = ctx.getDrawable(R.drawable.bg_glass_card)
            val p = c.dp(20)
            setPadding(p, p, p, p)
            // 吃掉卡片内点击，避免冒泡到遮罩
            isClickable = true
        }

        // 标题
        card.addView(TextView(ctx).apply {
            text = ctx.getString(R.string.menu_title)
            setTextColor(Color.parseColor("#E8ECF4"))
            textSize = 18f
            setPadding(0, 0, 0, c.dp(14))
        })

        // 功能项：导出装备表
        run {
            val busy = isExportBusy()
            val base = ctx.getString(R.string.feature_export_equip)
            card.addView(featureItem(
                R.drawable.ic_export,
                if (busy) ctx.getString(R.string.running_suffix, base) else base,
                ctx.getString(R.string.feature_export_equip_sub),
                enabled = !busy
            ) { onExport() })
        }

        // 间隔
        card.addView(android.view.View(ctx).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, c.dp(10))
        })

        // 功能项：导出装备图
        run {
            val busy = isCardBusy()
            val base = ctx.getString(R.string.feature_export_card)
            card.addView(featureItem(
                R.drawable.ic_card,
                if (busy) ctx.getString(R.string.running_suffix, base) else base,
                ctx.getString(R.string.feature_export_card_sub),
                enabled = !busy
            ) { onExportCard() })
        }

        // 间隔
        card.addView(android.view.View(ctx).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, c.dp(10))
        })

        // 功能项：秒杀开关(标题随状态变；运行中不可点)
        run {
            val busy = isInstakillBusy()
            val onNow = isInstakillOn()
            // 运行中显示"正在执行的动作": 当前关→正开启, 当前开→正关闭
            val base = ctx.getString(if (onNow) R.string.feature_instakill_off else R.string.feature_instakill_on)
            card.addView(featureItem(
                R.drawable.ic_instakill,
                if (busy) ctx.getString(R.string.running_suffix, base) else base,
                ctx.getString(R.string.feature_instakill_sub),
                enabled = !busy
            ) { onInstakillToggle() })
        }

        // 间隔
        card.addView(android.view.View(ctx).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, c.dp(10))
        })

        // 功能项：我方速度 280
        run {
            val busy = isSpeed280Busy()
            val onNow = isSpeed280On()
            val base = ctx.getString(if (onNow) R.string.feature_speed280_off else R.string.feature_speed280_on)
            card.addView(featureItem(
                R.drawable.ic_autospeed,
                if (busy) ctx.getString(R.string.running_suffix, base) else base,
                ctx.getString(R.string.feature_speed280_sub),
                enabled = !busy
            ) { onSpeed280Toggle() })
        }

        card.addView(android.view.View(ctx).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, c.dp(10))
        })

        // 功能项：最大重复战斗次数 999
        run {
            val busy = isRepeat999Busy()
            val on = isRepeat999On()
            val base = ctx.getString(
                if (on) R.string.feature_repeat999_off else R.string.feature_repeat999_on
            )
            card.addView(featureItem(
                R.drawable.ic_autospeed,
                if (busy) ctx.getString(R.string.running_suffix, base) else base,
                ctx.getString(R.string.feature_repeat999_sub),
                enabled = !busy
            ) { onRepeat999() })
        }

        card.addView(android.view.View(ctx).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, c.dp(10))
        })

        // 功能项：9 倍速
        run {
            val busy = isBattleSpeedBusy()
            val onNow = isBattleSpeedOn()
            val base = ctx.getString(if (onNow) R.string.feature_battle_speed_off else R.string.feature_battle_speed_on)
            card.addView(featureItem(
                R.drawable.ic_speed9,
                if (busy) ctx.getString(R.string.running_suffix, base) else base,
                ctx.getString(R.string.feature_battle_speed_sub),
                enabled = !busy
            ) { onBattleSpeedToggle() })
        }

        card.addView(android.view.View(ctx).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, c.dp(10))
        })

        // 功能项：未获得角色 CG 解锁(标题随状态变；运行中不可点)
        run {
            val busy = isCgUnlockBusy()
            val onNow = isCgUnlockOn()
            val base = ctx.getString(if (onNow) R.string.feature_cg_unlock_off else R.string.feature_cg_unlock_on)
            card.addView(featureItem(
                R.drawable.ic_cg_unlock,
                if (busy) ctx.getString(R.string.running_suffix, base) else base,
                ctx.getString(R.string.feature_cg_unlock_sub),
                enabled = !busy
            ) { onCgUnlockToggle() })
        }

        // 间隔
        card.addView(android.view.View(ctx).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, c.dp(10))
        })

        // 条目多于一屏时用 ScrollView 承载，避免底部被裁掉
        val screenH = ctx.resources.displayMetrics.heightPixels
        val scroll = android.widget.ScrollView(ctx).apply {
            isVerticalScrollBarEnabled = false
            addView(card, LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT))
        }
        val cardLp = FrameLayout.LayoutParams(
            c.dp(300), (screenH * 0.86f).toInt()
        ).apply { gravity = Gravity.CENTER }
        root.addView(scroll, cardLp)

        params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.MATCH_PARENT,
            overlayType(),
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
            PixelFormat.TRANSLUCENT
        ).apply { gravity = Gravity.TOP or Gravity.START }

        wm.addView(root, params)

        // 内容不足一屏时把滚动区收缩到内容高度，卡片才居中而不贴顶
        card.post {
            val maxH = (screenH * 0.86f).toInt()
            if (card.height in 1 until maxH) {
                (scroll.layoutParams as? FrameLayout.LayoutParams)?.let { lp ->
                    lp.height = card.height + c.dp(8)
                    scroll.layoutParams = lp
                    runCatching { wm.updateViewLayout(root, params) }
                }
            }
        }

        // 入场动画
        card.scaleX = 0.85f; card.scaleY = 0.85f; card.alpha = 0f
        card.animate().scaleX(1f).scaleY(1f).alpha(1f)
            .setDuration(220).setInterpolator(DecelerateInterpolator()).start()
        root.alpha = 0f
        ObjectAnimator.ofFloat(root, "alpha", 0f, 1f).setDuration(180).start()
    }

    private fun featureItem(icon: Int, title: String, sub: String, enabled: Boolean = true, onClick: () -> Unit): View {
        val row = LinearLayout(ctx).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            background = ctx.getDrawable(R.drawable.bg_feature_item)
            val p = c.dp(10)
            setPadding(p, p, p, p)
            alpha = if (enabled) 1f else 0.4f       // 运行中变灰
            isEnabled = enabled
            if (enabled) setOnClickListener { onClick() }
            // 禁用时不设点击监听 → 不可点
        }
        row.addView(ImageView(ctx).apply {
            setImageResource(icon)
            layoutParams = LinearLayout.LayoutParams(c.dp(30), c.dp(30))
        })
        val texts = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            val lp = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            lp.marginStart = c.dp(12)
            layoutParams = lp
        }
        texts.addView(TextView(ctx).apply {
            text = title
            setTextColor(Color.parseColor("#E8ECF4")); textSize = 15f
        })
        if (sub.isNotEmpty()) {
            texts.addView(TextView(ctx).apply {
                text = sub
                setTextColor(Color.parseColor("#7A8499")); textSize = 12f
                setPadding(0, c.dp(2), 0, 0)
            })
        }
        row.addView(texts)
        return row
    }

    fun dismiss() {
        if (!::root.isInitialized) return
        root.animate().alpha(0f).setDuration(150).withEndAction {
            runCatching { wm.removeView(root) }
        }.start()
    }

    private fun overlayType(): Int =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
        else
            @Suppress("DEPRECATION") WindowManager.LayoutParams.TYPE_PHONE
}
