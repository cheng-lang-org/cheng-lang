package com.cheng.mobile

import android.app.Activity
import android.os.Bundle
import java.io.File
import java.io.FileOutputStream

class ChengActivity : Activity() {
    private var view: ChengSurfaceView? = null
    private var hostServices: AndroidHostServices? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        ensureAssetsExtracted()?.let { root ->
            ChengNative.setAssetRoot(root.absolutePath)
        }
        hostServices = AndroidHostServices(applicationContext)
        view = ChengSurfaceView(this)
        view?.let { ChengNative.registerView(it) }
        setContentView(view)
    }

    override fun onResume() {
        super.onResume()
        hostServices?.start()
        view?.startFrameTick()
    }

    override fun onPause() {
        view?.stopFrameTick()
        hostServices?.stop()
        super.onPause()
    }

    override fun onDestroy() {
        hostServices?.stop()
        hostServices = null
        super.onDestroy()
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
