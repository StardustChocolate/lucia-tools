package com.openrubi.luciatools

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

/**
 * 启动入口：申请悬浮窗权限 → 启动前台服务承载浮窗 → 自身 finish。
 * App 本身无界面，全部交互在悬浮窗。
 */
class MainActivity : AppCompatActivity() {

    private val overlayReq = 1001

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (!hasOverlay()) {
            requestOverlay()
        } else {
            launchService()
        }
    }

    private fun hasOverlay(): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.M || Settings.canDrawOverlays(this)

    private fun requestOverlay() {
        Toast.makeText(this, R.string.need_overlay_perm, Toast.LENGTH_LONG).show()
        val intent = Intent(
            Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
            Uri.parse("package:$packageName")
        )
        startActivityForResult(intent, overlayReq)
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == overlayReq) {
            if (hasOverlay()) launchService()
            else Toast.makeText(this, R.string.need_overlay_perm, Toast.LENGTH_LONG).show()
            finish()
        }
    }

    private fun launchService() {
        val svc = Intent(this, FloatService::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(svc)
        else startService(svc)
        finish()
    }
}
