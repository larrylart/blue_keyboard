////////////////////////////////////////////////////////////////////
// BleHub
// Created by: Larry Lart
//
// High-level BLE + micro-TLS hub for the Blue Keyboard dongle.
//
//  - Own a singleton BluetoothDeviceManager and app Context
//  - Auto-connect to the preferred dongle on app start
//  - Manage provisioning (APPKEY) and secure session (MTLS)
//  - Hide all binary framing (B0/B1/B2, B3, D0/D1, C0/C1/C2, etc.)
//  - Expose simple methods to:
//      * connect from Settings (with UI prompts)
//      * send passwords / strings
//      * query / set layout
//      * reset dongle to defaults
//
// NOTE: This file is tightly coupled to the Blue Keyboard firmware
// protocol. If the protocol changes, most adjustments happen here.
////////////////////////////////////////////////////////////////////
package com.blu.blukeyborg

import android.content.Context
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import java.util.UUID
import com.blu.blukeyborg.PreferencesUtil
import android.os.SystemClock
import kotlin.math.min
import com.blu.blukeyborg.BleAppSec

import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothAdapter

import com.blu.blukeyborg.BuildConfig
import android.util.Log

// Tag + guarded logging helpers.
// Only emit logs in debug builds to avoid leaking protocol details
// or passwords in production. LOG_ENABLED is guarded by BuildConfig.DEBUG.
private const val TAG = "BleHub"
// Only log in debug builds
private val LOG_ENABLED = BuildConfig.DEBUG

private fun logd(msg: String) {
    if (LOG_ENABLED) Log.d(TAG, msg)
}

private fun loge(msg: String, tr: Throwable? = null) {
    if (LOG_ENABLED) {
        if (tr != null) Log.e(TAG, msg, tr) else Log.e(TAG, msg)
    }
}

// Plain-text "unprovisioned" banner parser.
//
// When the dongle is not provisioned, it sends an ASCII line like:
//   LAYOUT=US_WINLIN; PROTO=1.2; FW=1.2.1
//
// This is used to:
//   - detect unprovisioned state (no APPKEY yet)
//   - pick up the dongle's preferred keyboard layout
//   - show protocol/firmware versions in logs or UI.
private val plainInfoRx = Regex("""\bLAYOUT=([A-Z0-9_]+);\s*PROTO=([0-9.]+);\s*FW=([0-9.]+)\b""")
private fun parsePlainInfo(bytes: ByteArray): Triple<String,String,String>? {
    val s = bytes.toString(Charsets.UTF_8)
    val m = plainInfoRx.find(s) ?: return null
    return Triple(m.groupValues[1], m.groupValues[2], m.groupValues[3])
}

// Micro-TLS (MTLS) client state for the active session.
//
//  sid     : 32-bit session id negotiated during B0/B1/B2.
//  sessKey : 32-byte session key derived via ECDH + HKDF.
//  seqOut  : 16-bit client sequence counter for B3 frames.
private data class MtlsState(
    var sid: Int = 0,
    var sessKey: ByteArray? = null,
    var seqOut: Int = 0
)

private var mtls: MtlsState? = null

// Convenience hex helpers for logging and debug tooling.
private fun ByteArray.toHex(): String = joinToString("") { "%02x".format(it) }
private fun String.hexToBytes(): ByteArray {
    require(length % 2 == 0) { "hex len" }
    return ByteArray(length / 2) { i -> substring(2 * i, 2 * i + 2).toInt(16).toByte() }
}

////////////////////////////////////////////////////////////////////
// appCtx  : global Application context (set via BleHub.init()).
// _connected : LiveData to expose "secure session is ready" to the UI.
// SERVICE_UUID / CHAR_UUID / RX_UUID : Nordic UART-style service/char UUIDs used by the dongle firmware.
////////////////////////////////////////////////////////////////////
object BleHub 
{
    private lateinit var appCtx: Context
    private val _connected = MutableLiveData(false)
    val connected: LiveData<Boolean> = _connected

	// Tracks whether BleHub is currently running a heavy connect/handshake flow,
	// e.g. autoConnectFromPrefs() or connectAndEstablishSecure().
	@Volatile
	private var connectInProgress: Boolean = false

	// Helper for user-facing operations: if a connect/handshake is already in
	// progress, short-circuit with a clear error and avoid starting a new one.
	private inline fun failIfConnecting(
		crossinline onResult: (Boolean, String?) -> Unit
	): Boolean {
		if (connectInProgress) {
			onResult(false, "Connecting to dongle, please wait…")
			return true
		}
		return false
	}

    // Tracks whether the current BLE/MTLS session has "fast keys" enabled
    // on the dongle (raw key tap mode). Resets on disconnect/handshake loss.
    private var fastKeysSessionEnabled: Boolean = false

    fun isFastKeysEnabled(): Boolean = fastKeysSessionEnabled

    // (NUS) Nordic UART defaults 
    val SERVICE_UUID: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    val CHAR_UUID:    UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
	val RX_UUID: UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")

	// Lazily create and cache a single BluetoothDeviceManager instance.
	//
	// NOTE: We guard access with a lock because this object can be touched
	// from different threads (startup vs. Settings UI vs. background flows).
    private val mgrLock = Any()
    private var mgr: BluetoothDeviceManager? = null
    private fun ensureMgr(): BluetoothDeviceManager =
        synchronized(mgrLock) { mgr ?: BluetoothDeviceManager(appCtx).also { mgr = it } }

    private var currentAddress: String? = null

	// Initialize BleHub with application Context.
	// MUST be called once from App.onCreate() before any other API is used.
    fun init(context: Context) {
        if (!::appCtx.isInitialized) appCtx = context.applicationContext
    }
	
	// Optional "current target" override used by writePassword() when the
	// app wants to direct traffic to a specific device instead of the
	// one stored in Preferences.	
    fun setTarget(address: String?) { currentAddress = address }

	//////////////////////////////////////////////////////////////////
	// Auto-connect helper used by app startup.
	//
	// Called with:
	//   useExternal : "Use external keyboard device" toggle state.
	//   address     : preferred dongle MAC (may be null).
	//
	// Behaviour:
	//  - If Settings UI is open (passwordPrompt != null), DO NOTHING.
	//    This avoids racing against the user while they configure things.
	//  - If external output is disabled or no address is configured,
	//    callback with a descriptive error.
	//  - Otherwise, run a short "connect + handshake" sequence WITHOUT
	//    prompting for a password. Failures here can auto-disable the toggle.
	//////////////////////////////////////////////////////////////////
    fun autoConnectIfEnabled(
        useExternal: Boolean,
        address: String?,
        onReady: ((Boolean, String?) -> Unit)? = null
    ) 
	{
		// If Settings is visible (provider set), don't run the silent path here.
		if (passwordPrompt != null) {
			onReady?.invoke(false, "Suppressed while in Settings")
			return
		}		
				
		setTarget(address)
        if (!useExternal || address.isNullOrBlank()) {
            onReady?.invoke(false, "Output device not enabled or not selected")
            return
        }
		
		// Startup policy: 1s timeout, 3 attempts, NO password prompt here
		connectAndFetchLayoutSimple(
			timeoutMs = 3500L,
			retries = 3,
			allowPrompt = false
		) { ok, err ->
			// If connect failed for whatever reason at startup, the per-case logic
			// below will already disable the toggle (we also return the error up).
			onReady?.invoke(ok, err)
		}
	
    }
	
	////////////////////////////////////////////////////////////////////
	// autoConnectFromPrefs
	//
	// Startup flow used when the app comes to foreground:
	//
	// 1. Read global toggle + "disabled by error" flag.
	//    - If user turned it OFF manually => respect and exit.
	//    - If it was auto-disabled by error => we are allowed to retry.
	//
	// 2. Build candidate list:
	//    - Primary device from prefs (if set)
	//    - PLUS all bonded devices that already have an APPKEY stored
	//      (see bondedProvisionedAddresses()).
	//
	// 3. Quick path:
	//    - Try a fast GATT-only connect to the primary.
	//    - If that works, run full binary handshake for primary only.
	//
	// 4. Fallback path:
	//    - Run a short RSSI scan over remaining candidates.
	//    - Sort by strongest signal first.
	//    - Try each in order with a 1.5s timeout (no password prompt).
	//
	// 5. If all candidates fail:
	//    - disable auto-connect and show a toast explaining why.
	//
	// This function is intentionally "aggressive" at startup, but never
	// pops UI prompts or password dialogs.
	////////////////////////////////////////////////////////////////////
    fun autoConnectFromPrefs(onReady: ((Boolean, String?) -> Unit)? = null) {
        val useExt = PreferencesUtil.useExternalKeyboardDevice(appCtx)
        val wasErrorOff = PreferencesUtil.wasOutputDeviceDisabledByError(appCtx)

		fun autoDone(ok: Boolean, msg: String?) {
			connectInProgress = false
			onReady?.invoke(ok, msg)
		}

        // If toggle is OFF and it was NOT auto-disabled by an error, respect user choice.
        if (!useExt && !wasErrorOff) {
            //onReady?.invoke(false, "Output device disabled in settings")
			autoDone(false, "Output device disabled in settings")
            return
        }

        // If Settings has a password prompt provider, back off and let manual flow win.
        if (passwordPrompt != null) {
            //onReady?.invoke(false, "Suppressed while in Settings")
			autoDone(false, "Suppressed while in Settings")
            return
        }

        val primary = PreferencesUtil.getOutputDeviceId(appCtx)
        val baseCandidates = LinkedHashSet<String>().apply {
            if (!primary.isNullOrBlank()) add(primary)
            // add all other bonded devices that already have an APPKEY
            bondedProvisionedAddresses().forEach { add(it) }
        }.toList()

        if (baseCandidates.isEmpty()) {
            logd("AUTO: no provisioned candidates (primary=$primary)")
            //onReady?.invoke(false, "No provisioned dongles found")
			autoDone(false, "No provisioned dongles found")
            return
        }

		// From this point on we are in an async connect/handshake flow.
		connectInProgress = true

		// Try to auto-connect using an RSSI pre-scan:
		//
		// - Scan for candidates for  aprox. 800ms.
		// - Order them by strongest RSSI first.
		// - Try each in sequence with a short connect + handshake.
		// - Only when ALL fail do we disable autoconnect.
        fun connectViaScan(candidatesBase: List<String>) {
            if (candidatesBase.isEmpty()) {
                logd("AUTO: no remaining candidates for RSSI scan")
                disableAutoConnect("No provisioned dongle is reachable.")
                //onReady?.invoke(false, "No dongle reachable")
				autoDone(false, "No dongle reachable")
                return
            }

            val mgr = ensureMgr()
            val targetSet = candidatesBase.toSet()

            // Short scan to see which remaining dongles are actually on-air
            mgr.scanForRssiOnce(targetSet, durationMs = 800L) { rssiMap ->
                val seenAddrs = rssiMap.keys.intersect(targetSet)

                val ordered: List<String> =
                    if (seenAddrs.isNotEmpty()) {
                        val visibleSorted = seenAddrs
                            .sortedByDescending { addr -> rssiMap[addr] ?: Int.MIN_VALUE }

                        // Try visible (strongest RSSI first), then any we didn't see at all
                        val out = mutableListOf<String>()
                        out.addAll(visibleSorted)
                        val unseen = candidatesBase.filterNot { seenAddrs.contains(it) }
                        out.addAll(unseen)
                        out.distinct()
                    } else {
                        // Scan saw nothing – fall back to original order
                        candidatesBase
                    }

                logd(
                    "AUTO: startup candidates after RSSI scan = " +
                        ordered.joinToString { addr ->
                            val rssi = rssiMap[addr]
                            if (rssi != null) "$addr(rssi=$rssi)" else addr
                        }
                )

                val it = ordered.iterator()

                fun tryNext() {
                    if (!it.hasNext()) {
                        // Nothing worked – now we finally disable autoconnect
                        logd("AUTO: all candidates failed after scan, disabling autoconnect")
                        disableAutoConnect("No provisioned dongle is reachable.")
                        //onReady?.invoke(false, "No dongle reachable")
						autoDone(false, "No dongle reachable")
                        return
                    }

                    val addr = it.next()
                    logd("AUTO: trying dongle $addr after scan")

                    // Make sure previous connection (if any) is fully dropped before next attempt
                    try { ensureMgr().disconnect() } catch (_: Throwable) {}

                    var used = false

                    connectAndFetchLayoutSimpleTo(
                        address = addr,
                        timeoutMs = 4000L,
                        retries = 2,
                        allowPrompt = false,
                        suppressAutoDisable = true   // do NOT kill autoconnect per-device
                    ) { ok, err ->
                        // Guard: ignore any second/late callback for this candidate
                        if (used) {
                            logd("AUTO: second callback for $addr ignored (ok=$ok err=$err)")
                            return@connectAndFetchLayoutSimpleTo
                        }
                        used = true

                        if (ok) {
                            logd("AUTO: connected successfully to $addr – making it active")

                            // Update prefs & current target
                            PreferencesUtil.setOutputDeviceId(appCtx, addr)
                            setTarget(addr)

                            // Try to store a friendly name if we can see it
                            try {
                                val btMgr = appCtx.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
                                val adapter = btMgr?.adapter
                                val devName = adapter?.getRemoteDevice(addr)?.name
                                if (!devName.isNullOrBlank()) {
                                    PreferencesUtil.setOutputDeviceName(appCtx, devName)
                                }
                            } catch (_: Throwable) {
                                // ignore – name is only cosmetic
                            }

                            // Keep the toggle ON on success
                            PreferencesUtil.setUseExternalKeyboardDevice(appCtx, true)
                            // also clear any "disabled by error" flag
                            PreferencesUtil.setOutputDeviceDisabledByError(appCtx, false)

                            //onReady?.invoke(true, null)
							autoDone(true, null)
							
                        } else {
                            logd("AUTO: dongle $addr failed: ${err ?: "unknown"} – trying next")
                            tryNext()
                        }
                    }
                }

                // Kick off scan-based attempts
                tryNext()
            }
        }

        // 1) Quick path: try to connect to the currently selected dongle BEFORE any scan.
		if (!primary.isNullOrBlank() && baseCandidates.contains(primary)) {
			logd("AUTO: attempting full connect+handshake to primary $primary before RSSI scan")

			// Make sure we start from a clean state once, here.
			try { ensureMgr().disconnect() } catch (_: Throwable) {}

			connectInProgress = true

			connectAndFetchLayoutSimpleTo(
				address = primary,
				timeoutMs = 5000L,          // total banner/handshake timeout
				retries = 1,
				allowPrompt = false,        // no UI at startup
				suppressAutoDisable = false,
				connectTimeoutMs = 3500L    // pass through to BluetoothDeviceManager.connect()
			) { ok, err ->
				if (ok) {
					logd("AUTO: connected successfully to primary $primary – making it active")

					PreferencesUtil.setOutputDeviceId(appCtx, primary)
					setTarget(primary)

					// Cosmetic: try to cache device name
					try {
						val btMgr = appCtx.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
						val adapter = btMgr?.adapter
						val devName = adapter?.getRemoteDevice(primary)?.name
						if (!devName.isNullOrBlank()) {
							PreferencesUtil.setOutputDeviceName(appCtx, devName)
						}
					} catch (_: Throwable) {
						// ignore – name is only cosmetic
					}

					PreferencesUtil.setUseExternalKeyboardDevice(appCtx, true)
					PreferencesUtil.setOutputDeviceDisabledByError(appCtx, false)

					autoDone(true, null)
				} else {
					logd("AUTO: primary $primary full connect+handshake failed: ${err ?: "unknown"} – falling back to scan of other dongles")

					val remaining = baseCandidates.filter { it != primary }
					connectViaScan(remaining)
				}
			}

			// Subsequent work happens via callbacks
			return
		}

        // 2) No primary set (or not provisioned): just do RSSI-based scan over all candidates.
        connectViaScan(baseCandidates)
    }

	////////////////////////////////////////////////////////////////////
	// Enumerate bonded BT devices and filter to those that already have
	// an APPKEY in BleAppSec.
	//
	// Used at startup to build the candidate list of "provisioned dongles"
	// that we are allowed to auto-connect to silently.
	////////////////////////////////////////////////////////////////////
    private fun bondedProvisionedAddresses(): List<String> {
        val btMgr = appCtx.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
            ?: run {
                logd("AUTO: no BluetoothManager from appCtx")
                return emptyList()
            }
        val adapter: BluetoothAdapter = btMgr.adapter ?: run {
            logd("AUTO: no BluetoothAdapter (BT off?)")
            return emptyList()
        }

        return try {
            val bonded = adapter.bondedDevices ?: emptySet()
            if (bonded.isEmpty()) {
                logd("AUTO: no bonded devices at all")
                return emptyList()
            }

            val out = bonded.mapNotNull { dev ->
                val addr = dev.address ?: return@mapNotNull null
                val hasKey = BleAppSec.getKey(appCtx, addr) != null
                logd("AUTO: bonded device $addr name='${dev.name}' hasKey=$hasKey")
                if (hasKey) addr else null
            }.distinct()

            logd("AUTO: bondedProvisionedAddresses -> ${out.joinToString()}")
            out
        } catch (ex: SecurityException) {
            logd("AUTO: SecurityException while reading bonded devices: ${ex.message}")
            emptyList()
        }
    }
	
	////////////////////////////////////////////////////////////////////	
	// Public "simple connect" wrapper used by:
	//
	//  - Settings > Output device (manual connect/verify)
	//  - connectFromSettings()
	//  - some internal flows that want a one-shot connect.
	//
	// It:
	//   1) Connects to the selected device from prefs.
	//   2) Ensures notifications are enabled.
	//   3) Waits for either a plaintext INFO banner (unprovisioned)
	//      OR a B0 frame (provisioned).
	//   4) For B0 => run binary handshake (MTLS).
	//      For plaintext => start provisioning flow if allowed.
	////////////////////////////////////////////////////////////////////
    fun connectAndFetchLayoutSimple(
        timeoutMs: Long = 3500L,
        retries: Int = 2,
        allowPrompt: Boolean = false,
        onDone: ((Boolean, String?) -> Unit)? = null
    ) {
        val addr = PreferencesUtil.getOutputDeviceId(appCtx)
        if (addr.isNullOrBlank()) { onDone?.invoke(false, "No device selected"); return }
        connectAndFetchLayoutSimpleTo(
            address = addr,
            timeoutMs = timeoutMs,
            retries = retries,
            allowPrompt = allowPrompt,
            suppressAutoDisable = false,
			connectTimeoutMs = null,      // no watchdog for the manual coonnect path
            onDone = onDone
        )
    }

	////////////////////////////////////////////////////////////////////
	// Core "connect + detect + handshake" engine.
	//
	// address          : target MAC address.
	// timeoutMs        : total wait for banner/B0.
	// retries          : how many times to retry the entire connection.
	// allowPrompt      : whether we are allowed to show password UI.
	// suppressAutoDisable:
	//   - false => may flip off autoconnect on repeated failures.
	//   - true  => respect "auto-connect enabled" even if this device fails.
	//
	// Flow per attempt:
	//   1) Connect via BluetoothDeviceManager.connect().
	//   2) Ensure CCCD/notifications are ON.
	//   3) waitForInfoBannerOrB0():
	//        - B0  => doBinaryHandshakeFromB0()
	//        - plain => clear any stale APPKEY and optionally run
	//                  provisioning + reconnect.
	//        - timeout => retry or fail.
	//
	//////////////////////////////////////////////////////////////////// 
    private fun connectAndFetchLayoutSimpleTo(
        address: String,
        timeoutMs: Long,
        retries: Int,
        allowPrompt: Boolean,
        suppressAutoDisable: Boolean,
		connectTimeoutMs: Long? = null,
        onDone: ((Boolean, String?) -> Unit)?
    ) {
        fun attempt(left: Int) {
			logd("CONNECT: attempt left=$left to $address (connectTimeoutMs=$connectTimeoutMs)")
            ensureMgr().connect(address, connectTimeoutMs) { ok, err ->
				logd("CONNECT: connect() callback for $address ok=$ok err=$err")
                if (!ok) {
                    if (left > 0) {
						logd("CONNECT: transport failed, retrying (left=${left - 1}) err=$err")
                        attempt(left - 1)
                    } else {
                        // Transport failed after retries
                        if (!allowPrompt && !suppressAutoDisable) {
                            // Startup/autoconnect policy: flip off autoconnect
                            disableAutoConnect("Output device is not responding.")
                        }
                        onDone?.invoke(false, err)
                    }
                    return@connect
                }

				logd("CONNECT: GATT OK for $address, enabling notifications…")
                ensureNotificationsEnabled(address) { _, _ ->

                    val hasAppKey = BleAppSec.getKey(appCtx, address) != null
                    val bannerTimeout = if (hasAppKey) 5000L else timeoutMs

                    // wait for EITHER plaintext INFO (unprovisioned) OR B0 (provisioned)
                    waitForInfoBannerOrB0(totalTimeoutMs = bannerTimeout) { plain, b0 ->
                        when {
                            b0 != null -> {
                                logd("CONNECT: got B0 - binary handshake")
                                doBinaryHandshakeFromB0(address, b0) { okH, errH ->
                                    _connected.postValue(okH)
									
									// used befor in single provisioning mode
                                    //if (!okH && errH == "APPKEY missing" && !suppressAutoDisable) {
                                    //    disableAutoConnect("Device requires pairing reset (APPKEY missing).")
                                    //}
                                    //onDone?.invoke(okH, errH)
									
                                    if (okH) {
                                        onDone?.invoke(true, null)
                                        return@doBinaryHandshakeFromB0
                                    }

									// 1) Device has APPKEY, this app instance does not => "APPKEY missing".
									//    - From Settings (allowPrompt=true): run GET_APPKEY flow + reconnect.
									//    - From auto-connect: keep old behaviour (may disable autoconnect).
									if (errH == "APPKEY missing") {
										if (allowPrompt) {
											logd("CONNECT: APPKEY missing – provisioning from Settings…")

											// Ask password, GET_APPKEY, store in BleAppSec
											requestAndStoreAppKeyWithPrompt(address, forcePrompt = true) { okKey, errKey ->
												if (!okKey) {
													onDone?.invoke(false, errKey ?: "APPKEY provisioning failed")
													return@requestAndStoreAppKeyWithPrompt
												}

												// After provisioning, reconnect and redo B0/B1/B2
												reconnectExpectingB0AndHandshake { okH2, errH2 ->
													_connected.postValue(okH2)
													onDone?.invoke(okH2, errH2)
												}
											}
										} else if (!suppressAutoDisable) {
											// Startup/autoconnect path: keep your old policy
											disableAutoConnect("Device requires pairing reset (APPKEY missing).")
											onDone?.invoke(false, errH)
										} else {
											onDone?.invoke(false, errH)
										}
										return@doBinaryHandshakeFromB0
									}

                                    // 2) wrong APPKEY (BADMAC) – only auto-recover when prompted flow is allowed
                                    if (allowPrompt && errH?.contains("BADMAC") == true) {
                                        logd("CONNECT: handshake BADMAC – clearing cached APPKEY and re-provisioning…")
                                        BleAppSec.clearKey(appCtx, address)

                                        // Password prompt + GET_APPKEY + store in BleAppSec
                                        requestAndStoreAppKeyWithPrompt(address, forcePrompt = true) { okKey, errKey ->
                                            if (!okKey) {
                                                onDone?.invoke(false, errKey ?: "APPKEY failed after BADMAC")
                                                return@requestAndStoreAppKeyWithPrompt
                                            }

                                            // After provisioning, drop and re-establish the connection expecting B0 again
                                            reconnectExpectingB0AndHandshake { okH2, errH2 ->
                                                _connected.postValue(okH2)
                                                onDone?.invoke(okH2, errH2)
                                            }
                                        }
                                        return@doBinaryHandshakeFromB0
                                    }

                                    // Fallback: just propagate whatever error we got
                                    onDone?.invoke(false, errH)									
                                }
                            }
                            plain != null -> {
                                val (layout, proto, fw) = plain
                                logd("CONNECT: PLAINTEXT INFO (unprovisioned): LAYOUT=$layout PROTO=$proto FW=$fw")
                                logd("DEBUG: allowPrompt=$allowPrompt, appKeyExists=" + (BleAppSec.getKey(appCtx, address) != null))

                                // device says it's unprovisioned - drop any stale cached key
                                BleAppSec.clearKey(appCtx, address)
                                logd("DEBUG: cleared stale APPKEY for $address (saw plaintext 'unprovisioned' banner)")

                                // Store layout immediately
                                PreferencesUtil.setKeyboardLayout(appCtx, layout)

                                if (!allowPrompt) {
                                    // Startup path: do NOT prompt here
                                    if (!suppressAutoDisable) {
                                        disableAutoConnect("Failed to connect: device is unprovisioned.")
                                    }
                                    onDone?.invoke(false, "unprovisioned")
                                    return@waitForInfoBannerOrB0
                                }

                                logd("DEBUG: calling requestAndStoreAppKeyWithPrompt() ...")
                                // Prompt for password, GET_APPKEY, reconnect, expect B0, handshake
                                requestAndStoreAppKeyWithPrompt(address, forcePrompt = true) { okKey, errKey ->
                                    if (!okKey) {
                                        onDone?.invoke(false, errKey ?: "APPKEY failed")
                                        return@requestAndStoreAppKeyWithPrompt
                                    }
                                    reconnectExpectingB0AndHandshake { okH, errH ->
                                        _connected.postValue(okH)
                                        onDone?.invoke(okH, errH)
                                    }
                                }
                            }
                            else -> {
                                // no banner/B0 within timeout
                                if (left > 0) {
                                    ensureMgr().disconnect()
                                    attempt(left - 1)
                                } else {
                                    if (!allowPrompt && !suppressAutoDisable) {
                                        disableAutoConnect("Output device is not responding.")
                                    }
                                    onDone?.invoke(false, "No banner/B0")
                                }
                            }
                        }
                    }
                }
            }
        }
        attempt(retries)
    }

    fun disconnect() {
        ensureMgr().disconnect()
        _connected.postValue(false)
        // Drop MTLS + fast-key state on explicit disconnect
        mtls = null
        fastKeysSessionEnabled = false		
    }	
	
	////////////////////////////////////////////////////////////////////
	// Binary framing:
	//   [OP u8][LEN u16 LE][PAYLOAD...]
	//
	// Framer accumulates arbitrary BLE notification chunks and
	// extracts complete frames while being robust against:
	//
	//   - noisy ASCII banners mixed with binary.
	//   - split frames (header / payload arriving separately).
	//   - bogus LEN values (sanity-capped via MAX_LEN).
	//
	// It also attempts to "resync" by skipping bytes until a plausible
	// header is found.
	//
	// NOTE: All app-level binary ops (B0/B1/B2/B3, A0..A3, C0..C4, D0/D1)
	//       go through this framing layer.
	////////////////////////////////////////////////////////////////////
	private fun u16le(x: Int) = byteArrayOf((x and 0xFF).toByte(), ((x ushr 8) and 0xFF).toByte())
	private fun rdU16le(b: ByteArray, o: Int) = (b[o].toInt() and 0xFF) or ((b[o+1].toInt() and 0xFF) shl 8)
	private data class Frame(val op: Int, val payload: ByteArray)

	private class Framer {
		private val buf = java.io.ByteArrayOutputStream()

		fun push(bytes: ByteArray): List<Frame> {
			// Log every chunk
			logd( "RX:chunk n=${bytes.size} head=${bytes.copyOfRange(0, minOf(16, bytes.size)).toHex()}")
			buf.write(bytes)

			val out = ArrayList<Frame>()
			var ba = buf.toByteArray()
			var i = 0
			val MAX_LEN = 1024  // sanity cap to reject bogus LENs from stray ASCII

			fun canReadHeader(pos: Int): Boolean = (pos + 3) <= ba.size
			fun plausibleFrameAt(pos: Int): Boolean {
				if (!canReadHeader(pos)) return false
				val op = ba[pos].toInt() and 0xFF
				val len = rdU16le(ba, pos + 1)
				// We accept any op, but len must be sane and fully available
				if (len < 0 || len > MAX_LEN) return false
				return (pos + 3 + len) <= ba.size
			}

			// RESYNC: skip garbage until a plausible [OP|LEN] fits in buffer
			while (i < ba.size && !plausibleFrameAt(i)) {
				i++
			}

			// Parse all contiguous frames
			while (plausibleFrameAt(i)) {
				val op = ba[i].toInt() and 0xFF
				val len = rdU16le(ba, i + 1)
				val pay = ba.copyOfRange(i + 3, i + 3 + len)
				out.add(Frame(op, pay))
				logd( String.format("RX:frame op=0x%02X len=%d head=%s",
					op, pay.size, pay.copyOfRange(0, minOf(16, pay.size)).toHex()))
				i += 3 + len
				// After consuming a frame, there may be banner junk again; try to resync
				while (i < ba.size && !plausibleFrameAt(i)) i++
			}

			// Keep any trailing partial data (or junk prior to a partial header) in the buffer
			if (i > 0) {
				val rest = ba.copyOfRange(i, ba.size)
				buf.reset()
				buf.write(rest)
			}
			return out
		}
	}


	// === Binary MTLS helpers ===
	private fun beInt(i: Int) = java.nio.ByteBuffer.allocate(4).order(java.nio.ByteOrder.BIG_ENDIAN).putInt(i).array()
	private fun leInt(i: Int) = java.nio.ByteBuffer.allocate(4).order(java.nio.ByteOrder.LITTLE_ENDIAN).putInt(i).array()
	private fun beShort(s: Int) = java.nio.ByteBuffer.allocate(2).order(java.nio.ByteOrder.BIG_ENDIAN).putShort(s.toShort()).array()
	private fun leShort(s: Int) = java.nio.ByteBuffer.allocate(2).order(java.nio.ByteOrder.LITTLE_ENDIAN).putShort(s.toShort()).array()

	// Put near hmacSha256() helpers
	private fun pbkdf2Sha256_bytes(passBytes: ByteArray, salt: ByteArray, iters: Int, dkLen: Int = 32): ByteArray {
		val skf = javax.crypto.SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256")
		val spec = javax.crypto.spec.PBEKeySpec(
			// Convert bytes -> ISO-8859-1 chars so we control encoding => bytes round-trip 1:1
			passBytes.toString(Charsets.ISO_8859_1).toCharArray(),
			salt, iters, dkLen * 8
		)
		return skf.generateSecret(spec).encoded
	}

	// Same derivation but with "common firmware" normalizations:
	//  - NFKC to collapse visually identical sequences
	//  - trim() to drop accidental leading/trailing whitespace/newlines
	private fun normalizePasswordForFirmware(pwChars: CharArray): ByteArray {
		val s0 = String(pwChars)
		val s1 = java.text.Normalizer.normalize(s0, java.text.Normalizer.Form.NFKC)
		val s2 = s1.trim() 
		return s2.toByteArray(Charsets.UTF_8)
	}


	private fun hmacSha256(key: ByteArray, data: ByteArray): ByteArray {
		val mac = javax.crypto.Mac.getInstance("HmacSHA256")
		mac.init(javax.crypto.spec.SecretKeySpec(key, "HmacSHA256"))
		return mac.doFinal(data)
	}
	private fun hmac16(key: ByteArray, data: ByteArray) = hmacSha256(key, data).copyOf(16)

	private fun aesCtrEnc(key: ByteArray, iv: ByteArray, plain: ByteArray): ByteArray {
		val c = javax.crypto.Cipher.getInstance("AES/CTR/NoPadding")
		c.init(javax.crypto.Cipher.ENCRYPT_MODE, javax.crypto.spec.SecretKeySpec(key, "AES"), javax.crypto.spec.IvParameterSpec(iv))
		return c.doFinal(plain)
	}

	////////////////////////////////////////////////////////////////////
	// Deterministic IV derivation for AES-CTR per MTLS frame.
	//
	// IV = HMAC(sessKey, "IV"||0x00||sid||dir||seq)[0..15]
	//
	// dir = 'C' for client-to-server, 'S' for server-to-client.
	// sid = session id (32-bit).
	// seq = 16-bit sequence number.
	////////////////////////////////////////////////////////////////////
	private fun mtlsIv(key: ByteArray, sid: Int, dir: Char, seq: Int): ByteArray {
		val ivMsg = java.io.ByteArrayOutputStream().apply {
			write("IV".toByteArray()); write(byteArrayOf(0))
			write(beInt(sid)); write(byteArrayOf(dir.code.toByte())); write(beShort(seq))
		}.toByteArray()
		return hmacSha256(key, ivMsg).copyOf(16)
	}

	////////////////////////////////////////////////////////////////////
	// wrapB3
	//
	// Wrap inner app frame [OP|LEN|PAYLOAD] into an encrypted B3 frame:
	//
	//   1) Build IV from (sid, 'C', seq).
	//   2) AES-CTR encrypt inner frame with sessKey.
	//   3) MAC = HMAC(sessKey, "ENCM"||sid||'C'||seq||cipher)[0..16].
	//   4) Payload = [seq_le16][cipherLen_le16][cipher][mac16].
	//   5) Outer frame = [0xB3][LEN_le16][Payload].
	//
	// seqOut is incremented modulo 2^16 for each sent frame.
	////////////////////////////////////////////////////////////////////
	private fun wrapB3(plainAppFrame: ByteArray): ByteArray {
		val st = mtls ?: error("no mtls")
		val key = st.sessKey ?: error("no sessKey")
		val seq = st.seqOut and 0xFFFF
		
		val iv  = mtlsIv(key, st.sid, 'C', seq)
		
		val enc = aesCtrEnc(key, iv, plainAppFrame)

		val macData = java.io.ByteArrayOutputStream().apply {
			write("ENCM".toByteArray()); write(beInt(st.sid)); write(byteArrayOf('C'.code.toByte())); write(beShort(seq)); write(enc)
		}.toByteArray()
		val mac = hmac16(key, macData)

		val pay = java.io.ByteArrayOutputStream().apply {
			write(leShort(seq)); write(leShort(enc.size)); write(enc); write(mac)
		}.toByteArray()
		st.seqOut = (seq + 1) and 0xFFFF
		return byteArrayOf(0xB3.toByte()) + u16le(pay.size) + pay
	}
	
	////////////////////////////////////////////////////////////////////	
	// Low-level send of a single framed message over the NUS TX
	// characteristic. Used for handshake/control ops:
	//
	//   op      : top-level operation id (e.g. 0xB1, 0xB2, 0xA0...).
	//   payload : frame payload (NOT including [OP|LEN]).
	//   addressOverride : optional explicit MAC (e.g. during handshake)
	//                     otherwise falls back to prefs device.
	////////////////////////////////////////////////////////////////////
	private fun sendRawFrame(
		op: Int,
		payload: ByteArray,
		addressOverride: String? = null,
		cb: (Boolean, String?) -> Unit
	) {
		val addr = addressOverride ?: PreferencesUtil.getOutputDeviceId(appCtx)
		if (addr.isNullOrBlank()) {
			cb(false, "No device")
			return
		}
		val frame = byteArrayOf(op.toByte()) + u16le(payload.size) + payload
		ensureMgr().writeOrConnect(addr, SERVICE_UUID, CHAR_UUID, frame, onResult = cb)
	}

	////////////////////////////////////////////////////////////////////
	// Wait for the next Frame matching [predicate] using streaming.
	//
	// - Starts a fresh notification stream.
	// - Feeds all chunks into Framer.
	// - Stops as soon as a matching Frame is seen OR timeout hits.
	// - Hard timeout is independent from BLE notify traffic.
	////////////////////////////////////////////////////////////////////
	private fun awaitNextFrame(
		timeoutMs: Long,
		predicate: ((Frame) -> Boolean)? = null,
		onResult: (Frame?) -> Unit
	) {
		val framer = Framer()
		var done = false
		val handler = android.os.Handler(android.os.Looper.getMainLooper())

		fun finish(f: Frame?) {
			if (done) return
			done = true
			ensureMgr().stopNotificationStream()
			onResult(f)
		}

		// Hard timeout guard independent of incoming notifications
		val to = Runnable {
			logd( "RX: awaitNextFrame TIMEOUT (${timeoutMs}ms) with no match")
			finish(null)
		}
		handler.postDelayed(to, timeoutMs)

		logd( "RX: awaitNextFrame — stream START")
		ensureMgr().startNotificationStream { chunk ->
			if (done) return@startNotificationStream
			val frames = framer.push(chunk)
			val hit = frames.firstOrNull { predicate?.invoke(it) ?: true }
			if (hit != null) 
			{
				logd( String.format("RX: awaitNextFrame — match op=0x%02X len=%d", hit.op, hit.payload.size))
				handler.removeCallbacks(to)
				finish(hit)
			}
		}
	}

	////////////////////////////////////////////////////////////////////
	// Binary APPKEY provisioning (A0/A2/A3/A1).
	//
	// Sequence:
	//
	//   A0 -> device            : "GET_APPKEY" (no payload)
	//   A2 <- device (op=0xA2)  : [salt16][iters4 LE][chal16]
	//   A3 -> device (op=0xA3)  : HMAC(PBKDF2(pw,salt,iters), "APPKEY"||chal)
	//   A1 <- device (op=0xA1)  : APPKEY (either raw 32 bytes or
	//                             wrapped 48 bytes = cipher32||mac16)
	//
	// Additional behaviour:
	//   - If no password provided, ask passwordPrompt UI provider.
	//   - First try "raw" UTF-8 password.
	//   - If proof rejected, retry with NFKC-normalized + trimmed password.
	//   - Supports wrapped APPKEYs (AES-CTR + HMAC integrity).
	//
	// On success:
	//   - APPKEY is decrypted (if needed) and stored via BleAppSec.putKey().
	////////////////////////////////////////////////////////////////////
	private fun ensureAppKeyCachedBinary(
		address: String,
		timeoutMs: Long = 6000L,
		forceFetch: Boolean = false,
		userPassword: CharArray? = null,
		onDone: (Boolean, String?) -> Unit
	) {
		if (!forceFetch) {
			BleAppSec.getKey(appCtx, address)?.let { onDone(true, null); return }
		}

		// If we don't have a password, ask the UI provider here.
		if (userPassword == null || userPassword.isEmpty()) {
			val ask = passwordPrompt
			if (ask == null) {
				loge( "APPKEY: no password prompt provider set")
				onDone(false, "No password UI")
				return
			}
			// ensure main ui post
			mainHandler.post {
				ask(appCtx) { chars ->
					if (chars == null || chars.isEmpty()) {
						onDone(false, "Password cancelled")
					} else {
						val pwLocal = chars.copyOf()
						java.util.Arrays.fill(chars, '\u0000')

						ensureAppKeyCachedBinary(address, timeoutMs, true, pwLocal) { ok, err ->
							java.util.Arrays.fill(pwLocal, '\u0000')
							onDone(ok, err)
						}
					}
				}
			}

			return
		}
			
		// debug
		logd( "APPKEY: sending A0 (GET_APPKEY) — expecting A2 within ${timeoutMs}ms")
		ensureMgr().stopNotificationStream()   // NEW: nuke any previous stream before we start a new await
		
		
		// Send A0 (GET_APPKEY)
		////////////////////////////////////////////////////////////////
		sendRawFrame(0xA0, ByteArray(0)) { ok, err ->
			if (!ok) { onDone(false, err); return@sendRawFrame }

			awaitNextFrame(timeoutMs, predicate = { it.op == 0xA2 || it.op == 0xFF }) { f ->
			
                if (f == null) {
                    onDone(false, "No CHALLENGE")
                    return@awaitNextFrame
                }
                if (f.op == 0xFF) {
                    onDone(false, mapAppKeyErrorFromDevice(f.payload))
                    return@awaitNextFrame
                }

				logd( String.format("APPKEY: got A2 op=0x%02X payLen=%d", f.op, f.payload.size))						
				
				val pay = f.payload
							
				if (pay.size != (16 + 4 + 16)) { onDone(false, "Bad CHALLENGE"); return@awaitNextFrame }
				val salt = pay.copyOfRange(0, 16)
				val iters = java.nio.ByteBuffer.wrap(pay.copyOfRange(16, 20))
					.order(java.nio.ByteOrder.LITTLE_ENDIAN).int
				val chal  = pay.copyOfRange(20, 36)
				//logd( "APPKEY: challenge iters=$iters")
				// debug
				logd( "APPKEY: challenge salt=${salt.toHex()} iters=$iters chal=${chal.toHex()}")

				val passIn = userPassword
				if (passIn == null || passIn.isEmpty()) {
					onDone(false, "Password required"); return@awaitNextFrame
				}
				val passChars = passIn.copyOf() // defensive copy for this scope

				// --- Build message = "APPKEY" || chal16
				val msg = java.io.ByteArrayOutputStream().apply {
					write("APPKEY".toByteArray()); write(chal)
				}.toByteArray()

				// --- Derive password bytes (two candidates)
				val passRawBytes  = String(passChars).toByteArray(Charsets.UTF_8)
				val passNormBytes = normalizePasswordForFirmware(passChars)

				// --- Debug the exact inputs both sides must agree on
				//logd( "APPKEY: msg('APPKEY'||chal16)=${msg.toHex()}")
				//logd( "APPKEY: passRawBytes[0..min(16)]=${passRawBytes.take(16).toByteArray().toHex()}")

				// --- PBKDF2 + HMAC (RAW)
				val verif_raw = pbkdf2Sha256_bytes(passRawBytes, salt, iters)
				logd( "APPKEY: verif_raw[0..7]=${verif_raw.copyOfRange(0,8).toHex()}")
				val mac_raw = hmacSha256(verif_raw, msg)
				logd( "APPKEY: mac_raw=${mac_raw.toHex().take(8)}…")

				// --- PBKDF2 + HMAC (NORMALIZED)
				val verif_norm = pbkdf2Sha256_bytes(passNormBytes, salt, iters)
				logd( "APPKEY: verif_norm[0..7]=${verif_norm.copyOfRange(0,8).toHex()}")
				val mac_norm = hmacSha256(verif_norm, msg)
				logd( "APPKEY: mac_norm=${mac_norm.toHex().take(8)}…")

				// Try RAW first
				fun sendProof(mac: ByteArray, then: (Boolean, Frame?) -> Unit) {
					sendRawFrame(0xA3, mac) { ok2, err2 ->
						if (!ok2) { onDone(false, err2); return@sendRawFrame }
						awaitNextFrame(timeoutMs, predicate = { it.op == 0xA1 || it.op == 0xFF }) { f2 ->
							then(f2 != null && f2.op == 0xA1, f2)
						}
					}
				}

				// Unwrap A1 payload when device sends encrypted APPKEY:
				//
				//  - legacy:   32-byte raw APPKEY
				//  - wrapped:  48 bytes = cipher32 || mac16
				//
				// Wrap key = HMAC(verif, "AKWRAP" || chal)
				// MAC       = HMAC(wrapKey, "AKMAC" || chal || cipher)[0..15]
				// IV        = HMAC(verif, "AKIV"   || chal)[0..15]
				//
				// If MAC check passes, decrypt via AES-CTR to recover APPKEY.
				fun tryUnwrapA1Maybe(
					verif: ByteArray,    // 32 bytes verifier used for HMAC on the device side (verif_raw or verif_norm)
					chal: ByteArray,     // 16-byte challenge used in this exchange
					payload: ByteArray
				): ByteArray? {
					// legacy raw APPKEY: 32 bytes
					if (payload.size == 32) return payload

					// wrapped form: 48 bytes = cipher32 || mac16
					if (payload.size == 48) {
						val cipher = payload.copyOfRange(0, 32)
						val macIn  = payload.copyOfRange(32, 48)

						// wrapKey = HMAC(verif, "AKWRAP" || chal)
						val wrapMsg = java.io.ByteArrayOutputStream().apply {
							write("AKWRAP".toByteArray()); write(chal)
						}.toByteArray()
						val wrapKey = hmacSha256(verif, wrapMsg)

						// macExp = HMAC(wrapKey, "AKMAC" || chal || cipher)[0..15]
						val macMsg = java.io.ByteArrayOutputStream().apply {
							write("AKMAC".toByteArray()); write(chal); write(cipher)
						}.toByteArray()
						val macExpFull = hmacSha256(wrapKey, macMsg)
						val macExp = macExpFull.copyOfRange(0, 16)
						if (!macExp.contentEquals(macIn)) {
							loge( "APPKEY: wrapped A1 MAC mismatch")
							return null
						}

						// IV = HMAC(verif, "AKIV" || chal)[0..15]
						val ivMsg = java.io.ByteArrayOutputStream().apply {
							write("AKIV".toByteArray()); write(chal)
						}.toByteArray()
						val ivFull = hmacSha256(verif, ivMsg)
						val iv16 = ivFull.copyOfRange(0, 16)

						// decrypt
						val plain = aesCtrEnc(wrapKey, iv16, cipher)
						if (plain.size != 32) {
							loge( "APPKEY: decrypted size != 32")
							return null
						}
						return plain
					}
					// unexpected size
					loge( "APPKEY: unexpected A1 payload len=${payload.size}")
					return null
				}

				// sendProof callback - with retry on bad proof
				sendProof(mac_raw) { okRaw, f2 ->
					if (okRaw && f2 != null && f2.op == 0xA1) {
						val payload = f2.payload
						val maybeKey = tryUnwrapA1Maybe(verif_raw, chal, payload)
						if (maybeKey != null && maybeKey.size == 32) {
							BleAppSec.putKey(appCtx, address, maybeKey)
							
							// DEBUG: verify we can read it back immediately
							//val roundtrip = BleAppSec.getKey(appCtx, address)
							//Log.d("BleHub", "APPKEY roundtrip after put: len=${roundtrip?.size ?: -1}")
							
							java.util.Arrays.fill(passChars, '\u0000')
							onDone(true, null)
							return@sendProof
						}
						// continue to normalized retry
					}

					// DEBUG: if firmware explicitly sent an error (0xFF), surface it instead of blindly retrying
					if (f2 != null && f2.op == 0xFF) {
						val errMsg = mapAppKeyErrorFromDevice(f2.payload)
						java.util.Arrays.fill(passChars, '\u0000')
						onDone(false, errMsg)
						return@sendProof
					}

					// RAW rejected or unwrap failed — retry with normalized/trimmed
					logd( "APPKEY: RAW proof rejected — retrying with normalized/trimmed…")

					// Re-issue A0 to get a fresh challenge for the normalized path
					sendRawFrame(0xA0, ByteArray(0)) { okR, errR ->
						if (!okR) { onDone(false, errR); return@sendRawFrame }

						awaitNextFrame(timeoutMs, predicate = { it.op == 0xA2 || it.op == 0xFF }) { fR ->
							//if (fR == null || fR.op == 0xFF) { onDone(false, "No CHALLENGE (retry)"); return@awaitNextFrame }
                            if (fR == null) {
                                onDone(false, "No CHALLENGE (retry)")
                                return@awaitNextFrame
                            }
                            if (fR.op == 0xFF) {
                                onDone(false, mapAppKeyErrorFromDevice(fR.payload))
                                return@awaitNextFrame
                            }							

							val payR = fR.payload
							if (payR.size != 36) { onDone(false, "Bad CHALLENGE (retry)"); return@awaitNextFrame }

							val saltR  = payR.copyOfRange(0, 16)
							val itersR = java.nio.ByteBuffer.wrap(payR.copyOfRange(16, 20))
								.order(java.nio.ByteOrder.LITTLE_ENDIAN).int
							val chalR  = payR.copyOfRange(20, 36)

							val msgR = java.io.ByteArrayOutputStream().apply {
								write("APPKEY".toByteArray()); write(chalR)
							}.toByteArray()

							val verifR = pbkdf2Sha256_bytes(passNormBytes, saltR, itersR)
							val macR   = hmacSha256(verifR, msgR)
							logd( "APPKEY: retry mac_norm=${macR.toHex().take(8)}…")

							sendProof(macR) { okNorm, f3 ->
								if (!okNorm || f3 == null || f3.op != 0xA1) {
									// if we got an error frame here, surface its message
									if (f3 != null && f3.op == 0xFF) {
										val errMsg = mapAppKeyErrorFromDevice(f3.payload)
										java.util.Arrays.fill(passChars, '\u0000')
										onDone(false, errMsg)
									} else {
										java.util.Arrays.fill(passChars, '\u0000')
										onDone(false, "Proof rejected")
									}
									return@sendProof
								}

								val payloadN = f3.payload
								val maybeKeyN = tryUnwrapA1Maybe(verifR, chalR, payloadN)
								if (maybeKeyN == null || maybeKeyN.size != 32) {
									java.util.Arrays.fill(passChars, '\u0000')
									onDone(false, "Proof rejected")
									return@sendProof
								}

								BleAppSec.putKey(appCtx, address, maybeKeyN)
								java.util.Arrays.fill(passChars, '\u0000')
								onDone(true, null)
							}
						}
					}
				}

			}
		}
	}

	///////////////////////////////////////////
	private fun genP256(): java.security.KeyPair 
	{ 
		return java.security.KeyPairGenerator.getInstance("EC").apply{
					initialize(java.security.spec.ECGenParameterSpec("secp256r1"))
				}.generateKeyPair() 
	}
	
	private fun pubUncompressed(pub: java.security.PublicKey): ByteArray { /* already present */ 
		val x509 = pub.encoded
		return x509.copyOfRange(x509.size - 65, x509.size)
	}
	
	private fun ecdh(priv: java.security.PrivateKey, srvPub65: ByteArray): ByteArray { /* already present */ 
		val x = java.math.BigInteger(1, srvPub65.copyOfRange(1,33))
		val y = java.math.BigInteger(1, srvPub65.copyOfRange(33,65))
		val params = java.security.AlgorithmParameters.getInstance("EC").apply {
			init(java.security.spec.ECGenParameterSpec("secp256r1"))
		}.getParameterSpec(java.security.spec.ECParameterSpec::class.java)
		val kf = java.security.KeyFactory.getInstance("EC")
		val pubSpec = java.security.spec.ECPublicKeySpec(java.security.spec.ECPoint(x,y), params)
		val srvPub = kf.generatePublic(pubSpec)
		val ka = javax.crypto.KeyAgreement.getInstance("ECDH")
		ka.init(priv); ka.doPhase(srvPub, true)
		return ka.generateSecret()
	}
	
	private fun hkdfSha256(salt: ByteArray, ikm: ByteArray, info: ByteArray): ByteArray { /* present */ 
		val prk = hmacSha256(salt, ikm)
		val mac = javax.crypto.Mac.getInstance("HmacSHA256")
		mac.init(javax.crypto.spec.SecretKeySpec(prk, "HmacSHA256"))
		mac.update(info); mac.update(byteArrayOf(0x01))
		return mac.doFinal().copyOf(32)
	}

    // Map low-level 0xFF APPKEY errors into human-readable text.
    private fun mapAppKeyErrorFromDevice(payload: ByteArray?): String {
        if (payload == null || payload.isEmpty()) return "Device error"

        val raw = payload.toString(Charsets.UTF_8)

        return when {
            raw.startsWith("already set", ignoreCase = true) ->
                "Cannot get APPKEY: dongle is already provisioned in single-app mode. " +
                "To pair a new app you need to reset the dongle to factory defaults."

            raw.startsWith("KDF missing", ignoreCase = true) ->
                "Dongle is missing setup password parameters. Run the Wi-Fi/setup portal again or reset to factory defaults."

            raw.startsWith("GET_APPKEY blocked", ignoreCase = true) ->
                "Too many failed APPKEY attempts on this boot. Power-cycle the dongle and try again."

            raw.startsWith("bad proof", ignoreCase = true) ->
                "Wrong setup password. Please check the password and try again."

            raw.startsWith("HMAC fail", ignoreCase = true) ->
                "Internal error while verifying APPKEY. Try again or reset the dongle."

            raw.startsWith("no pending chal", ignoreCase = true) ->
                "APPKEY flow is out of sync. Please retry."

            raw.startsWith("send fail", ignoreCase = true) ->
                "Failed to send the encrypted APPKEY to the app. Check the BLE link and try again."

            else -> "Device error: $raw"
        }
    }

    // Map 0xFF during B1/B2 handshake into a more specific error.
    private fun mapHandshakeErrorFromDevice(payload: ByteArray?): String {
        if (payload == null || payload.isEmpty()) return "Handshake failed"

        val raw = payload.toString(Charsets.UTF_8)

        return when {
            // We keep the keyword BADMAC in the string so we can detect it later.
            raw.startsWith("BADMAC", ignoreCase = true) ->
                "Handshake failed (APPKEY BADMAC)"

            raw.startsWith("DERIVE", ignoreCase = true) ->
                "Handshake failed (ECDH/HKDF error)"

            else -> "Handshake device error: $raw"
        }
    }

	///////////////////////////////////////////////////
	fun connectAndEstablishSecure(
		onDone: ((Boolean, String?) -> Unit)? = null
	) {
		if (connectInProgress) {
			onDone?.invoke(false, "Connecting to dongle, please wait…")
			return
		}

		connectInProgress = true

		fun done(ok: Boolean, msg: String?) {
			connectInProgress = false
			onDone?.invoke(ok, msg)
		}

		mtls = null
		val addr = PreferencesUtil.getOutputDeviceId(appCtx)
		if (addr.isNullOrBlank()) { done(false, "No device selected"); return }

		ensureMgr().connect(addr) { ok, err ->
			if (!ok) { done(false, err); return@connect }
			ensureNotificationsEnabled(addr) { _, _ ->
				waitForInfoBannerOrB0(totalTimeoutMs = 5000L) { plain, b0 ->
					when {
						b0 != null -> {
							doBinaryHandshakeFromB0(addr, b0) { okH, errH ->
								_connected.postValue(okH)
								done(okH, errH)
							}
						}
						plain != null -> done(false, "unprovisioned")
						else -> done(false, "No banner/B0")
					}
				}
			}
		}
	}

	////////////////////////////////////////////////////////////////////
	// Send one app-level operation under MTLS.
	//
	// If sessKey is not yet available:
	//   - run connectAndEstablishSecure() first,
	//   - then re-invoke sendAppFrame() recursively.
	//
	// inner frame = [OP u8][LEN u16 LE][PAYLOAD]
	// outer frame = B3 encrypted (see wrapB3()).
	////////////////////////////////////////////////////////////////////
	private fun sendAppFrame(op: Int, payload: ByteArray, onResult: (Boolean, String?) -> Unit) {
		val st = mtls
		if (st?.sessKey == null) {
			connectAndEstablishSecure { ok, err ->
				if (!ok) { onResult(false, err); return@connectAndEstablishSecure }
				sendAppFrame(op, payload, onResult)
			}
			return
		}
		val inner = byteArrayOf(op.toByte()) + u16le(payload.size) + payload
		val b3 = wrapB3(inner)
		val addr = PreferencesUtil.getOutputDeviceId(appCtx) ?: run { onResult(false, "No device"); return }
		ensureMgr().writeOrConnect(addr, SERVICE_UUID, CHAR_UUID, b3) { okW, errW ->
			onResult(okW, errW)
		}
	}

	////////////////////////////////////////////////////////////////////	
	// Await a single secure reply inside a B3 frame.
	//
	//  - Filters only B3 frames from the stream.
	//  - Verifies HMAC and decrypts cipher using sessKey.
	//  - Unpacks inner [OP|LEN|PAYLOAD] and checks expectOp.
	//  - Returns PAYLOAD bytes if everything is valid.
	//
	// Direction is 'S' (server->client) for inbound frames.
	////////////////////////////////////////////////////////////////////
	private fun awaitAppReply(timeoutMs: Long, expectOp: Int, onResult: (ByteArray?) -> Unit) 
	{
		// We’ll scan frames until we see a B3; then decrypt; then parse inner [OP|LEN|PAYLOAD]
		val framer = Framer()
		val st = mtls ?: return onResult(null)
		val key = st.sessKey ?: return onResult(null)

		fun tryInner(frame: Frame): ByteArray? {
			if (frame.op != 0xB3) return null
			val p = frame.payload
			if (p.size < 2+2+16) return null
			val seq  = rdU16le(p, 0)
			val clen = rdU16le(p, 2)
			if (p.size != 2+2+clen+16) return null
			val cipher = p.copyOfRange(4, 4+clen)
			val macIn  = p.copyOfRange(4+clen, 4+clen+16)

			// MAC verify: HMAC(sess,"ENCM"||sid||'C'||seq||cipher)[0..16] for inbound dir='C'
			val macData = java.io.ByteArrayOutputStream().apply {
				//write("ENCM".toByteArray()); write(beInt(st.sid)); write(byteArrayOf('C'.code.toByte())); write(beShort(seq)); write(cipher)
				write("ENCM".toByteArray()); write(beInt(st.sid)); write(byteArrayOf('S'.code.toByte())); write(beShort(seq)); write(cipher)
				
			}.toByteArray()
			val macExp = hmac16(key, macData)
			if (!macExp.contentEquals(macIn)) return null

			// Decrypt
			//val iv = mtlsIv(key, st.sid, 'C', seq)
			val iv = mtlsIv(key, st.sid, 'S', seq)
			
			val plain = aesCtrEnc(key, iv, cipher)
			if (plain.size < 3) return null
			val op = plain[0].toInt() and 0xFF
			val L  = rdU16le(plain, 1)
			if (plain.size != 3+L) return null
			return if (op == expectOp) plain.copyOfRange(3, 3+L) else null
		}

		// First one-shot, then stream if needed
		ensureMgr().awaitNextNotification(timeoutMs) { first ->
			if (first == null) { onResult(null); return@awaitNextNotification }
			val frames1 = framer.push(first)
			frames1.forEach { tryInner(it)?.let { pay -> onResult(pay); return@awaitNextNotification } }

			ensureMgr().startNotificationStream { chunk ->
				val framesN = framer.push(chunk)
				framesN.forEach { tryInner(it)?.let { pay ->
					ensureMgr().stopNotificationStream(); onResult(pay); return@startNotificationStream
				} }
			}
		}
	}

	////////////////////////////////////////////////////////////////////
	// Send string via secure app channel and wait for MD5 confirmation.
	//
	//   1) Optionally append '\n' depending on Settings.
	//   2) Compute MD5 of the exact bytes sent.
	//   3) Send D0 (op=0xD0) with payload = raw string bytes.
	//   4) Wait for D1 (op=0xD1) payload = [status1][md5(16)].
	//   5) Success if status == 0 and MD5 matches.
	//
	// This gives the app an integrity check that the dongle received
	// the same bytes the app intended to type.
	////////////////////////////////////////////////////////////////////
	fun sendStringAwaitHash(
		value: String,
		timeoutMs: Long = 6000L,
		onResult: (Boolean, String?) -> Unit
	) {
        //if (failIfConnecting(onResult)) return
		
		val addr = PreferencesUtil.getOutputDeviceId(appCtx)
		if (addr.isNullOrBlank()) { onResult(false, "No device selected"); return }

		// add newline per setting
		val raw = if (PreferencesUtil.sendNewLineAfterPassword(appCtx)) value + "\n" else value
		val bytes = raw.toByteArray(Charsets.UTF_8)
		val expectedMd5 = java.security.MessageDigest.getInstance("MD5").digest(bytes)

		// Send D0
		sendAppFrame(0xD0, bytes) { okW, errW ->
			if (!okW) { onResult(false, errW); return@sendAppFrame }
			// Expect D1: status1|md5[16]
			awaitAppReply(timeoutMs, expectOp = 0xD1) { pay ->
				if (pay == null || pay.size != 1+16) { onResult(false, "No/Bad D1"); return@awaitAppReply }
				val status = pay[0].toInt() and 0xFF
				val gotMd5 = pay.copyOfRange(1, 17)
				onResult(status == 0 && gotMd5.contentEquals(expectedMd5),
						 if (status == 0) null else "Non-zero status")
			}
		}
	}

	////////////////////////////////////////////////////////////////////
	// GET_INFO (C1) -> C2
	//
	// Sends C1 with empty payload and expects C2 with ASCII INFO.
	// Parses "LAYOUT=..." and returns the layout code string.
	////////////////////////////////////////////////////////////////////
	fun getLayout(timeoutMs: Long = 4000L, onResult: (Boolean, String?, String?) -> Unit) 
	{
		//if (failIfConnecting { ok, err -> onResult(ok, null, err) }) return
		
		sendAppFrame(0xC1, ByteArray(0)) { okW, errW ->
			if (!okW) { onResult(false, null, errW); return@sendAppFrame }
			awaitAppReply(timeoutMs, expectOp = 0xC2) { pay ->
				if (pay == null) { onResult(false, null, "No INFO"); return@awaitAppReply }
				val text = pay.toString(Charsets.UTF_8)
				// e.g. "LAYOUT=US_WINLIN; PROTO=1.2; FW=1.2.1"
				val m = Regex("""\bLAYOUT=([A-Z0-9_]+)""").find(text)
				val layout = m?.groupValues?.get(1)
				if (layout == null) onResult(false, null, "Parse fail: $text")
				else onResult(true, layout, null)
			}
		}
	}

	////////////////////////////////////////////////////////////////////
	// Send C4 to trigger firmware "reset to factory defaults".
	// Expects a bare ACK frame (op=0x00, empty payload) on success.
	////////////////////////////////////////////////////////////////////
	fun resetToDefault(timeoutMs: Long = 4000L, onResult: (Boolean, String?) -> Unit) 
	{
		//if (failIfConnecting(onResult)) return
		
		sendAppFrame(0xC4, ByteArray(0)) { okW, errW ->
			if (!okW) { onResult(false, errW); return@sendAppFrame }
			awaitAppReply(timeoutMs, expectOp = 0x00) { pay ->
				onResult(pay != null && pay.isEmpty(), if (pay == null) "No ACK" else null)
			}
		}
	}

    ////////////////////////////////////////////////////////////////////
    // Enable fast raw-key mode on the dongle for the current session.
    //
    // Sends C8 (SET_RAW_FAST_MODE) via MTLS with payload:
    //   [0x01] = enable
    //
    // Expects ACK (op=0x00, empty payload) on success.
    //
    // On success, fastKeysSessionEnabled is set to true. We do NOT
    // auto-disable it; firmware clears it on BLE disconnect.
    ////////////////////////////////////////////////////////////////////
    fun enableFastKeys(timeoutMs: Long = 4000L, onResult: (Boolean, String?) -> Unit) {
		//if (failIfConnecting(onResult)) return
		
        // If we already know it's on for this session, just short-circuit
        if (fastKeysSessionEnabled) {
            onResult(true, null)
            return
        }

        // Use secure app channel - sendAppFrame will establish MTLS if needed.
        val body = byteArrayOf(0x01) // 1 = enable
        sendAppFrame(0xC8, body) { okW, errW ->
            if (!okW) {
                onResult(false, errW)
                return@sendAppFrame
            }

            // Expect bare ACK (0x00, empty payload)
            awaitAppReply(timeoutMs, expectOp = 0x00) { pay ->
                val ok = (pay != null && pay.isEmpty())
                if (ok) {
                    fastKeysSessionEnabled = true
                    onResult(true, null)
                } else {
                    onResult(false, "No ACK for fast keys")
                }
            }
        }
    }

    ////////////////////////////////////////////////////////////////////
    // sendRawKeyTap (0xE0)
    //
    // Fast-path: send a single HID usage (mods + usage) optionally
    // repeated, without MD5, without ACK.
    //
    // PRECONDITIONS:
    //   - MTLS session has been established at least once (APPKEY ok).
    //   - Firmware raw-fast-mode has been enabled via C8.
    //
    // Payload format:
    //   [mods1][usage1]            (len = 2)
    //   [mods1][usage1][repeat1]   (len = 3, repeat 1..255)
    ////////////////////////////////////////////////////////////////////
    fun sendRawKeyTap(
        mods: Int,
        usage: Int,
        repeat: Int = 1,
        onResult: (Boolean, String?) -> Unit
    ) {
		//if (failIfConnecting(onResult)) return
		
        // Make sure we have a device selected
        val addr = PreferencesUtil.getOutputDeviceId(appCtx)
        if (addr.isNullOrBlank()) {
            onResult(false, "No device selected")
            return
        }

        // We expect MTLS to have run already (so firmware will accept 0xE0)
        val st = mtls
        if (st?.sessKey == null) {
            // No MTLS yet – run handshake once, then retry this call
            connectAndEstablishSecure { ok, err ->
                if (!ok) {
                    onResult(false, err)
                } else {
                    sendRawKeyTap(mods, usage, repeat, onResult)
                }
            }
            return
        }

        if (!fastKeysSessionEnabled) {
            onResult(false, "Fast keys not enabled")
            return
        }

        val rep = repeat.coerceIn(1, 255)
        val payload = if (rep <= 1) {
            byteArrayOf(mods.toByte(), usage.toByte())
        } else {
            byteArrayOf(mods.toByte(), usage.toByte(), rep.toByte())
        }

        // 0xE0 frame is intentionally sent "raw" (non-B3) for speed.
        sendRawFrame(0xE0, payload) { okW, errW ->
            onResult(okW, errW)
        }
    }

	////////////////////////////////////////// NEW BANNER AND GETAPP KEY
	// Wait until we see EITHER the plaintext INFO banner (unprovisioned) OR a binary B0 frame (provisioned)
	private fun waitForInfoBannerOrB0(
		totalTimeoutMs: Long,
		onDone: (plain: Triple<String,String,String>?, b0Payload: ByteArray?) -> Unit
	) {
		val deadline = android.os.SystemClock.elapsedRealtime() + totalTimeoutMs
		val framer = Framer()
		val sb = java.io.ByteArrayOutputStream()
		var finished = false

		fun finish(plain: Triple<String,String,String>?, b0: ByteArray?) {
			if (finished) return
			finished = true
			ensureMgr().stopNotificationStream()
			onDone(plain, b0)
		}

		// Start streaming raw notify chunks
		ensureMgr().startNotificationStream { chunk ->
			if (finished) return@startNotificationStream

			// 1) Try binary first (B0)
			val frames = framer.push(chunk)
			frames.firstOrNull { it.op == 0xB0 }?.let { frame ->
				logd( "CONNECT: detected B0 (len=${frame.payload.size})")
				mtls = null
				finish(null, frame.payload)
				return@startNotificationStream
			}

			// 2) Also accumulate for plaintext banner (unprovisioned)
			sb.write(chunk)
			parsePlainInfo(sb.toByteArray())?.let {
				logd( "CONNECT: detected PLAINTEXT INFO banner")
				finish(it, null)
				return@startNotificationStream
			}

			// timeout guard
			if (android.os.SystemClock.elapsedRealtime() >= deadline) {
				logd( "CONNECT: no banner/B0 before timeout")
				finish(null, null)
			}
		}
	}

	////////////////////////////////////////////////////////////////////
	// MTLS handshake from B0.
	//
	// Input: B0 payload = [srvPub65][sid_le32]
	//
	// Steps:
	//   1) Fetch APPKEY for this address (must exist).
	//   2) Generate ephemeral P-256 key pair (cliPub65).
	//   3) Build B1 payload = cliPub65 || mac16, where:
	//        mac16 = HMAC(APPKEY, "KEYX"||sid_le||srvPub||cliPub)[0..16].
	//   4) Send B1 and wait for B2 (or error frame 0xFF).
	//   5) Compute ECDH shared secret from srvPub.
	//   6) Derive sessKey = HKDF-SHA256(APPKEY, shared, "MT1"||sid_be||srvPub||cliPub).
	//   7) Verify B2 payload == HMAC(sessKey, "SFIN"||sid_le||srvPub||cliPub)[0..16].
	//   8) If OK, initialise global mtls state with (sid, sessKey, seqOut=0).
	//
	// On success, all future app ops use B3 encrypted frames.
	////////////////////////////////////////////////////////////////////
	private fun doBinaryHandshakeFromB0(
		address: String,
		b0Payload: ByteArray,
		onDone: (Boolean, String?) -> Unit
	) {
		if (b0Payload.size != 69) { onDone(false, "Bad B0"); return }
		val srvPub65 = b0Payload.copyOfRange(0, 65)
		val sid = java.nio.ByteBuffer.wrap(b0Payload, 65, 4)
			.order(java.nio.ByteOrder.LITTLE_ENDIAN).int
		val appKey = BleAppSec.getKey(appCtx, address)
		if (appKey == null) {
			logd("MTLS: no APPKEY for $address – failing handshake")
			onDone(false, "APPKEY missing")
			return
		} else {
			logd("MTLS: using APPKEY for $address, len=${appKey.size}")
		}

		// B1 = cliPub65 || mac16, mac16 = HMAC(APPKEY, "KEYX"||sid||srv_pub||cli_pub)[0..16]
		val kp = genP256()
		val cliPub65 = pubUncompressed(kp.public)

		// check if this is LE on the dongle
		val keyxMsg = java.io.ByteArrayOutputStream().apply {
			write("KEYX".toByteArray())
			write(leInt(sid))
			write(srvPub65)
			write(cliPub65)
		}.toByteArray()
		val mac16 = hmac16(appKey, keyxMsg)

		// debug
		logd( "B1 sid(le)=${leInt(sid).toHex()} srv[0..4]=${srvPub65.copyOfRange(0,5).toHex()} cli[0..4]=${cliPub65.copyOfRange(0,5).toHex()} mac16=${mac16.toHex()}")


		val b1Pay = cliPub65 + mac16
		//sendRawFrame(0xB1, b1Pay) { okW, errW ->
		//	if (!okW) { onDone(false, errW); return@sendRawFrame }
		sendRawFrame(0xB1, b1Pay, addressOverride = address) { okW, errW ->
			if (!okW) {
				onDone(false, errW)
				return@sendRawFrame
			}

			awaitNextFrame(4000L, predicate = { it.op == 0xB2 || it.op == 0xFF }) { resp ->
				//if (resp == null || resp.op == 0xFF) { onDone(false, "B2 missing"); return@awaitNextFrame }
                if (resp == null) {
                    onDone(false, "B2 timeout")
                    return@awaitNextFrame
                }
                if (resp.op == 0xFF) {
                    val reason = mapHandshakeErrorFromDevice(resp.payload)
                    onDone(false, reason)
                    return@awaitNextFrame
                }				

				// Session key: HKDF-SHA256(salt=APPKEY, IKM=ECDH(shared), info="MT1"||sid||srv_pub||cli_pub)
				val shared = ecdh(kp.private, srvPub65)
				val info = java.io.ByteArrayOutputStream().apply {
					write("MT1".toByteArray())     // match firmware
					write(beInt(sid))
					write(srvPub65)
					write(cliPub65)
				}.toByteArray()
				val sess = hkdfSha256(appKey, shared, info)

				// Verify B2 mac16 == HMAC(sess, "SFIN"||sid||srv_pub||cli_pub)[0..16]
				val sfin = java.io.ByteArrayOutputStream().apply {
					write("SFIN".toByteArray())
					write(leInt(sid))
					write(srvPub65)
					write(cliPub65)
				}.toByteArray()
				val expect = hmac16(sess, sfin)
				if (!expect.contentEquals(resp.payload)) { onDone(false, "SFIN mismatch"); return@awaitNextFrame }

				mtls = MtlsState(sid = sid, sessKey = sess, seqOut = 0)
				// new session, raw fast mode off by default
				fastKeysSessionEnabled = false  
				
				logd( "MTLS: session established (sid=$sid)")
				
				onDone(true, null)
			}
		}
	}

	///////////////////////////////////////////////////////////////////
	private val mainHandler by lazy { android.os.Handler(android.os.Looper.getMainLooper()) }
	private fun toast(msg: String) {
		mainHandler.post {
			android.widget.Toast
				.makeText(appCtx, msg, android.widget.Toast.LENGTH_LONG)
				.show()
		}
	}
	
	// Switch off "Use external keyboard device", drop the session, and inform the user. 
    private fun disableAutoConnect(reason: String)
    {
        // If Settings has a prompt provider, back off quietly and let manual flow proceed.
        if (passwordPrompt != null) {
            disconnect()
            return
        }

        // Mark that OFF was due to an error, not user choice
        PreferencesUtil.setOutputDeviceDisabledByError(appCtx, true)
        PreferencesUtil.setUseExternalKeyboardDevice(appCtx, false)

        disconnect() // also sets _connected=false
        toast("$reason\nGo to Settings - Output device to reconnect.")        
    }

	private fun reconnectExpectingB0AndHandshake(
		onDone: (Boolean, String?) -> Unit = { _, _ -> }
	) {
		val addr = PreferencesUtil.getOutputDeviceId(appCtx)
		if (addr.isNullOrBlank()) { onDone(false, "No device"); return }

		ensureMgr().disconnect()
		mainHandler.postDelayed({
			ensureMgr().connect(addr) { ok, err ->
				if (!ok) { onDone(false, err); return@connect }
				
				ensureNotificationsEnabled(addr) { _, _ ->			
						// After APPKEY set, firmware sends ONLY B0 on connect
						waitForInfoBannerOrB0(totalTimeoutMs = 5000L) { plain, b0 ->
							if (b0 == null) {
								logd( "RECONNECT: expected B0, got ${if (plain!=null) "PLAINTEXT" else "nothing"}")
								onDone(false, "No B0 after provisioning")
							} else {
								doBinaryHandshakeFromB0(addr, b0, onDone)
							}
						}
				}
			}
		}, 1000L)
	}

	////////////////////////////////////////////////////////////////////
	// Generic INFO query over secure channel.
	// Useful for debugging / future UI that wants the full C2 text.
	////////////////////////////////////////////////////////////////////
	fun getInfo(timeoutMs: Long = 4000L, onResult: (Boolean, String?, String?) -> Unit) {
		//if (failIfConnecting { ok, err -> onResult(ok, null, err) }) return
		
		// send app op 0xC1 (GET_INFO), expect 0xC2 with ASCII payload
		sendAppFrame(0xC1, ByteArray(0)) { okW, errW ->
			if (!okW) { onResult(false, null, errW); return@sendAppFrame }
			awaitAppReply(timeoutMs, expectOp = 0xC2) { pay ->
				val txt = pay?.toString(Charsets.UTF_8)?.trim()
				if (txt.isNullOrBlank()) onResult(false, null, "Empty INFO")
				else onResult(true, txt, null)
			}
		}
	}

	////////////////////////////////////////////////////////////////////
	// SET_LAYOUT (C0) -> ACK
	//
	// Payload is a UTF-8 string layout code (e.g. "UK_WINLIN").
	// On success, device replies with empty ACK (op=0x00).
	////////////////////////////////////////////////////////////////////
	fun setLayoutString(layout: String, timeoutMs: Long = 4000L, onResult: (Boolean, String?) -> Unit) {
        //if (failIfConnecting(onResult)) return
		
		val body = layout.toByteArray(Charsets.UTF_8)
		sendAppFrame(0xC0, body) { okW, errW ->
			if (!okW) { onResult(false, errW); return@sendAppFrame }
			awaitAppReply(timeoutMs, expectOp = 0x00) { pay ->
				onResult(pay != null && pay.isEmpty(), if (pay == null) "No ACK" else null)
			}
		}
	}

	// UI bridge: injected by Settings fragment.
	//
	// Provider signature:
	//   (Context, (CharArray?) -> Unit) -> Unit
	//
	// The provider is responsible for showing a password dialog,
	// then calling the callback with:
	//   - char[] password  => user confirmed
	//   - null             => user cancelled
	private var passwordPrompt: ((Context, (CharArray?) -> Unit) -> Unit)? = null

	fun setPasswordPrompt(provider: (Context, (CharArray?) -> Unit) -> Unit) {
		passwordPrompt = provider
	}

	////////////////////////////////////////////////////////////////////
	// High-level "ask user, then fetch APPKEY" helper.
	//
	// Behaviour:
	//   - If forcePrompt == false and APPKEY already exists,
	//     returns immediately.
	//   - Otherwise, loops:
	//       * Ask passwordPrompt provider for password.
	//       * Run ensureAppKeyCachedBinary().
	//       * On success => done.
	//       * On error (not "Password cancelled") => show toast and
	//         re-prompt, giving the user another chance.
	//
	// This is only used in flows where we are allowed to show UI
	// (e.g. Settings -> Output Device connect).
	////////////////////////////////////////////////////////////////////
	fun requestAndStoreAppKeyWithPrompt(
		address: String,
		forcePrompt: Boolean = false,
		onDone: (Boolean, String?) -> Unit
	) {
		logd( "DEBUG: entered requestAndStoreAppKeyWithPrompt for $address")

		// If we’re forcing a prompt (because firmware said "unprovisioned"),
		// DO NOT early-return even if a cached key exists.
		if (!forcePrompt) {
			BleAppSec.getKey(appCtx, address)?.let { onDone(true, null); return }
		}

		val ask = passwordPrompt
		logd( "DEBUG: passwordPrompt provider is " + (ask != null))
		
		if (ask == null) {
			loge( "APPKEY: no password prompt provider set")
			onDone(false, "No password UI")
			return
		}

		mainHandler.post {
			fun promptAndRun() {
				ask(appCtx) { chars ->
					if (chars == null || chars.isEmpty()) {
						onDone(false, "Password cancelled")
						return@ask
					}

					val pwLocal = chars.copyOf()
					java.util.Arrays.fill(chars, '\u0000')

					ensureAppKeyCachedBinary(
						address = address,
						timeoutMs = 6000L,
						forceFetch = true,
						userPassword = pwLocal
					) { ok, err ->
						java.util.Arrays.fill(pwLocal, '\u0000')

						if (ok) {
							onDone(true, null)
						} else {
							// Keep retrying unless user cancelled the dialog
							when (err) {
								"Password cancelled" -> onDone(false, err)
								else -> {
									// Optional: toast once so user knows why we re-prompt
									toast("Provisioning failed: ${err ?: "unknown"}. Try again.")
									promptAndRun()
								}
							}
						}
					}
				}
			}
			logd( "DEBUG: invoking passwordPrompt on main thread (looping)")
			promptAndRun()
		}
		
		logd( "DEBUG: invoking passwordPrompt on main thread")

	}

	fun clearPasswordPrompt() { passwordPrompt = null }

	////////////////////////////////////////////////////////////////////
	// Entry point used ONLY from Settings screen.
	//
	// Similar to connectAndFetchLayoutSimple(), but with:
	//   - short timeouts,
	//   - a single retry,
	//   - allowPrompt = true (can provoke provisioning).
	////////////////////////////////////////////////////////////////////
    fun connectFromSettings(onDone: ((Boolean, String?) -> Unit)? = null) 
	{
        connectAndFetchLayoutSimple(
            timeoutMs = 3500L,
            retries = 1,
            allowPrompt = true,
            onDone = onDone
        )
    }

	////////////////////////////////////////////////////////////////////
	// Ensure NUS RX notifications are enabled for this device.
	//
	// Current hack implementation:
	//   - startNotificationStream() once to force CCCD write,
	//   - stop it after ~100ms,
	//   - assume CCCD is now ON.
	//
	// NOTE: This relies on BluetoothDeviceManager enabling CCCD in its
	// GATT callback. If that behaviour changes, update this helper to
	// explicitly write the CCCD descriptor instead of "priming" it.
	////////////////////////////////////////////////////////////////////
//	private fun ensureNotificationsEnabled(addr: String, onDone: (Boolean, String?) -> Unit) {
//		// Stub fall-back: start + immediately stop a stream to force CCCD = ON.
//		ensureMgr().startNotificationStream { /* prime CCCD */ }
//		android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
//			ensureMgr().stopNotificationStream()
//			onDone(true, null)
//		}, 100) // 100 ms is enough to write CCCD
//	}

	// new approach
	private fun ensureNotificationsEnabled(addr: String, onDone: (Boolean, String?) -> Unit) {
		// BluetoothDeviceManager already discovers services and writes CCCD
		// in its GATT callback, so by the time connect() reports success
		// notifications are enabled. Do not start a dummy stream here, it
		// can race with the banner/B0 and eat the first notification.
		onDone(true, null)
	}

}
