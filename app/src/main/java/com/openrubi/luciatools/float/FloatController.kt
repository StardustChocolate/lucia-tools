package com.openrubi.luciatools.float

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.PixelFormat
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.ImageView
import android.widget.Toast
import com.openrubi.luciatools.FloatService
import com.openrubi.luciatools.R
import com.openrubi.luciatools.feature.ExportEquipment
import com.openrubi.luciatools.mem.NativeBridge
import kotlin.math.abs
import kotlin.math.hypot

/**
 * 悬浮窗总控：主浮窗(拖动/边界限制/点击) + 径向子菜单 + 功能窗 + 连接状态。
 */
@SuppressLint("ClickableViewAccessibility", "InflateParams")
class FloatController(private val ctx: Context) {

    private val wm = ctx.getSystemService(Context.WINDOW_SERVICE) as WindowManager
    private val main = Handler(Looper.getMainLooper())

    private lateinit var bubble: ImageView
    private lateinit var bubbleParams: WindowManager.LayoutParams

    private var radial: RadialMenu? = null
    private var featureWindow: FeatureWindow? = null

    private var expanded = false
    private var gamePid = 0
    private var connected = false

    private val sizePx by lazy { dp(72) }  // 主图标更大(原56)

    // ---- 屏幕尺寸 ----
    private val screen = android.graphics.Point().also {
        wm.defaultDisplay.getSize(it)
    }

    fun show() {
        bubble = ImageView(ctx).apply {
            setImageResource(R.drawable.ic_bubble) // 浮窗图标 (Cube.png)
            scaleType = ImageView.ScaleType.FIT_CENTER
        }
        bubbleParams = WindowManager.LayoutParams(
            sizePx, sizePx,
            overlayType(),
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = dp(16)
            y = dp(120)
        }
        bubble.setOnTouchListener(DragClickListener())
        wm.addView(bubble, bubbleParams)

        // 启动先扫一次确定连接状态
        scanConnection(silent = true)
    }

    fun destroy() {
        collapse()
        featureWindow?.dismiss(); featureWindow = null
        runCatching { wm.removeView(bubble) }
    }

    private fun overlayType(): Int =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
        else
            @Suppress("DEPRECATION") WindowManager.LayoutParams.TYPE_PHONE

    fun dp(v: Int): Int = (v * ctx.resources.displayMetrics.density).toInt()

    // ---- 拖动 + 点击判定 ----
    private inner class DragClickListener : View.OnTouchListener {
        private var startX = 0; private var startY = 0
        private var touchX = 0f; private var touchY = 0f
        private var moved = false
        private val slop = dp(8)

        override fun onTouch(v: View, e: MotionEvent): Boolean {
            when (e.action) {
                MotionEvent.ACTION_DOWN -> {
                    startX = bubbleParams.x; startY = bubbleParams.y
                    touchX = e.rawX; touchY = e.rawY
                    moved = false
                    return true
                }
                MotionEvent.ACTION_MOVE -> {
                    val dx = (e.rawX - touchX).toInt()
                    val dy = (e.rawY - touchY).toInt()
                    if (abs(dx) > slop || abs(dy) > slop) moved = true
                    // 限制不出边缘(不吸附)
                    bubbleParams.x = (startX + dx).coerceIn(0, screen.x - sizePx)
                    bubbleParams.y = (startY + dy).coerceIn(0, screen.y - sizePx)
                    wm.updateViewLayout(bubble, bubbleParams)
                    if (moved && expanded) {
                        // 拖动时收起子菜单, 避免错位
                        collapse()
                    }
                    return true
                }
                MotionEvent.ACTION_UP -> {
                    if (!moved) toggle()
                    return true
                }
            }
            return false
        }
    }

    private fun toggle() {
        if (expanded) collapse() else expand()
    }

    private fun expand() {
        if (expanded) return
        expanded = true
        radial = RadialMenu(
            ctx = ctx,
            wm = wm,
            controller = this,
            bubbleCenter = bubbleCenter(),
            screen = screen,
            onMenu = { openFeatureWindow() },
            onConnect = { onConnectClicked() },
            onStop = { onStopClicked() },
            isConnected = { connected },
            onCollapseRequest = { collapse() }
        ).also { it.show() }
    }

    fun collapse() {
        if (!expanded) return
        expanded = false
        radial?.dismiss()
        radial = null
    }

    private fun bubbleCenter(): android.graphics.Point =
        android.graphics.Point(bubbleParams.x + sizePx / 2, bubbleParams.y + sizePx / 2)

    // ---- 子按钮①：菜单 → 功能窗 ----
    private fun openFeatureWindow() {
        collapse()
        featureWindow?.dismiss()
        featureWindow = FeatureWindow(ctx, wm, this,
            onExport = { runExport() },
            onExportCard = { runExportCard() },
            onInstakillToggle = { runInstakillToggle() },
            isInstakillOn = { instakillOn },
            isExportBusy = { exportBusy },
            isCardBusy = { cardBusy },
            isInstakillBusy = { instakillBusy },
            onCgUnlockToggle = { runCgUnlockToggle() },
            isCgUnlockOn = { cgUnlockOn },
            isCgUnlockBusy = { cgUnlockBusy },
            onSpeed280Toggle = { runSpeed280Toggle() },
            isSpeed280On = { speed280On },
            isSpeed280Busy = { speed280Busy },
            onBattleSpeedToggle = { runBattleSpeedToggle() },
            isBattleSpeedOn = { battleSpeedOn },
            isBattleSpeedBusy = { battleSpeedBusy },
            onRepeat999 = { runRepeat999() },
            isRepeat999On = { repeat999On },
            isRepeat999Busy = { repeat999Busy }
        ).also { it.show() }
    }

    // ---- 各功能"运行中"状态(打开功能列表时据此显示运行中+置灰不可点) ----
    @Volatile private var exportBusy = false
    @Volatile private var cardBusy = false
    @Volatile private var instakillBusy = false
    @Volatile private var cgUnlockBusy = false
    @Volatile private var speed280Busy = false
    @Volatile private var battleSpeedBusy = false
    @Volatile private var repeat999Busy = false
    @Volatile private var repeat999On = false

    /**
     * 确保拿到当前活的游戏 pid(处理游戏重启导致 pid 变化的情况)。
     * 每次操作前调用:重扫并验证;返回有效 pid,失败返回 0。
     */
    private fun ensureLivePid(): Int {
        val pid = NativeBridge.scan()
        gamePid = pid
        connected = pid > 0
        return pid
    }

    // ---- 秒杀开关 ----
    @Volatile private var instakillOn = false
    private fun runInstakillToggle() {
        featureWindow?.dismiss(); featureWindow = null
        if (instakillBusy) { toast(R.string.busy_running); return }
        val target = !instakillOn
        instakillBusy = true
        Thread {
            val msg: String = try {
                val pid = ensureLivePid()
                if (pid <= 0) ctx.getString(R.string.not_connected_export)
                else {
                    val cnt = NativeBridge.nativeSetInstakill(pid, target)
                    if (cnt < 0) ctx.getString(R.string.instakill_fail, "code=$cnt")
                    else {
                        instakillOn = target
                        ctx.getString(if (target) R.string.instakill_on_ok else R.string.instakill_off_ok)
                    }
                }
            } catch (e: Exception) {
                ctx.getString(R.string.instakill_fail, e.message ?: "未知错误")
            } finally {
                instakillBusy = false
            }
            main.post { Toast.makeText(ctx, msg, Toast.LENGTH_LONG).show() }
        }.start()
    }


    // ---- 我方速度设为 280 ----
    @Volatile private var speed280On = false
    private fun runSpeed280Toggle() {
        featureWindow?.dismiss(); featureWindow = null
        if (speed280Busy) { toast(R.string.busy_running); return }
        val target = !speed280On
        speed280Busy = true
        Thread {
            val msg: String = try {
                val pid = ensureLivePid()
                if (pid <= 0) ctx.getString(R.string.not_connected_export)
                else {
                    val cnt = NativeBridge.nativeSetRoleSpeed(pid, 280f, target)
                    when {
                        cnt < 0 -> ctx.getString(R.string.speed280_fail,
                            if (cnt == -4) ctx.getString(R.string.need_in_battle) else "code=$cnt")
                        else -> {
                            speed280On = target
                            ctx.getString(
                                if (target) R.string.speed280_on_ok else R.string.speed280_off_ok, cnt)
                        }
                    }
                }
            } catch (e: Exception) {
                ctx.getString(R.string.speed280_fail, e.message ?: "未知错误")
            } finally {
                speed280Busy = false
            }
            main.post { Toast.makeText(ctx, msg, Toast.LENGTH_LONG).show() }
        }.start()
    }

    // ---- 9 倍速 ----
    @Volatile private var battleSpeedOn = false
    private fun runBattleSpeedToggle() {
        featureWindow?.dismiss(); featureWindow = null
        if (battleSpeedBusy) { toast(R.string.busy_running); return }
        val target = !battleSpeedOn
        battleSpeedBusy = true
        Thread {
            val msg: String = try {
                val pid = ensureLivePid()
                if (pid <= 0) ctx.getString(R.string.not_connected_export)
                else {
                    val cnt = NativeBridge.nativeSetBattleSpeed(pid, 9f, target)
                    when {
                        cnt == -5 -> ctx.getString(R.string.battle_speed_unavailable)
                        cnt < 0 -> ctx.getString(R.string.speed280_fail, "code=$cnt")
                        else -> {
                            battleSpeedOn = target
                            ctx.getString(
                                if (target) R.string.battle_speed_on_ok else R.string.battle_speed_off_ok,
                                if (target) "9" else "")
                        }
                    }
                }
            } catch (e: Exception) {
                ctx.getString(R.string.speed280_fail, e.message ?: "未知错误")
            } finally {
                battleSpeedBusy = false
            }
            main.post { Toast.makeText(ctx, msg, Toast.LENGTH_LONG).show() }
        }.start()
    }

    // ---- 最大重复战斗次数 999（放开 VIP 配置表里的次数上限） ----
    private fun runRepeat999() {
        featureWindow?.dismiss(); featureWindow = null
        if (repeat999Busy) { toast(R.string.busy_running); return }
        repeat999Busy = true
        val target = !repeat999On
        Thread {
            val msg: String = try {
                val pid = ensureLivePid()
                if (pid <= 0) ctx.getString(R.string.not_connected_export)
                else {
                    val cnt = NativeBridge.nativeSetRepeatCount(pid, 999, target)
                    if (cnt < 0) ctx.getString(R.string.repeat999_fail, cnt)
                    else if (target) {
                        repeat999On = true
                        ctx.getString(R.string.repeat999_ok, cnt)
                    } else {
                        repeat999On = false
                        ctx.getString(R.string.repeat999_off_ok, cnt)
                    }
                }
            } catch (e: Exception) {
                ctx.getString(R.string.repeat999_fail, -1)
            } finally {
                repeat999Busy = false
            }
            main.post { Toast.makeText(ctx, msg, Toast.LENGTH_LONG).show() }
        }.start()
    }

    // ---- 未获得角色 CG 解锁 ----
    @Volatile private var cgUnlockOn = false
    private val cgSnapPath by lazy {
        java.io.File(ctx.getExternalFilesDir(null) ?: ctx.filesDir, "cg_unlock.snap").absolutePath
    }

    private fun runCgUnlockToggle() {
        featureWindow?.dismiss(); featureWindow = null
        if (cgUnlockBusy) { toast(R.string.busy_running); return }
        val target = !cgUnlockOn
        cgUnlockBusy = true
        Thread {
            val msg: String = try {
                val pid = ensureLivePid()
                if (pid <= 0) ctx.getString(R.string.not_connected_export)
                else {
                    if (target) {
                        val json = NativeBridge.nativeCgUnlock(pid, "@UNAcquired", "H150_04", 0, cgSnapPath)
                        if (json.contains("\"err\"")) {
                            ctx.getString(R.string.cg_unlock_fail, json)
                        } else {
                            cgUnlockOn = true
                            ctx.getString(R.string.cg_unlock_on_ok, jsonInt(json, "targets"))
                        }
                    } else {
                        val json = NativeBridge.nativeCgRestore(pid, cgSnapPath)
                        if (json.contains("\"err\"")) {
                            ctx.getString(R.string.cg_unlock_fail, json)
                        } else {
                            cgUnlockOn = false
                            ctx.getString(R.string.cg_unlock_off_ok)
                        }
                    }
                }
            } catch (e: Exception) {
                ctx.getString(R.string.cg_unlock_fail, e.message ?: "未知错误")
            } finally {
                cgUnlockBusy = false
            }
            main.post { Toast.makeText(ctx, msg, Toast.LENGTH_LONG).show() }
        }.start()
    }

    private fun jsonInt(json: String, key: String): Int =
        Regex("\"" + Regex.escape(key) + "\"\\s*:\\s*(-?\\d+)")
            .find(json)?.groupValues?.get(1)?.toIntOrNull() ?: 0

    // ---- 子按钮②：连接 ----
    private fun onConnectClicked() {
        collapse()
        scanConnection(silent = false)
    }

    // ---- 子按钮③：停止 ----
    private fun onStopClicked() {
        collapse()
        ctx.stopService(android.content.Intent(ctx, FloatService::class.java))
    }

    // ---- 连接扫描(后台线程) ----
    private fun scanConnection(silent: Boolean) {
        Thread {
            val pid = NativeBridge.scan()
            main.post {
                gamePid = pid
                connected = pid > 0
                radial?.refreshConnect(connected)
                if (!silent) {
                    toast(if (connected) R.string.connect_connected else R.string.connect_failed)
                }
            }
            // 连接成功后后台预热缓存,使点击功能时瞬间完成
            if (pid > 0) {
                runCatching { NativeBridge.nativePrimeExport(pid) }
                runCatching { NativeBridge.nativePrimeInstakill(pid) }
            }
        }.start()
    }

    // ---- 导出 ----
    private fun runExport() {
        featureWindow?.dismiss(); featureWindow = null
        if (exportBusy) { toast(R.string.busy_running); return }
        toast(R.string.export_running)
        exportBusy = true
        Thread {
            val msg: String = try {
                val pid = ensureLivePid()
                if (pid <= 0) ctx.getString(R.string.not_connected_export)
                else {
                    val r = ExportEquipment.run(ctx, pid)
                    if (r.count <= 0) ctx.getString(R.string.export_empty)
                    else ctx.getString(R.string.export_done, r.count)
                }
            } catch (e: Exception) {
                ctx.getString(R.string.export_fail, e.message ?: "未知错误")
            } finally {
                exportBusy = false
            }
            main.post { Toast.makeText(ctx, msg, Toast.LENGTH_LONG).show() }
        }.start()
    }

    // ---- 导出装备图 ----
    private fun runExportCard() {
        featureWindow?.dismiss(); featureWindow = null
        if (cardBusy) { toast(R.string.busy_running); return }
        toast(R.string.card_running)
        cardBusy = true
        Thread {
            val msg: String = try {
                val pid = ensureLivePid()
                if (pid <= 0) ctx.getString(R.string.not_connected_export)
                else {
                    com.openrubi.luciatools.feature.ExportCard.run(ctx, pid)
                    ctx.getString(R.string.card_done)
                }
            } catch (e: Exception) {
                ctx.getString(R.string.export_fail, e.message ?: "未知错误")
            } finally {
                cardBusy = false
            }
            main.post { Toast.makeText(ctx, msg, Toast.LENGTH_LONG).show() }
        }.start()
    }

    private fun toast(resId: Int) {
        Toast.makeText(ctx, resId, Toast.LENGTH_SHORT).show()
    }

    // 供子组件查询连接态
    fun isConnected() = connected

    /** 调试：直接触发一次扫描+导出，带日志。 */
    fun debugExport() {
        Thread {
            val pid = NativeBridge.scan()
            android.util.Log.i("LuciaDbg", "scan pid=$pid")
            if (pid <= 0) { android.util.Log.e("LuciaDbg", "no game pid"); return@Thread }
            gamePid = pid; connected = true
            try {
                val r = ExportEquipment.run(ctx, pid)
                android.util.Log.i("LuciaDbg", "export OK count=${r.count} file=${r.fileName}")
            } catch (e: Exception) {
                android.util.Log.e("LuciaDbg", "export FAIL", e)
            }
        }.start()
    }

    /** 调试：触发装备图导出。 */
    fun debugExportCard() {
        Thread {
            val pid = NativeBridge.scan()
            android.util.Log.i("LuciaDbg", "card scan pid=$pid")
            if (pid <= 0) { android.util.Log.e("LuciaDbg", "no game pid"); return@Thread }
            gamePid = pid; connected = true
            try {
                val acc = NativeBridge.nativeExportAccount(pid)
                android.util.Log.i("LuciaDbg", "account=$acc")
                val r = com.openrubi.luciatools.feature.ExportCard.run(ctx, pid)
                android.util.Log.i("LuciaDbg", "card OK png=${r.pngName} maxed=${r.maxedCount}")
            } catch (e: Exception) {
                android.util.Log.e("LuciaDbg", "card FAIL", e)
            }
        }.start()
    }
}


