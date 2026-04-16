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
        ChengNative.registerActivity(this)
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
        ChengNative.registerActivity(this)
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
        ChengNative.registerActivity(null)
        super.onDestroy()
    }

    private fun applyLaunchArgs() {
        var kv = intent?.getStringExtra("cheng_app_args_kv") ?: ""
        if (!hasLaunchArg(kv, "route_state") && !hasLaunchArg(kv, "route")) {
            kv = appendLaunchArg(kv, "route_state=home_default")
        }
        if (!hasLaunchArg(kv, "gate_mode")) {
            kv = appendLaunchArg(kv, "gate_mode=android-semantic-visual-1to1")
        }
        if (!hasLaunchArg(kv, "truth_mode")) {
            kv = appendLaunchArg(kv, "truth_mode=strict")
        }
        val gateMode = extractLaunchArg(kv, "gate_mode") ?: ""
        val strictTruthMode = gateMode == "android-semantic-visual-1to1"
        if (strictTruthMode && !hasLaunchArg(kv, "expected_framehash")) {
            val route = extractLaunchArg(kv, "route_state")
                ?: extractLaunchArg(kv, "route")
                ?: "home_default"
            val expectedFrameHash = readAssetText("truth/$route.runtime_framehash")?.trim()
            if (!expectedFrameHash.isNullOrEmpty()) {
                kv = appendLaunchArg(kv, "expected_framehash=$expectedFrameHash")
            }
        }
        val jsRaw = intent?.getStringExtra("cheng_app_args_json") ?: ""
        val jsB64 = intent?.getStringExtra("cheng_app_args_json_b64") ?: ""
        val js = decodeJsonArg(jsB64, jsRaw)
        ChengNative.setLaunchArgs(kv, js)
        persistRuntimeStateSnapshot()
    }

    private fun hasLaunchArg(kv: String, key: String): Boolean {
        if (kv.isBlank()) {
            return false
        }
        val prefix = "$key="
        return kv.split(';').any { token ->
            val part = token.trim()
            part.startsWith(prefix) && part.length > prefix.length
        }
    }

    private fun extractLaunchArg(kv: String, key: String): String? {
        if (kv.isBlank()) {
            return null
        }
        val prefix = "$key="
        for (token in kv.split(';')) {
            val part = token.trim()
            if (part.startsWith(prefix) && part.length > prefix.length) {
                return part.substring(prefix.length).trim()
            }
        }
        return null
    }

    private fun appendLaunchArg(kv: String, segment: String): String {
        val trimmed = kv.trim()
        if (trimmed.isEmpty()) {
            return segment
        }
        return "$trimmed;$segment"
    }

    private fun readAssetText(path: String): String? {
        return try {
            assets.open(path).bufferedReader().use { it.readText() }
        } catch (_: Exception) {
            null
        }
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
            val root = File(filesDir, "cheng_assets")
            if (!root.exists()) {
                root.mkdirs()
            }
            extractAssetTree("", root)
            return root
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

    private fun extractAssetTree(path: String, root: File) {
        val entries = try {
            assets.list(path)
        } catch (_: Exception) {
            null
        } ?: emptyArray()
        if (entries.isNotEmpty()) {
            for (name in entries) {
                val child = if (path.isEmpty()) name else "$path/$name"
                extractAssetTree(child, root)
            }
            return
        }
        if (path.isEmpty()) {
            return
        }
        val outFile = File(root, path)
        outFile.parentFile?.mkdirs()
        try {
            assets.open(path).use { input ->
                FileOutputStream(outFile).use { output ->
                    input.copyTo(output)
                }
            }
        } catch (_: Exception) {
        }
    }
}
