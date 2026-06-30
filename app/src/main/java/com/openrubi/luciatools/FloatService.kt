package com.openrubi.luciatools

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder
import com.openrubi.luciatools.float.FloatController

/** 前台服务：承载悬浮窗生命周期。 */
class FloatService : Service() {

    private var controller: FloatController? = null

    override fun onCreate() {
        super.onCreate()
        startForegroundCompat()
        controller = FloatController(this).also { it.show() }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // 调试钩子：adb am start-foreground-service -a com.openrubi.luciatools.TEST_EXPORT 触发导出
        if (intent?.action == "com.openrubi.luciatools.TEST_EXPORT") {
            controller?.debugExport()
        }
        if (intent?.action == "com.openrubi.luciatools.TEST_CARD") {
            controller?.debugExportCard()
        }
        return START_STICKY
    }

    override fun onDestroy() {
        controller?.destroy()
        controller = null
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun startForegroundCompat() {
        val channelId = "lucia_float"
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val nm = getSystemService(NotificationManager::class.java)
            val ch = NotificationChannel(
                channelId, "璐茜娅的魔方", NotificationManager.IMPORTANCE_LOW
            )
            nm.createNotificationChannel(ch)
            val notif: Notification = Notification.Builder(this, channelId)
                .setContentTitle("璐茜娅的魔方")
                .setContentText("运行中")
                .setSmallIcon(R.drawable.ic_launcher)
                .build()
            startForeground(1, notif)
        }
    }

    companion object {
        fun stop(svc: Service) {
            svc.stopForeground(STOP_FOREGROUND_REMOVE)
            svc.stopSelf()
        }
    }
}
