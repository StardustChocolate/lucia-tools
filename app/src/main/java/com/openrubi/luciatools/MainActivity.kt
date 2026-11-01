package com.openrubi.luciatools

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.openrubi.luciatools.mem.NativeBridge
import java.io.File

/**
 * 启动入口：申请悬浮窗权限 → 启动前台服务承载浮窗 → 自身 finish。
 * App 本身无界面，全部交互在悬浮窗。
 */
class MainActivity : AppCompatActivity() {

    private val overlayReq = 1001

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (intent?.hasExtra("cgUnlock") == true) {
            runCgUnlock(
                intent.getStringExtra("roles") ?: "@UNAcquired",
                intent.getStringExtra("donor") ?: "H150_04",
                intent.getIntExtra("dry", 0),
                intent.getIntExtra("restore", 0)
            )
            return
        }
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

    /** 调试入口：解锁/恢复未获得角色 CG。 */
    private fun runCgUnlock(roles: String, donor: String, dry: Int, restore: Int) {
        Thread {
            val sb = StringBuilder()
            runCatching {
                val pid = NativeBridge.scan()
                val snap = File(getExternalFilesDir(null) ?: filesDir, "cg_unlock.snap").absolutePath
                sb.append("pid=").append(pid).append('\n')
                if (pid > 0) {
                    sb.append(
                        if (restore != 0) NativeBridge.nativeCgRestore(pid, snap)
                        else NativeBridge.nativeCgUnlock(pid, roles, donor, dry, snap)
                    ).append('\n')
                }
            }.onFailure { sb.append("EX ").append(it.toString()).append('\n') }
            runCatching {
                File(getExternalFilesDir(null) ?: filesDir, "cg_unlock.txt").writeText(sb.toString())
            }
            android.os.Process.killProcess(android.os.Process.myPid())
        }.start()
    }

    private fun launchService() {
        val svc = Intent(this, FloatService::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(svc)
        else startService(svc)
        finish()
    }
}
