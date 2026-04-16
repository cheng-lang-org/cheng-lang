package com.cheng.mobile

import android.app.Activity
import android.hardware.biometrics.BiometricPrompt
import android.os.Build
import android.os.CancellationSignal
import android.os.Handler
import android.os.Looper
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import android.view.Surface
import java.lang.ref.WeakReference
import java.nio.charset.StandardCharsets
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.MessageDigest
import java.security.Signature
import java.security.interfaces.ECPublicKey
import java.security.spec.ECGenParameterSpec
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

object ChengNative {
    @Volatile private var started = false
    @Volatile private var viewRef: WeakReference<ChengSurfaceView>? = null
    @Volatile private var activityRef: WeakReference<Activity>? = null

    init {
        System.loadLibrary("cheng_mobile_host")
    }

    @JvmStatic external fun onTouch(windowId: Int, action: Int, pointerId: Int, timeMs: Long, x: Float, y: Float, dx: Float, dy: Float, button: Int)
    @JvmStatic external fun onKey(windowId: Int, keyCode: Int, down: Boolean, repeat: Boolean)
    @JvmStatic external fun onText(windowId: Int, text: String)
    @JvmStatic external fun onIme(windowId: Int, visible: Boolean, composing: String, cursorStart: Int, cursorEnd: Int)
    @JvmStatic external fun onFrame(deltaSeconds: Float)
    @JvmStatic external fun onSurface(surface: Surface?, width: Int, height: Int)
    @JvmStatic external fun onPauseNative()
    @JvmStatic external fun onResumeNative()
    @JvmStatic external fun setLaunchArgs(argsKv: String, argsJson: String)
    @JvmStatic external fun runtimeStateJson(): String?
    @JvmStatic external fun setAssetRoot(path: String)
    @JvmStatic external fun pollBusRequestEnvelope(): String?
    @JvmStatic external fun pushBusResponseEnvelope(response: String): Int
    @JvmStatic external fun cborBuilderBegin(): Int
    @JvmStatic external fun cborBuilderPutText(handle: Int, key: String, value: String): Boolean
    @JvmStatic external fun cborBuilderFinish(handle: Int): String?
    @JvmStatic external fun cborGetText(payload: String, key: String): String?
    @JvmStatic external fun cborHasKey(payload: String, key: String): Boolean
    @JvmStatic external fun cborIsValidMap(payload: String): Boolean
    @JvmStatic external fun cborMapCount(payload: String): Int
    @JvmStatic external fun cborMapKeyAt(payload: String, index: Int): String?
    @JvmStatic external fun cborMapValueAt(payload: String, index: Int): String?
    @JvmStatic external fun setDensityScale(scale: Float)
    @JvmStatic external fun shmSizeBytes(handle: Int): Long
    @JvmStatic external fun start()

    @JvmStatic
    fun registerView(view: ChengSurfaceView) {
        viewRef = WeakReference(view)
    }

    @JvmStatic
    fun registerActivity(activity: Activity?) {
        activityRef = if (activity == null) null else WeakReference(activity)
    }

    private fun currentActivity(): Activity? = activityRef?.get()

    // Called from native (C/Cheng) to toggle IME visibility.
    @JvmStatic
    fun setImeVisible(visible: Boolean) {
        viewRef?.get()?.setImeVisible(visible)
    }

    @JvmStatic
    fun biometricFingerprintAuthorize(
        requestId: String,
        purpose: Int,
        didText: String,
        promptTitle: String,
        promptReason: String,
        deviceBindingSeedHint: String,
        deviceLabelHint: String,
    ): String {
        val activity = currentActivity() ?: return ChengBiometricHost.errorWire("activity_required")
        return ChengBiometricHost.authorize(
            activity = activity,
            requestId = requestId,
            purpose = purpose,
            didText = didText,
            promptTitle = promptTitle,
            promptReason = promptReason,
            deviceBindingSeedHint = deviceBindingSeedHint,
            deviceLabelHint = deviceLabelHint,
        )
    }

    @JvmStatic
    @Synchronized
    fun startOnce() {
        if (started) {
            return
        }
        start()
        started = true
    }

    @JvmStatic
    fun isStarted(): Boolean = started
}

private object ChengBiometricHost {
    private const val attestorAlias = "cheng.bio.did.attestor.v1"
    private const val rootKeyId = "android.keystore.biometric.v1"
    private const val platformAndroid = 3
    private const val attestationKindPlatformChain = 1
    private const val promptTimeoutSeconds = 40L

    fun authorize(
        activity: Activity,
        requestId: String,
        purpose: Int,
        didText: String,
        promptTitle: String,
        promptReason: String,
        deviceBindingSeedHint: String,
        deviceLabelHint: String,
    ): String {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.P) {
            return errorWire("biometric_not_available")
        }
        val deviceBindingSeed = if (deviceBindingSeedHint.isNotBlank()) {
            deviceBindingSeedHint
        } else {
            "android.device.binding.v1"
        }
        val deviceLabel = if (deviceLabelHint.isNotBlank()) {
            deviceLabelHint
        } else {
            defaultDeviceLabel()
        }
        val sensorId = "android.biometric.strong"
        val entry = ensureAttestorEntry() ?: return errorWire("secure_store_error")
        val pubRaw = publicKeyRaw65(entry) ?: return errorWire("secure_store_error")
        val certChainText = certificateChainText(entry)
        val feature32 = sha256Parts(bytes("v3.biometric.android.feature"), pubRaw)
        val authorizedAtMs = System.currentTimeMillis()
        val livenessText =
            "android-biometric|request_id=$requestId|purpose=$purpose|did=${sanitizeForText(didText)}|ts_ms=$authorizedAtMs"
        val envelope = AttestationEnvelope(
            requestId = requestId,
            purpose = purpose,
            sensorId = sensorId,
            platform = platformAndroid,
            attestationKind = attestationKindPlatformChain,
            rootKeyId = rootKeyId,
            certChainCid = platformCertChainCid(platformAndroid, attestationKindPlatformChain, rootKeyId, certChainText),
            livenessCid = livenessCid(livenessText),
            deviceBindingSeedHash = deviceBindingSeedHash(deviceBindingSeed),
            deviceLabel = deviceLabel,
            featureCommitment = featureCommitment(feature32),
            attestorPubKey = pubRaw,
        )
        val signable = attestationSignable(envelope)
        val signatureRaw = authenticateAndSign(
            activity = activity,
            promptTitle = promptTitle,
            promptReason = promptReason,
            privateKeyAlias = attestorAlias,
            signable = signable.toByteArray(StandardCharsets.UTF_8),
        ) ?: return errorWire(lastError.get() ?: "biometric_not_available")
        val attestationText = attestationText(envelope, signatureRaw)
        return successWire(
            feature32 = feature32,
            deviceBindingSeed = deviceBindingSeed,
            deviceLabel = deviceLabel,
            sensorId = sensorId,
            hardwareAttestation = attestationText,
        )
    }

    private data class AttestationEnvelope(
        val requestId: String,
        val purpose: Int,
        val sensorId: String,
        val platform: Int,
        val attestationKind: Int,
        val rootKeyId: String,
        val certChainCid: ByteArray,
        val livenessCid: ByteArray,
        val deviceBindingSeedHash: ByteArray,
        val deviceLabel: String,
        val featureCommitment: ByteArray,
        val attestorPubKey: ByteArray,
    )

    private val lastError = AtomicReference("biometric_not_available")

    fun errorWire(error: String): String {
        return buildString {
            append("ok=0\n")
            append("feature32_hex=\n")
            append("device_binding_seed_hex=\n")
            append("device_label_hex=\n")
            append("sensor_id_hex=\n")
            append("hardware_attestation_hex=\n")
            append("error_hex=").append(hexUtf8(error)).append('\n')
        }
    }

    private fun successWire(
        feature32: ByteArray,
        deviceBindingSeed: String,
        deviceLabel: String,
        sensorId: String,
        hardwareAttestation: String,
    ): String {
        return buildString {
            append("ok=1\n")
            append("feature32_hex=").append(hex(feature32)).append('\n')
            append("device_binding_seed_hex=").append(hexUtf8(deviceBindingSeed)).append('\n')
            append("device_label_hex=").append(hexUtf8(deviceLabel)).append('\n')
            append("sensor_id_hex=").append(hexUtf8(sensorId)).append('\n')
            append("hardware_attestation_hex=").append(hexUtf8(hardwareAttestation)).append('\n')
            append("error_hex=\n")
        }
    }

    private fun defaultDeviceLabel(): String {
        val maker = Build.MANUFACTURER ?: "android"
        val model = Build.MODEL ?: "device"
        return "$maker $model".trim()
    }

    private fun sanitizeForText(value: String): String {
        return value.replace('|', '/').replace('\n', ' ').replace('\r', ' ')
    }

    private fun ensureAttestorEntry(): KeyStore.PrivateKeyEntry? {
        val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        val existing = keyStore.getEntry(attestorAlias, null) as? KeyStore.PrivateKeyEntry
        if (existing != null) {
            return existing
        }
        val generator = KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_EC, "AndroidKeyStore")
        val builder = KeyGenParameterSpec.Builder(
            attestorAlias,
            KeyProperties.PURPOSE_SIGN or KeyProperties.PURPOSE_VERIFY,
        ).setAlgorithmParameterSpec(ECGenParameterSpec("secp256r1"))
            .setDigests(KeyProperties.DIGEST_SHA256, KeyProperties.DIGEST_SHA512)
            .setUserAuthenticationRequired(true)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            builder.setInvalidatedByBiometricEnrollment(true)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            builder.setUserAuthenticationParameters(0, KeyProperties.AUTH_BIOMETRIC_STRONG)
        }
        generator.initialize(builder.build())
        generator.generateKeyPair()
        return keyStore.getEntry(attestorAlias, null) as? KeyStore.PrivateKeyEntry
    }

    private fun certificateChainText(entry: KeyStore.PrivateKeyEntry): String {
        val chain = entry.certificateChain ?: return ""
        return chain.joinToString("|") { cert ->
            Base64.encodeToString(cert.encoded, Base64.NO_WRAP)
        }
    }

    private fun publicKeyRaw65(entry: KeyStore.PrivateKeyEntry): ByteArray? {
        val pub = entry.certificate.publicKey as? ECPublicKey ?: return null
        val x = fixed32(pub.w.affineX.toByteArray())
        val y = fixed32(pub.w.affineY.toByteArray())
        val out = ByteArray(65)
        out[0] = 0x04
        System.arraycopy(x, 0, out, 1, 32)
        System.arraycopy(y, 0, out, 33, 32)
        return out
    }

    private fun fixed32(raw: ByteArray): ByteArray {
        val out = ByteArray(32)
        val src = if (raw.size > 32) raw.copyOfRange(raw.size - 32, raw.size) else raw
        System.arraycopy(src, 0, out, 32 - src.size, src.size)
        return out
    }

    private fun authenticateAndSign(
        activity: Activity,
        promptTitle: String,
        promptReason: String,
        privateKeyAlias: String,
        signable: ByteArray,
    ): ByteArray? {
        val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        val entry = keyStore.getEntry(privateKeyAlias, null) as? KeyStore.PrivateKeyEntry
            ?: run {
                lastError.set("key_not_found")
                return null
            }
        val signature = try {
            Signature.getInstance("SHA256withECDSA").apply {
                initSign(entry.privateKey)
            }
        } catch (_: Exception) {
            lastError.set("sign_failed")
            return null
        }
        val latch = CountDownLatch(1)
        val resultRef = AtomicReference<ByteArray?>(null)
        val errorRef = AtomicReference("biometric_not_available")
        val executor = activity.mainExecutor
        val prompt = BiometricPrompt.Builder(activity)
            .setTitle(if (promptTitle.isNotBlank()) promptTitle else "Authorize biometric DID")
            .setDescription(if (promptReason.isNotBlank()) promptReason else "Use strong biometric to sign")
            .setNegativeButton("Cancel", executor) { _, _ ->
                errorRef.set("biometric_user_cancelled")
                latch.countDown()
            }
            .build()
        val callback = object : BiometricPrompt.AuthenticationCallback() {
            override fun onAuthenticationError(errorCode: Int, errString: CharSequence?) {
                val mapped = when (errorCode) {
                    BiometricPrompt.BIOMETRIC_ERROR_USER_CANCELED -> "biometric_user_cancelled"
                    BiometricPrompt.BIOMETRIC_ERROR_CANCELED -> "biometric_user_cancelled"
                    BiometricPrompt.BIOMETRIC_ERROR_LOCKOUT,
                    BiometricPrompt.BIOMETRIC_ERROR_LOCKOUT_PERMANENT -> "biometric_lockout"
                    else -> "biometric_not_available"
                }
                errorRef.set(mapped)
                latch.countDown()
            }

            override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult?) {
                try {
                    val cryptoSig = result?.cryptoObject?.signature ?: signature
                    cryptoSig.update(signable)
                    val der = cryptoSig.sign()
                    resultRef.set(derToRaw64(der))
                } catch (_: Exception) {
                    errorRef.set("sign_failed")
                } finally {
                    latch.countDown()
                }
            }

            override fun onAuthenticationFailed() {
                // Keep waiting; platform will either succeed or emit a terminal error.
            }
        }
        val cancellation = CancellationSignal()
        val launch = Runnable {
            try {
                val crypto = BiometricPrompt.CryptoObject(signature)
                prompt.authenticate(crypto, cancellation, executor, callback)
            } catch (_: Exception) {
                errorRef.set("biometric_not_available")
                latch.countDown()
            }
        }
        if (Looper.myLooper() == Looper.getMainLooper()) {
            launch.run()
        } else {
            Handler(Looper.getMainLooper()).post(launch)
        }
        val finished = latch.await(promptTimeoutSeconds, TimeUnit.SECONDS)
        if (!finished) {
            cancellation.cancel()
            lastError.set("biometric_user_cancelled")
            return null
        }
        val result = resultRef.get()
        if (result == null) {
            lastError.set(errorRef.get())
        } else {
            lastError.set("")
        }
        return result
    }

    private fun derToRaw64(der: ByteArray): ByteArray {
        if (der.size < 8 || der[0].toInt() != 0x30) {
            throw IllegalArgumentException("invalid_der_signature")
        }
        var index = 1
        index += derLengthBytes(der, index)
        if (index >= der.size || der[index].toInt() != 0x02) {
            throw IllegalArgumentException("invalid_der_signature")
        }
        index += 1
        val rLen = derLengthValue(der, index)
        index += derLengthBytes(der, index)
        val r = der.copyOfRange(index, index + rLen)
        index += rLen
        if (index >= der.size || der[index].toInt() != 0x02) {
            throw IllegalArgumentException("invalid_der_signature")
        }
        index += 1
        val sLen = derLengthValue(der, index)
        index += derLengthBytes(der, index)
        val s = der.copyOfRange(index, index + sLen)
        val out = ByteArray(64)
        val rFixed = fixed32(stripLeadingZero(r))
        val sFixed = fixed32(stripLeadingZero(s))
        System.arraycopy(rFixed, 0, out, 0, 32)
        System.arraycopy(sFixed, 0, out, 32, 32)
        return out
    }

    private fun stripLeadingZero(raw: ByteArray): ByteArray {
        if (raw.isEmpty()) {
            return raw
        }
        return if (raw.size > 1 && raw[0].toInt() == 0) raw.copyOfRange(1, raw.size) else raw
    }

    private fun derLengthBytes(der: ByteArray, index: Int): Int {
        val first = der[index].toInt() and 0xff
        return if ((first and 0x80) == 0) 1 else 1 + (first and 0x7f)
    }

    private fun derLengthValue(der: ByteArray, index: Int): Int {
        val first = der[index].toInt() and 0xff
        if ((first and 0x80) == 0) {
            return first
        }
        val count = first and 0x7f
        var out = 0
        for (i in 0 until count) {
            out = (out shl 8) or (der[index + 1 + i].toInt() and 0xff)
        }
        return out
    }

    private fun featureCommitment(feature32: ByteArray): ByteArray {
        return sha256Parts(bytes("v3.biometric.feature.commitment"), feature32)
    }

    private fun deviceBindingSeedHash(seed: String): ByteArray {
        return sha256Parts(bytes("v3.biometric.device.binding.seed"), bytes(seed))
    }

    private fun platformCertChainCid(platform: Int, attestationKind: Int, rootKeyId: String, certChainText: String): ByteArray {
        return sha256Parts(
            bytes("v3.biometric.platform.cert.chain"),
            bytes(platform.toString()),
            bytes("|"),
            bytes(attestationKind.toString()),
            bytes("|"),
            bytes(rootKeyId),
            bytes("|"),
            bytes(certChainText),
        )
    }

    private fun livenessCid(text: String): ByteArray {
        return sha256Parts(bytes("v3.biometric.liveness"), bytes(text))
    }

    private fun attestationSignable(envelope: AttestationEnvelope): String {
        return buildString {
            append("v3_biometric_attestation_version=2\n")
            append("request_id=").append(hexUtf8(envelope.requestId)).append('\n')
            append("purpose=").append(envelope.purpose).append('\n')
            append("sensor_id=").append(hexUtf8(envelope.sensorId)).append('\n')
            append("platform=").append(envelope.platform).append('\n')
            append("attestation_kind=").append(envelope.attestationKind).append('\n')
            append("root_key_id=").append(hexUtf8(envelope.rootKeyId)).append('\n')
            append("cert_chain_cid=").append(hex(envelope.certChainCid)).append('\n')
            append("liveness_cid=").append(hex(envelope.livenessCid)).append('\n')
            append("device_binding_seed_hash=").append(hex(envelope.deviceBindingSeedHash)).append('\n')
            append("device_label=").append(hexUtf8(envelope.deviceLabel)).append('\n')
            append("feature_commitment=").append(hex(envelope.featureCommitment)).append('\n')
            append("attestor_pub_key=").append(hex(envelope.attestorPubKey)).append('\n')
        }
    }

    private fun attestationText(envelope: AttestationEnvelope, signatureRaw64: ByteArray): String {
        return attestationSignable(envelope) + "signature=" + hex(signatureRaw64) + "\n"
    }

    private fun bytes(text: String): ByteArray = text.toByteArray(StandardCharsets.UTF_8)

    private fun hexUtf8(text: String): String = hex(bytes(text))

    private fun sha256Parts(vararg parts: ByteArray): ByteArray {
        val digest = MessageDigest.getInstance("SHA-256")
        parts.forEach { digest.update(it) }
        return digest.digest()
    }

    private fun hex(bytes: ByteArray): String {
        val out = CharArray(bytes.size * 2)
        val digits = "0123456789abcdef"
        var i = 0
        while (i < bytes.size) {
            val v = bytes[i].toInt() and 0xff
            out[i * 2] = digits[v ushr 4]
            out[i * 2 + 1] = digits[v and 0x0f]
            i += 1
        }
        return String(out)
    }
}
