package com.cheng.mobile

import android.app.Activity
import android.os.Bundle
import android.util.Base64
import java.io.File
import java.io.FileOutputStream
import java.nio.charset.StandardCharsets

class ChengActivity : Activity() {
    private var view: ChengSurfaceView? = null
    private var hostServices: AndroidHostServices? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        applyLaunchArgs()
        ensureAssetsExtracted()?.let { root ->
            ChengNative.setAssetRoot(root.absolutePath)
        }
        hostServices = AndroidHostServices(applicationContext)
        hostServices?.attachActivity(this)
        view = ChengSurfaceView(this)
        view?.let { ChengNative.registerView(it) }
        setContentView(view)
        persistRuntimeStateSnapshot()
    }

    override fun onResume() {
        super.onResume()
        ChengNative.startOnce()
        ChengNative.onResumeNative()
        hostServices?.attachActivity(this)
        hostServices?.start()
        view?.startFrameTick()
        persistRuntimeStateSnapshot()
        scheduleRuntimeStateRefresh(10, 300L)
    }

    override fun onPause() {
        view?.stopFrameTick()
        hostServices?.stop()
        persistRuntimeStateSnapshot()
        ChengNative.onPauseNative()
        super.onPause()
    }

    override fun onDestroy() {
        hostServices?.stop()
        hostServices?.attachActivity(null)
        hostServices = null
        super.onDestroy()
    }

    private fun applyLaunchArgs() {
        val kv = intent?.getStringExtra("cheng_app_args_kv") ?: ""
        val jsRaw = intent?.getStringExtra("cheng_app_args_json") ?: ""
        val jsB64 = intent?.getStringExtra("cheng_app_args_json_b64") ?: ""
        val js = decodeJsonArg(jsB64, jsRaw)
        ChengNative.setLaunchArgs(kv, js)
        persistRuntimeStateSnapshot()
    }

    private fun decodeJsonArg(base64Arg: String, fallback: String): String {
        if (base64Arg.isNotEmpty()) {
            try {
                val bytes = Base64.decode(base64Arg, Base64.URL_SAFE or Base64.NO_WRAP)
                return String(bytes, StandardCharsets.UTF_8)
            } catch (_: Exception) {
            }
        }
        return fallback
    }

    private fun persistRuntimeStateSnapshot() {
        try {
            val payload = ChengNative.runtimeStateJson() ?: return
            val out = File(filesDir, "cheng_runtime_state.json")
            out.parentFile?.mkdirs()
            out.writeText(payload)
        } catch (_: Exception) {
        }
    }

    private fun scheduleRuntimeStateRefresh(rounds: Int, delayMs: Long) {
        if (rounds <= 0) {
            return
        }
        view?.postDelayed({
            persistRuntimeStateSnapshot()
            scheduleRuntimeStateRefresh(rounds - 1, delayMs)
        }, delayMs)
    }

    private fun ensureAssetsExtracted(): File? {
        val manifestName = "assets_manifest.txt"
        val manifestText = try {
            assets.open(manifestName).bufferedReader().use { it.readText() }
        } catch (e: Exception) {
            return null
        }
        val root = File(filesDir, "cheng_assets")
        if (!root.exists()) {
            root.mkdirs()
        }
        File(root, manifestName).writeText(manifestText)
        val lines = manifestText.split('\n')
        for (line in lines) {
            val trimmed = line.trim()
            if (trimmed.isEmpty() || trimmed.startsWith("#")) {
                continue
            }
            val parts = trimmed.split('\t')
            if (parts.isEmpty()) {
                continue
            }
            val rel = parts[0]
            if (rel.isEmpty() || rel == manifestName) {
                continue
            }
            val size = parts.getOrNull(1)?.trim()?.toLongOrNull() ?: -1L
            val outFile = File(root, rel)
            if (outFile.exists() && size >= 0 && outFile.length() == size) {
                continue
            }
            outFile.parentFile?.mkdirs()
            try {
                assets.open(rel).use { input ->
                    FileOutputStream(outFile).use { output ->
                        input.copyTo(output)
                    }
                }
            } catch (e: Exception) {
                continue
            }
        }
        return root
    }
}
