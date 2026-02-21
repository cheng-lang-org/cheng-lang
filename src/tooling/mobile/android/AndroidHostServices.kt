package com.cheng.mobile

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.hardware.camera2.CameraManager
import android.location.LocationManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.wifi.WifiManager
import android.nfc.NfcAdapter
import android.os.Build
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.Signature

class AndroidHostServices(private val context: Context) {
    @Volatile
    private var running = false
    private var worker: Thread? = null

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
    private val methodCameraClose = methodHash("close")
    private val methodCameraStartPreview = methodHash("start_preview")
    private val methodCameraStopPreview = methodHash("stop_preview")

    private val methodLocationStart = methodHash("start")
    private val methodLocationStop = methodHash("stop")
    private val methodLocationLastFix = methodHash("last_fix")

    private val methodNfcPayAuthorize = methodHash("pay_authorize")
    private val methodNfcReadTag = methodHash("read_tag")

    private val methodSecureGenerateKey = methodHash("generate_key")
    private val methodSecureSign = methodHash("sign")
    private val methodSecureDeleteKey = methodHash("delete_key")

    private val methodNetworkSubscribe = methodHash("subscribe")
    private val methodNetworkUnsubscribe = methodHash("unsubscribe")
    private val methodNetworkSnapshot = methodHash("snapshot")

    private fun hasPermission(permission: String): Boolean {
        return context.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED
    }

    private fun parsePayloadKv(payload: String): Map<String, String> {
        if (payload.isEmpty()) return emptyMap()
        val out = LinkedHashMap<String, String>()
        val segments = payload.split(';')
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
            else -> Response(ok = false, payload = "", error = "unsupported_service")
        }
    }

    private fun handleCamera(req: Request): Response {
        val manager = context.getSystemService(Context.CAMERA_SERVICE) as? CameraManager
            ?: return Response(false, "", "camera_unavailable")
        if (!hasPermission(Manifest.permission.CAMERA)) {
            return Response(false, "", "permission_denied_camera")
        }
        val cameraId = try {
            manager.cameraIdList.firstOrNull() ?: ""
        } catch (_: Exception) {
            ""
        }
        if (cameraId.isEmpty()) {
            return Response(false, "", "camera_not_found")
        }

        return when (req.method) {
            methodCameraOpen -> Response(true, "camera_id=$cameraId;mode=host_managed", "")
            methodCameraStartPreview -> Response(true, "camera_id=$cameraId;preview=native_buffer", "")
            methodCameraStopPreview -> Response(true, "camera_id=$cameraId;preview=stopped", "")
            methodCameraClose -> Response(true, "camera_id=$cameraId;closed=1", "")
            else -> Response(false, "", "unsupported_method")
        }
    }

    private fun handleLocation(req: Request): Response {
        if (!hasPermission(Manifest.permission.ACCESS_FINE_LOCATION) &&
            !hasPermission(Manifest.permission.ACCESS_COARSE_LOCATION)) {
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
        val adapter = NfcAdapter.getDefaultAdapter(context)
            ?: return Response(false, "", "nfc_unavailable")
        if (!adapter.isEnabled) {
            return Response(false, "", "nfc_disabled")
        }
        return when (req.method) {
            methodNfcPayAuthorize -> Response(false, "", "wallet_ui_required")
            methodNfcReadTag -> Response(true, "nfc=ready;mode=foreground_dispatch", "")
            else -> Response(false, "", "unsupported_method")
        }
    }

    private fun handleSecureStore(req: Request): Response {
        val kv = parsePayloadKv(req.payload)
        val alias = kv["alias"] ?: kv["keyAlias"] ?: "cheng_mobile_default"
        return try {
            when (req.method) {
                methodSecureGenerateKey -> {
                    val algo = (kv["algo"] ?: "EC").uppercase()
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
                    generator.initialize(builder.build())
                    generator.generateKeyPair()
                    Response(true, "alias=$alias;algo=$genAlgo", "")
                }
                methodSecureSign -> {
                    val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
                    val entry = keyStore.getEntry(alias, null) as? KeyStore.PrivateKeyEntry
                        ?: return Response(false, "", "key_not_found")
                    val privateKey = entry.privateKey
                    val signatureAlgo = when (privateKey.algorithm.uppercase()) {
                        "RSA" -> "SHA256withRSA"
                        else -> "SHA256withECDSA"
                    }
                    val sig = Signature.getInstance(signatureAlgo)
                    sig.initSign(privateKey)
                    val digestRaw = kv["digest"] ?: kv["digestCbor"] ?: req.payload
                    sig.update(digestRaw.toByteArray(Charsets.UTF_8))
                    val signed = sig.sign()
                    val b64 = Base64.encodeToString(signed, Base64.NO_WRAP)
                    Response(true, "alias=$alias;sig_b64=$b64", "")
                }
                methodSecureDeleteKey -> {
                    val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
                    if (keyStore.containsAlias(alias)) {
                        keyStore.deleteEntry(alias)
                    }
                    Response(true, "alias=$alias;deleted=1", "")
                }
                else -> Response(false, "", "unsupported_method")
            }
        } catch (e: Exception) {
            Response(false, "", "secure_store_error")
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
}
