package com.openrubi.luciatools.float

import android.animation.AnimatorSet
import android.animation.ObjectAnimator
import android.content.Context
import android.graphics.Point
import android.graphics.PixelFormat
import android.os.Build
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.view.animation.OvershootInterpolator
import android.widget.FrameLayout
import android.widget.ImageView
import com.openrubi.luciatools.R
import kotlin.math.cos
import kotlin.math.sin

/**
 * 径向子菜单：以浮窗中心为圆心，弧形弹出 3 个子按钮。
 * 智能方向：依据浮窗在屏幕的位置选择朝屏幕内部的扇区，避免被边缘裁切。
 * 动画：scale + alpha + 位移，错峰 stagger，OvershootInterpolator。
 */
class RadialMenu(
    private val ctx: Context,
    private val wm: WindowManager,
    controller: FloatController,
    private val bubbleCenter: Point,
    private val screen: Point,
    private val onMenu: () -> Unit,
    private val onConnect: () -> Unit,
    private val onStop: () -> Unit,
    private val isConnected: () -> Boolean,
    private val onCollapseRequest: () -> Unit
) {
    private val c = controller
    private val btnSize = c.dp(46)
    private val radius = c.dp(72)   // 主图标与子按钮距离更近(原96)

    private lateinit var root: FrameLayout
    private lateinit var params: WindowManager.LayoutParams
    private val buttons = mutableListOf<ImageView>()
    private var connectBtn: ImageView? = null

    fun show() {
        root = FrameLayout(ctx)
        params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.MATCH_PARENT,
            overlayType(),
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            PixelFormat.TRANSLUCENT
        ).apply { gravity = Gravity.TOP or Gravity.START }

        // 点击空白区域(按钮以外)收起子菜单
        root.setOnClickListener { onCollapseRequest() }

        // 三个子按钮固定顺序：菜单、连接、停止
        val specs = listOf(
            Triple(R.drawable.ic_menu, { onMenu() }, false),
            Triple(
                if (isConnected()) R.drawable.ic_plug_connected else R.drawable.ic_plug_disconnected,
                { onConnect() }, true
            ),
            Triple(R.drawable.ic_close, { onStop() }, false)
        )

        // 角度按浮窗位置选朝内扇区；并保证 specs[0..2] 在屏幕上从上到下排列
        val angles = pickAngles()

        specs.forEachIndexed { i, (icon, action, isConn) ->
            val btn = makeButton(icon, action)
            if (isConn) connectBtn = btn
            buttons.add(btn)
            root.addView(btn)
            placeAndAnimate(btn, angles[i], i)
        }

        wm.addView(root, params)
    }

    private fun makeButton(iconRes: Int, action: () -> Unit): ImageView =
        ImageView(ctx).apply {
            setImageResource(iconRes)
            background = ctx.getDrawable(R.drawable.bg_sub_button)
            val pad = c.dp(12)
            setPadding(pad, pad, pad, pad)
            setOnClickListener { action() }
        }

    /** 根据浮窗位置选朝屏幕内部的 3 个角度；返回值已排序使 index 0→2 在屏幕上自上而下。 */
    private fun pickAngles(): List<Double> {
        val cx = bubbleCenter.x.toDouble() / screen.x  // 0..1
        val cy = bubbleCenter.y.toDouble() / screen.y
        // 角度系：0°=右, 90°=下, 180°=左, 270°=上 (屏幕坐标 y 向下)
        val baseDeg = when {
            cx < 0.33 && cy < 0.33 -> 45.0    // 左上 → 朝右下
            cx > 0.66 && cy < 0.33 -> 135.0   // 右上 → 朝左下
            cx < 0.33 && cy > 0.66 -> -45.0   // 左下 → 朝右上
            cx > 0.66 && cy > 0.66 -> -135.0  // 右下 → 朝左上
            cx < 0.33 -> 0.0                  // 左边 → 朝右
            cx > 0.66 -> 180.0                // 右边 → 朝左
            cy < 0.33 -> 90.0                 // 上边 → 朝下
            else -> -90.0                     // 其余/下边 → 朝上
        }
        val spread = 52.0
        // 三个候选角度，按屏幕 Y(=sin) 升序排序 → 菜单/连接/停止 始终自上而下，
        // 与浮窗在左/右无关(修复右侧顺序颠倒)
        return listOf(baseDeg - spread, baseDeg, baseDeg + spread)
            .sortedBy { sin(Math.toRadians(it)) }
    }

    private fun placeAndAnimate(btn: ImageView, angleDeg: Double, index: Int) {
        val rad = Math.toRadians(angleDeg)
        val targetX = (bubbleCenter.x + radius * cos(rad)).toInt() - btnSize / 2
        val targetY = (bubbleCenter.y + radius * sin(rad)).toInt() - btnSize / 2
        // 限制按钮不出屏
        val tx = targetX.coerceIn(0, screen.x - btnSize)
        val ty = targetY.coerceIn(0, screen.y - btnSize)

        val lp = FrameLayout.LayoutParams(btnSize, btnSize)
        // 起点在浮窗中心
        lp.leftMargin = bubbleCenter.x - btnSize / 2
        lp.topMargin = bubbleCenter.y - btnSize / 2
        btn.layoutParams = lp
        btn.scaleX = 0.2f; btn.scaleY = 0.2f; btn.alpha = 0f

        btn.post {
            val animX = ObjectAnimator.ofFloat(btn, "translationX", 0f, (tx - lp.leftMargin).toFloat())
            val animY = ObjectAnimator.ofFloat(btn, "translationY", 0f, (ty - lp.topMargin).toFloat())
            val sx = ObjectAnimator.ofFloat(btn, "scaleX", 0.2f, 1f)
            val sy = ObjectAnimator.ofFloat(btn, "scaleY", 0.2f, 1f)
            val al = ObjectAnimator.ofFloat(btn, "alpha", 0f, 1f)
            AnimatorSet().apply {
                playTogether(animX, animY, sx, sy, al)
                duration = 260
                startDelay = (index * 45).toLong()
                interpolator = OvershootInterpolator(1.6f)
                start()
            }
        }
    }

    fun refreshConnect(connected: Boolean) {
        connectBtn?.setImageResource(
            if (connected) R.drawable.ic_plug_connected else R.drawable.ic_plug_disconnected
        )
    }

    fun dismiss() {
        if (!::root.isInitialized) return
        // 收起动画后移除
        val set = AnimatorSet()
        val anims = buttons.flatMap {
            listOf(
                ObjectAnimator.ofFloat(it, "scaleX", it.scaleX, 0.2f),
                ObjectAnimator.ofFloat(it, "scaleY", it.scaleY, 0.2f),
                ObjectAnimator.ofFloat(it, "alpha", it.alpha, 0f)
            )
        }
        set.playTogether(anims)
        set.duration = 180
        set.start()
        root.postDelayed({ runCatching { wm.removeView(root) } }, 190)
    }

    private fun overlayType(): Int =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
        else
            @Suppress("DEPRECATION") WindowManager.LayoutParams.TYPE_PHONE
}
