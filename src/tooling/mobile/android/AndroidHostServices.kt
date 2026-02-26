package com.cheng.mobile

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.hardware.biometrics.BiometricPrompt
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.location.LocationManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.wifi.WifiManager
import android.nfc.NfcAdapter
import android.nfc.Tag
import android.nfc.TagLostException
import android.nfc.tech.IsoDep
import android.os.Build
import android.os.CancellationSignal
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.lang.ref.WeakReference
import java.nio.charset.StandardCharsets
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.KeyFactory
import java.security.SecureRandom
import java.security.Signature
import java.security.spec.X509EncodedKeySpec
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import java.net.InetSocketAddress
import java.net.Socket

class AndroidHostServices(private val context: Context) {
    private val biometricErrorNegativeButton = 13

    @Volatile
    private var running = false
    private var worker: Thread? = null

    @Volatile
    private var activityRef: WeakReference<Activity>? = null

    private val nfcLock = Any()
    private var nfcSessionId = ""
    private var nfcIsoDep: IsoDep? = null
    private var nfcChipPubkeyB64 = ""
    private var nfcChallengeB64 = ""

    private val readerFlags =
        NfcAdapter.FLAG_READER_NFC_A or
            NfcAdapter.FLAG_READER_NFC_B or
            NfcAdapter.FLAG_READER_NFC_F or
            NfcAdapter.FLAG_READER_NFC_V or
            NfcAdapter.FLAG_READER_SKIP_NDEF_CHECK

    private val readerCallback = NfcAdapter.ReaderCallback { tag ->
        onNfcTagDiscovered(tag)
    }

    private data class Request(
        val id: String,
        val service: Int,
        val method: Int,
        val flags: Int,
        val payload: String,
    )

    private data class Response(
        val ok: Boolean,
        val payload: String,
        val error: String,
    )

    fun attachActivity(activity: Activity?) {
        activityRef = if (activity == null) null else WeakReference(activity)
    }

    private fun currentActivity(): Activity? = activityRef?.get()

    fun start() {
        if (running) return
        running = true
        worker = Thread {
            while (running) {
                val raw = ChengNative.pollBusRequestEnvelope() ?: ""
                if (raw.isEmpty()) {
                    try {
                        Thread.sleep(8)
                    } catch (_: InterruptedException) {
                    }
                    continue
                }

                val req = parseRequest(raw)
                val response = if (req == null) {
                    "unknown|0||invalid_envelope"
                } else {
                    val resp = handleRequest(req)
                    encodeResponse(req.id, resp)
                }
                ChengNative.pushBusResponseEnvelope(response)
            }
        }.apply {
            isDaemon = true
            name = "cheng-host-services"
            start()
        }
    }

    fun stop() {
        running = false
        worker?.interrupt()
        worker = null
        disableReaderMode()
    }

    private fun parseRequest(raw: String): Request? {
        val parts = raw.split("|", limit = 5)
        if (parts.size < 5) return null
        val service = parts[1].toIntOrNull() ?: return null
        val method = parts[2].toIntOrNull() ?: return null
        val flags = parts[3].toIntOrNull() ?: 0
        return Request(
            id = parts[0],
            service = service,
            method = method,
            flags = flags,
            payload = parts[4],
        )
    }

    private fun encodeResponse(id: String, resp: Response): String {
        return sanitize(id) + "|" +
            (if (resp.ok) "1" else "0") + "|" +
            sanitize(resp.payload) + "|" +
            sanitize(resp.error)
    }

    private fun sanitize(text: String): String {
        return text.replace('|', '/').replace('\n', ' ').replace('\r', ' ')
    }

    private fun methodHash(name: String): Int {
        var h = 0
        for (ch in name) {
            h = h * 131 + ch.code
            if (h < 0) h = -h
        }
        if (h < 0) return 0
        return h
    }

    private val methodCameraOpen = methodHash("open")
    private val methodCameraOpenFrontV2 = methodHash("open_front_v2")
    private val methodCameraClose = methodHash("close")
    private val methodCameraStartPreview = methodHash("start_preview")
    private val methodCameraStopPreview = methodHash("stop_preview")

    private val methodLocationStart = methodHash("start")
    private val methodLocationStop = methodHash("stop")
    private val methodLocationLastFix = methodHash("last_fix")

    private val methodNfcPayAuthorize = methodHash("pay_authorize")
    private val methodNfcReadTag = methodHash("read_tag")
    private val methodNfcStartPollV2 = methodHash("start_poll_v2")
    private val methodNfcPollTagV2 = methodHash("poll_tag_v2")
    private val methodNfcApduTransceiveV2 = methodHash("apdu_transceive_v2")
    private val methodNfcEndSessionV2 = methodHash("end_session_v2")
    private val methodNfcVerifyChipSigV2 = methodHash("verify_chip_sig_v2")

    private val methodSecureGenerateKey = methodHash("generate_key")
    private val methodSecureSign = methodHash("sign")
    private val methodSecureDeleteKey = methodHash("delete_key")
    private val methodSecureSignTxV2 = methodHash("sign_tx_v2")

    private val methodNetworkSubscribe = methodHash("subscribe")
    private val methodNetworkUnsubscribe = methodHash("unsubscribe")
    private val methodNetworkSnapshot = methodHash("snapshot")

    private val methodBiometricAuthorizeV2 = methodHash("authorize_v2")
    private val methodBiometricFacePrecheckV2 = methodHash("face_precheck_v2")
    private val methodBiometricFaceAuthorizeV2 = methodHash("face_authorize_v2")
    private val methodP2pBroadcastTxV1 = methodHash("broadcast_tx_v1")

    private fun hasPermission(permission: String): Boolean {
        return context.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED
    }

    private fun decodePayloadCompat(payload: String): String {
        if (!payload.startsWith("cbor64:")) {
            return payload
        }
        val encoded = payload.substring("cbor64:".length)
        val decoded = decodeBase64Url(encoded) ?: return ""
        return try {
            String(decoded, StandardCharsets.UTF_8)
        } catch (_: Exception) {
            ""
        }
    }

    private fun decodeBase64Url(text: String): ByteArray? {
        if (text.isEmpty()) return ByteArray(0)
        val flags = Base64.URL_SAFE or Base64.NO_WRAP
        val padded = text + "==="
        return try {
            Base64.decode(text, flags)
        } catch (_: Exception) {
            try {
                Base64.decode(padded, flags)
            } catch (_: Exception) {
                null
            }
        }
    }

    private fun decodeBase64Any(text: String): ByteArray? {
        if (text.isEmpty()) return ByteArray(0)
        return decodeBase64Url(text) ?: run {
            try {
                Base64.decode(text, Base64.NO_WRAP)
            } catch (_: Exception) {
                try {
                    Base64.decode(text, Base64.DEFAULT)
                } catch (_: Exception) {
                    null
                }
            }
        }
    }

    private fun parsePayloadKv(payload: String): Map<String, String> {
        val raw = decodePayloadCompat(payload)
        if (raw.isEmpty()) return emptyMap()
        val out = LinkedHashMap<String, String>()
        val segments = raw.split(';')
        for (seg in segments) {
            if (seg.isEmpty()) continue
            val i = seg.indexOf('=')
            if (i <= 0) continue
            val key = seg.substring(0, i)
            val value = if (i + 1 < seg.length) seg.substring(i + 1) else ""
            out[key] = value
        }
        return out
    }

    private fun handleRequest(req: Request): Response {
        return when (req.service) {
            1 -> handleCamera(req)
            2 -> handleLocation(req)
            3 -> handleNfc(req)
            4 -> handleSecureStore(req)
            5 -> handleNetworkState(req)
            6 -> handleBiometric(req)
            7 -> handleP2pDirect(req)
            else -> Response(ok = false, payload = "", error = "unsupported_service")
        }
    }

    private fun handleCamera(req: Request): Response {
        val manager = context.getSystemService(Context.CAMERA_SERVICE) as? CameraManager
            ?: return Response(false, "", "camera_unavailable")
        if (!hasPermission(Manifest.permission.CAMERA)) {
            return Response(false, "", "permission_denied_camera")
        }
        val preferFront = req.method == methodCameraOpenFrontV2
        val cameraId = pickCameraId(manager, preferFront)
        if (cameraId.isEmpty()) {
            return Response(false, "", "camera_not_found")
        }

        return when (req.method) {
            methodCameraOpenFrontV2 -> Response(true, "camera_id=$cameraId;lens=front;mode=host_managed", "")
            methodCameraOpen -> Response(true, "camera_id=$cameraId;mode=host_managed", "")
            methodCameraStartPreview -> Response(true, "camera_id=$cameraId;preview=native_buffer", "")
            methodCameraStopPreview -> Response(true, "camera_id=$cameraId;preview=stopped", "")
            methodCameraClose -> Response(true, "camera_id=$cameraId;closed=1", "")
            else -> Response(false, "", "unsupported_method")
        }
    }

    private fun pickCameraId(manager: CameraManager, preferFront: Boolean): String {
        return try {
            val all = manager.cameraIdList
            if (!preferFront) {
                return all.firstOrNull() ?: ""
            }
            for (id in all) {
                val ch = manager.getCameraCharacteristics(id)
                val facing = ch.get(CameraCharacteristics.LENS_FACING)
                if (facing == CameraCharacteristics.LENS_FACING_FRONT) {
                    return id
                }
            }
            all.firstOrNull() ?: ""
        } catch (_: Exception) {
            ""
        }
    }

    private fun handleLocation(req: Request): Response {
        if (!hasPermission(Manifest.permission.ACCESS_FINE_LOCATION) &&
            !hasPermission(Manifest.permission.ACCESS_COARSE_LOCATION)
        ) {
            return Response(false, "", "permission_denied_location")
        }

        val lm = context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
            ?: return Response(false, "", "location_unavailable")

        return when (req.method) {
            methodLocationStart -> Response(true, "stream=registered", "")
            methodLocationStop -> Response(true, "stream=stopped", "")
            methodLocationLastFix -> {
                val providers = listOf(
                    LocationManager.GPS_PROVIDER,
                    LocationManager.NETWORK_PROVIDER,
                    LocationManager.PASSIVE_PROVIDER,
                )
                var best: android.location.Location? = null
                for (p in providers) {
                    val fix = try {
                        lm.getLastKnownLocation(p)
                    } catch (_: SecurityException) {
                        null
                    } catch (_: Exception) {
                        null
                    }
                    if (fix != null) {
                        best = fix
                        break
                    }
                }
                if (best == null) {
                    Response(false, "", "location_unavailable")
                } else {
                    val payload = "lat=${best.latitude};lng=${best.longitude};acc=${best.accuracy};time_ms=${best.time}"
                    Response(true, payload, "")
                }
            }

            else -> Response(false, "", "unsupported_method")
        }
    }

    private fun handleNfc(req: Request): Response {
        if (!hasPermission(Manifest.permission.NFC)) {
            return Response(false, "", "permission_denied_nfc")
        }
        val adapter = NfcAdapter.getDefaultAdapter(context)
            ?: return Response(false, "", "nfc_unavailable")
        if (!adapter.isEnabled) {
            return Response(false, "", "nfc_disabled")
        }
        return when (req.method) {
            methodNfcPayAuthorize -> Response(false, "", "wallet_ui_required")
            methodNfcReadTag -> Response(true, "nfc=ready;mode=foreground_dispatch", "")
            methodNfcStartPollV2 -> handleNfcStartPollV2(adapter)
            methodNfcPollTagV2 -> handleNfcPollTagV2()
            methodNfcApduTransceiveV2 -> handleNfcApduTransceiveV2(req)
            methodNfcEndSessionV2 -> handleNfcEndSessionV2()
            methodNfcVerifyChipSigV2 -> handleNfcVerifyChipSigV2(req)
            else -> Response(false, "", "unsupported_method")
        }
    }

    private fun onNfcTagDiscovered(tag: Tag) {
        val iso = IsoDep.get(tag)
        if (iso == null) {
            synchronized(nfcLock) {
                nfcIsoDep = null
            }
            return
        }

        val challenge = ByteArray(16)
        SecureRandom().nextBytes(challenge)
        val tagId = tag.id ?: ByteArray(0)

        synchronized(nfcLock) {
            nfcIsoDep = iso
            if (nfcSessionId.isEmpty()) {
                nfcSessionId = UUID.randomUUID().toString()
            }
            nfcChallengeB64 = Base64.encodeToString(challenge, Base64.NO_WRAP)
            nfcChipPubkeyB64 = Base64.encodeToString(tagId, Base64.NO_WRAP)
        }
    }

    private fun handleNfcStartPollV2(adapter: NfcAdapter): Response {
        val activity = currentActivity() ?: return Response(false, "", "activity_required")
        val latch = CountDownLatch(1)
        activity.runOnUiThread {
            try {
                synchronized(nfcLock) {
                    nfcSessionId = UUID.randomUUID().toString()
                    nfcIsoDep = null
                    nfcChipPubkeyB64 = ""
                    nfcChallengeB64 = ""
                }
                adapter.enableReaderMode(activity, readerCallback, readerFlags, null)
            } catch (_: Exception) {
            } finally {
                latch.countDown()
            }
        }
        latch.await(2, TimeUnit.SECONDS)
        val sid = synchronized(nfcLock) { nfcSessionId }
        return Response(true, "session_id=$sid;mode=reader", "")
    }

    private fun handleNfcPollTagV2(): Response {
        val (sid, chip, challenge, hasTag) = synchronized(nfcLock) {
            val found = nfcIsoDep != null
            Quad(nfcSessionId, nfcChipPubkeyB64, nfcChallengeB64, found)
        }
        if (!hasTag) {
            return Response(true, "session_id=$sid;pending=1", "")
        }
        return Response(
            true,
            "session_id=$sid;chip_pubkey_b64=$chip;challenge_b64=$challenge",
            "",
        )
    }

    private fun handleNfcApduTransceiveV2(req: Request): Response {
        val kv = parsePayloadKv(req.payload)
        val sessionIdReq = kv["session_id"] ?: ""
        val apduB64 = kv["apdu_b64"] ?: ""

        val (sid, iso) = synchronized(nfcLock) {
            Pair(nfcSessionId, nfcIsoDep)
        }
        if (iso == null) {
            return Response(false, "", "nfc_tag_lost")
        }
        if (sessionIdReq.isNotEmpty() && sid.isNotEmpty() && sid != sessionIdReq) {
            return Response(false, "", "nfc_tag_lost")
        }
        val apdu = decodeBase64Url(apduB64)
            ?: return Response(false, "", "invalid_apdu")
        return try {
            if (!iso.isConnected) {
                iso.connect()
            }
            val resp = iso.transceive(apdu)
            val b64 = Base64.encodeToString(resp, Base64.NO_WRAP)
            Response(true, "session_id=$sid;apdu_resp_b64=$b64", "")
        } catch (_: TagLostException) {
            synchronized(nfcLock) {
                nfcIsoDep = null
            }
            Response(false, "", "nfc_tag_lost")
        } catch (_: Exception) {
            Response(false, "", "nfc_io_error")
        }
    }

    private fun handleNfcEndSessionV2(): Response {
        disableReaderMode()
        synchronized(nfcLock) {
            nfcIsoDep = null
            nfcChipPubkeyB64 = ""
            nfcChallengeB64 = ""
        }
        return Response(true, "ended=1", "")
    }

    private fun handleNfcVerifyChipSigV2(req: Request): Response {
        if (!ed25519Supported()) {
            return Response(false, "", "chip_algo_unsupported")
        }
        val kv = parsePayloadKv(req.payload)
        val pubB64 = kv["chip_pubkey_b64"] ?: kv["pubkey_b64"] ?: ""
        val challengeB64 = kv["challenge_b64"] ?: ""
        val signatureB64 = kv["signature_b64"] ?: ""
        val pub = decodeBase64Url(pubB64) ?: return Response(false, "", "chip_verify_failed")
        val msg = decodeBase64Url(challengeB64) ?: return Response(false, "", "chip_verify_failed")
        val sig = decodeBase64Url(signatureB64) ?: return Response(false, "", "chip_verify_failed")
        val verified = verifyEd25519(pub, msg, sig)
        if (!verified) {
            return Response(false, "", "chip_verify_failed")
        }
        return Response(true, "verified=1;algo=ed25519", "")
    }

    private fun ed25519Supported(): Boolean {
        return try {
            KeyFactory.getInstance("Ed25519")
            Signature.getInstance("Ed25519")
            true
        } catch (_: Exception) {
            false
        }
    }

    private fun verifyEd25519(publicKeyBytes: ByteArray, message: ByteArray, signature: ByteArray): Boolean {
        return try {
            val keyBytes = toEd25519Spki(publicKeyBytes)
            val keyFactory = KeyFactory.getInstance("Ed25519")
            val pub = keyFactory.generatePublic(X509EncodedKeySpec(keyBytes))
            val verifier = Signature.getInstance("Ed25519")
            verifier.initVerify(pub)
            verifier.update(message)
            verifier.verify(signature)
        } catch (_: Exception) {
            false
        }
    }

    private fun toEd25519Spki(rawOrSpki: ByteArray): ByteArray {
        if (rawOrSpki.size != 32) {
            return rawOrSpki
        }
        val prefix = byteArrayOf(
            0x30, 0x2A, 0x30, 0x05, 0x06, 0x03, 0x2B, 0x65, 0x70, 0x03, 0x21, 0x00,
        )
        val out = ByteArray(prefix.size + rawOrSpki.size)
        System.arraycopy(prefix, 0, out, 0, prefix.size)
        System.arraycopy(rawOrSpki, 0, out, prefix.size, rawOrSpki.size)
        return out
    }

    private fun disableReaderMode() {
        val adapter = NfcAdapter.getDefaultAdapter(context) ?: return
        val activity = currentActivity() ?: return
        try {
            activity.runOnUiThread {
                try {
                    adapter.disableReaderMode(activity)
                } catch (_: Exception) {
                }
            }
        } catch (_: Exception) {
        }
    }

    private fun loadSecureEntry(alias: String): KeyStore.PrivateKeyEntry? {
        val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        return keyStore.getEntry(alias, null) as? KeyStore.PrivateKeyEntry
    }

    private fun secureSigScheme(privateKey: java.security.PrivateKey): String {
        val algo = privateKey.algorithm.uppercase()
        if (algo == "ED25519" || algo == "EDDSA") {
            return "ed25519"
        }
        if (algo == "RSA") {
            return "rsa_pkcs1_sha256"
        }
        return "ecdsa_p256"
    }

    private fun hasPrefix(data: ByteArray, prefix: ByteArray): Boolean {
        if (data.size < prefix.size) {
            return false
        }
        for (i in prefix.indices) {
            if (data[i] != prefix[i]) {
                return false
            }
        }
        return true
    }

    private fun extractEd25519RawPublicKey(pubEncoded: ByteArray): ByteArray? {
        if (pubEncoded.size == 32) {
            return pubEncoded
        }
        val prefix = byteArrayOf(
            0x30, 0x2A, 0x30, 0x05, 0x06, 0x03, 0x2B, 0x65, 0x70, 0x03, 0x21, 0x00,
        )
        if (pubEncoded.size == prefix.size + 32 && hasPrefix(pubEncoded, prefix)) {
            return pubEncoded.copyOfRange(prefix.size, pubEncoded.size)
        }
        if (pubEncoded.size > 32) {
            return pubEncoded.copyOfRange(pubEncoded.size - 32, pubEncoded.size)
        }
        return null
    }

    private fun normalizePublicKeyBytes(sigScheme: String, encoded: ByteArray): ByteArray {
        if (sigScheme == "ed25519") {
            return extractEd25519RawPublicKey(encoded) ?: encoded
        }
        return encoded
    }

    private fun makeKeyInfoPayload(alias: String, sigScheme: String, pubBytes: ByteArray): String {
        val pubB64 = Base64.encodeToString(pubBytes, Base64.NO_WRAP)
        return "alias=$alias;sig_scheme=$sigScheme;pubkey_b64=$pubB64"
    }

    private fun generateSecureKey(alias: String, algoRaw: String, requireUserAuth: Boolean): Response {
        val algo = algoRaw.uppercase()
        val existing = loadSecureEntry(alias)
        if (existing != null) {
            val scheme = secureSigScheme(existing.privateKey)
            if (algo == "ED25519" && scheme != "ed25519") {
                return Response(false, "", "sig_scheme_unsupported")
            }
            val pubBytes = normalizePublicKeyBytes(scheme, existing.certificate.publicKey.encoded)
            return Response(true, makeKeyInfoPayload(alias, scheme, pubBytes), "")
        }

        return when (algo) {
            "ED25519" -> {
                val generator = try {
                    KeyPairGenerator.getInstance("Ed25519", "AndroidKeyStore")
                } catch (_: Exception) {
                    null
                } ?: return Response(false, "", "sig_scheme_unsupported")

                val builder = KeyGenParameterSpec.Builder(
                    alias,
                    KeyProperties.PURPOSE_SIGN or KeyProperties.PURPOSE_VERIFY,
                )
                if (requireUserAuth && Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    builder.setUserAuthenticationParameters(
                        0,
                        KeyProperties.AUTH_BIOMETRIC_STRONG,
                    )
                }
                generator.initialize(builder.build())
                generator.generateKeyPair()
                val created = loadSecureEntry(alias) ?: return Response(false, "", "key_not_found")
                val scheme = secureSigScheme(created.privateKey)
                if (scheme != "ed25519") {
                    return Response(false, "", "sig_scheme_unsupported")
                }
                val pubBytes = normalizePublicKeyBytes(scheme, created.certificate.publicKey.encoded)
                Response(true, makeKeyInfoPayload(alias, scheme, pubBytes), "")
            }

            "RSA", "EC" -> {
                val genAlgo = if (algo == "RSA") KeyProperties.KEY_ALGORITHM_RSA else KeyProperties.KEY_ALGORITHM_EC
                val generator = KeyPairGenerator.getInstance(genAlgo, "AndroidKeyStore")
                val builder = KeyGenParameterSpec.Builder(
                    alias,
                    KeyProperties.PURPOSE_SIGN or KeyProperties.PURPOSE_VERIFY,
                ).setDigests(
                    KeyProperties.DIGEST_SHA256,
                    KeyProperties.DIGEST_SHA512,
                )
                if (genAlgo == KeyProperties.KEY_ALGORITHM_RSA) {
                    builder.setSignaturePaddings(KeyProperties.SIGNATURE_PADDING_RSA_PKCS1)
                }
                if (requireUserAuth && Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    builder.setUserAuthenticationParameters(
                        0,
                        KeyProperties.AUTH_BIOMETRIC_STRONG,
                    )
                }
                generator.initialize(builder.build())
                generator.generateKeyPair()
                val created = loadSecureEntry(alias) ?: return Response(false, "", "key_not_found")
                val scheme = secureSigScheme(created.privateKey)
                val pubBytes = normalizePublicKeyBytes(scheme, created.certificate.publicKey.encoded)
                Response(true, makeKeyInfoPayload(alias, scheme, pubBytes), "")
            }

            else -> Response(false, "", "unsupported_algorithm")
        }
    }

    private fun handleSecureStore(req: Request): Response {
        val kv = parsePayloadKv(req.payload)
        val alias = kv["alias"] ?: kv["keyAlias"] ?: "cheng_mobile_default"
        return try {
            when (req.method) {
                methodSecureGenerateKey -> {
                    val algo = kv["algo"] ?: "EC"
                    val requireUserAuth = kv["require_user_auth"] == "1"
                    generateSecureKey(alias, algo, requireUserAuth)
                }

                methodSecureSign -> {
                    signWithAlias(alias, kv["digest"] ?: kv["digestCbor"] ?: req.payload)
                }

                methodSecureDeleteKey -> {
                    val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
                    if (keyStore.containsAlias(alias)) {
                        keyStore.deleteEntry(alias)
                    }
                    Response(true, "alias=$alias;deleted=1", "")
                }

                methodSecureSignTxV2 -> {
                    val prompt = kv["prompt"] ?: "Authorize RWAD transfer"
                    val auth = authenticateBiometricStrong(prompt)
                    if (!auth.first) {
                        return Response(false, "", auth.second)
                    }
                    val digestBytes = if ((kv["digest_b64"] ?: "").isNotEmpty()) {
                        decodeBase64Any(kv["digest_b64"] ?: "") ?: return Response(false, "", "invalid_digest")
                    } else {
                        (kv["digest"] ?: req.payload).toByteArray(StandardCharsets.UTF_8)
                    }
                    signWithAliasBytes(alias, digestBytes)
                }

                else -> Response(false, "", "unsupported_method")
            }
        } catch (e: Exception) {
            Response(false, "", mapSecureStoreError(e))
        }
    }

    private fun signWithAlias(alias: String, digestRaw: String): Response {
        return signWithAliasBytes(alias, digestRaw.toByteArray(StandardCharsets.UTF_8))
    }

    private fun signWithAliasBytes(alias: String, digestBytes: ByteArray): Response {
        val entry = loadSecureEntry(alias)
            ?: return Response(false, "", "key_not_found")
        val privateKey = entry.privateKey
        val sigScheme = secureSigScheme(privateKey)
        val signatureAlgo = when (sigScheme) {
            "ed25519" -> "Ed25519"
            "rsa_pkcs1_sha256" -> "SHA256withRSA"
            else -> "SHA256withECDSA"
        }
        val sig = Signature.getInstance(signatureAlgo)
        sig.initSign(privateKey)
        sig.update(digestBytes)
        val signed = sig.sign()
        val b64 = Base64.encodeToString(signed, Base64.NO_WRAP)
        val pubBytes = normalizePublicKeyBytes(sigScheme, entry.certificate.publicKey.encoded)
        val pubB64 = Base64.encodeToString(pubBytes, Base64.NO_WRAP)
        return Response(true, "alias=$alias;sig_b64=$b64;sig_scheme=$sigScheme;pubkey_b64=$pubB64", "")
    }

    private fun mapSecureStoreError(e: Exception): String {
        val name = e::class.java.name
        if (name.contains("KeyPermanentlyInvalidated", ignoreCase = true)) {
            return "key_invalidated"
        }
        return "sign_failed"
    }

    private fun handleBiometric(req: Request): Response {
        val kv = parsePayloadKv(req.payload)
        return when (req.method) {
            methodBiometricAuthorizeV2 -> {
                val prompt = kv["prompt"] ?: "Authorize action"
                val auth = authenticateBiometricStrong(prompt)
                if (!auth.first) {
                    return Response(false, "", auth.second)
                }
                Response(true, "authorized=1", "")
            }

            methodBiometricFacePrecheckV2 -> {
                val supported = if (isFaceHardwareAvailable()) 1 else 0
                if (supported == 0) {
                    Response(false, "face_supported=0", "biometric_not_available")
                } else {
                    Response(true, "face_supported=1", "")
                }
            }

            methodBiometricFaceAuthorizeV2 -> {
                if (!isFaceHardwareAvailable()) {
                    return Response(false, "face_supported=0", "biometric_not_available")
                }
                val prompt = kv["prompt"] ?: "Face authorization required"
                val auth = authenticateBiometricStrong(prompt)
                if (!auth.first) {
                    return Response(false, "", auth.second)
                }
                Response(true, "authorized=1;modality=face", "")
            }

            else -> Response(false, "", "unsupported_method")
        }
    }

    private fun isFaceHardwareAvailable(): Boolean {
        val activity = currentActivity() ?: return false
        return activity.packageManager.hasSystemFeature(PackageManager.FEATURE_FACE)
    }

    private fun authenticateBiometricStrong(promptText: String): Pair<Boolean, String> {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.P) {
            return Pair(false, "biometric_not_available")
        }
        val activity = currentActivity() ?: return Pair(false, "activity_required")

        val hasBioFeature =
            activity.packageManager.hasSystemFeature(PackageManager.FEATURE_FINGERPRINT) ||
                activity.packageManager.hasSystemFeature(PackageManager.FEATURE_FACE)
        if (!hasBioFeature) {
            return Pair(false, "biometric_not_available")
        }

        val latch = CountDownLatch(1)
        val result = AtomicReference(false)
        val error = AtomicReference("biometric_not_available")

        activity.runOnUiThread {
            try {
                val executor = activity.mainExecutor
                val callback = object : BiometricPrompt.AuthenticationCallback() {
                    override fun onAuthenticationError(errorCode: Int, errString: CharSequence?) {
                        error.set(
                            when (errorCode) {
                                BiometricPrompt.BIOMETRIC_ERROR_USER_CANCELED,
                                BiometricPrompt.BIOMETRIC_ERROR_CANCELED,
                                biometricErrorNegativeButton -> "biometric_user_cancelled"
                                BiometricPrompt.BIOMETRIC_ERROR_LOCKOUT,
                                BiometricPrompt.BIOMETRIC_ERROR_LOCKOUT_PERMANENT -> "biometric_lockout"
                                else -> "biometric_not_available"
                            },
                        )
                        latch.countDown()
                    }

                    override fun onAuthenticationSucceeded(resultInfo: BiometricPrompt.AuthenticationResult?) {
                        result.set(true)
                        error.set("")
                        latch.countDown()
                    }

                    override fun onAuthenticationFailed() {
                        // Wait for explicit success or error callback.
                    }
                }

                val prompt = BiometricPrompt.Builder(activity)
                    .setTitle("Biometric Authorization")
                    .setDescription(promptText)
                    .setNegativeButton("Cancel", executor) { _, _ ->
                        error.set("biometric_user_cancelled")
                        latch.countDown()
                    }
                    .build()

                prompt.authenticate(CancellationSignal(), executor, callback)
            } catch (_: Exception) {
                error.set("biometric_not_available")
                latch.countDown()
            }
        }

        val completed = latch.await(35, TimeUnit.SECONDS)
        if (!completed) {
            return Pair(false, "biometric_user_cancelled")
        }
        if (!result.get()) {
            return Pair(false, error.get())
        }
        return Pair(true, "")
    }

    private fun handleP2pDirect(req: Request): Response {
        return when (req.method) {
            methodP2pBroadcastTxV1 -> handleP2pBroadcastTxV1(req)
            else -> Response(false, "", "unsupported_method")
        }
    }

    private fun handleP2pBroadcastTxV1(req: Request): Response {
        val kv = parsePayloadKv(req.payload)
        val envelopeB64 = kv["envelope_b64"] ?: kv["tx_envelope_b64"] ?: ""
        val txBytes = if (envelopeB64.isNotEmpty()) {
            decodeBase64Any(envelopeB64) ?: return Response(false, "", "p2p_broadcast_failed")
        } else if ((kv["tx_b64"] ?: "").isNotEmpty()) {
            decodeBase64Any(kv["tx_b64"] ?: "") ?: return Response(false, "", "p2p_broadcast_failed")
        } else {
            (kv["tx"] ?: req.payload).toByteArray(StandardCharsets.UTF_8)
        }
        val txHash = kv["tx_hash"] ?: ""
        val chain = kv["chain"] ?: ""

        val peersRaw = kv["peers"] ?: kv["peer"] ?: ""
        if (peersRaw.isEmpty()) {
            return Response(false, "", "p2p_unavailable")
        }
        val peers = peersRaw.split(',').map { it.trim() }.filter { it.isNotEmpty() }
        for (peer in peers) {
            if (sendTcp(peer, txBytes)) {
                val chainPart = if (chain.isEmpty()) "" else ";chain=$chain"
                val txHashPart = if (txHash.isEmpty()) "" else ";tx_hash=$txHash"
                return Response(true, "peer=$peer;bytes=${txBytes.size};transport=tcp$chainPart$txHashPart", "")
            }
        }
        return Response(false, "", "p2p_broadcast_failed")
    }

    private fun sendTcp(peer: String, payload: ByteArray): Boolean {
        val idx = peer.lastIndexOf(':')
        if (idx <= 0 || idx >= peer.length - 1) return false
        val host = peer.substring(0, idx)
        val port = peer.substring(idx + 1).toIntOrNull() ?: return false
        return try {
            Socket().use { socket ->
                socket.tcpNoDelay = true
                socket.soTimeout = 3000
                socket.connect(InetSocketAddress(host, port), 2500)
                socket.getOutputStream().use { out ->
                    out.write(payload)
                    out.flush()
                }
            }
            true
        } catch (_: Exception) {
            false
        }
    }

    @Suppress("DEPRECATION")
    private fun handleNetworkState(req: Request): Response {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
            ?: return Response(false, "", "network_unavailable")

        return when (req.method) {
            methodNetworkSubscribe -> Response(true, "network=subscribed", "")
            methodNetworkUnsubscribe -> Response(true, "network=unsubscribed", "")
            methodNetworkSnapshot -> {
                val network = cm.activeNetwork
                val caps = if (network != null) cm.getNetworkCapabilities(network) else null
                val connected = caps != null
                val transport = when {
                    caps?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true -> "wifi"
                    caps?.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) == true -> "cellular"
                    caps?.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET) == true -> "ethernet"
                    caps?.hasTransport(NetworkCapabilities.TRANSPORT_VPN) == true -> "vpn"
                    else -> "unknown"
                }
                val expensive = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                    caps?.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED) == false
                } else {
                    false
                }

                var ssid = ""
                var bssid = ""
                try {
                    val wifi = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
                    val info = wifi?.connectionInfo
                    ssid = info?.ssid?.replace("\"", "") ?: ""
                    bssid = info?.bssid ?: ""
                } catch (_: Exception) {
                }

                var ipAddr = ""
                try {
                    val lp = if (network != null) cm.getLinkProperties(network) else null
                    ipAddr = lp?.linkAddresses?.firstOrNull()?.address?.hostAddress ?: ""
                } catch (_: Exception) {
                }

                Response(
                    true,
                    "connected=${if (connected) 1 else 0};transport=$transport;expensive=${if (expensive) 1 else 0};ssid=$ssid;bssid=$bssid;ip=$ipAddr",
                    "",
                )
            }

            else -> Response(false, "", "unsupported_method")
        }
    }

    private data class Quad(
        val a: String,
        val b: String,
        val c: String,
        val d: Boolean,
    )
}
