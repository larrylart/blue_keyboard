////////////////////////////////////////////////////////////////////
// BluetoothDeviceManager
// Created by: Larry Lart
//
// Single entry point for all BLE operations used by BleHub:
//  - Maintains a merged list of bonded + scanned devices for the UI.
//  - Runs one-shot RSSI scans for a given set of MAC addresses
//    (used by BleHub auto-connect logic).
//  - Manages a persistent GATT connection (connect + write).
//  - Handles notification streaming and one-shot notification waits.
//
// This class is intentionally UI-agnostic: it exposes LiveData and
// callbacks, but never touches Views directly.
////////////////////////////////////////////////////////////////////
package com.blu.blukeyborg

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import android.os.Handler
import android.os.Looper
import androidx.annotation.RequiresPermission
import androidx.lifecycle.MutableLiveData
import android.bluetooth.BluetoothStatusCodes
import com.blu.blukeyborg.PreferencesUtil

// Lightweight summary used in the device picker and auto-connect logic.
// "bonded" is purely informational here. pairing is controlled separately.
data class BtDevice(val name: String, val address: String, val bonded: Boolean)

////////////////////////////////////////////////////////////////////
// Context is the app Context, used for:
//  - obtaining BluetoothManager
//  - permission-checked BLE operations
////////////////////////////////////////////////////////////////////
class BluetoothDeviceManager(private val context: Context)
{
	// Main-thread handler used to:
	//  - post LiveData updates
	//  - schedule scan timeouts and write timeouts
	//
	// btMgr / adapter / scanner are lazy-accessed to keep things null-safe
	// when Bluetooth is off or unavailable.
	//
	// devicesMap holds the merged set of:
	//   - already bonded devices
	//   - devices seen during current/last scan
	//
	// "devices" LiveData is what the UI observes; we keep the map as
	// the mutable backing store and publish a sorted copy via postList().	
    private val main = Handler(Looper.getMainLooper())

    private val btMgr = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val adapter: BluetoothAdapter? get() = btMgr.adapter
    private val scanner: BluetoothLeScanner? get() = adapter?.bluetoothLeScanner

    /** Combined list (bonded ∪ scanned) */
    val devices = MutableLiveData<List<BtDevice>>(emptyList())

    private val devicesMap = LinkedHashMap<String, BtDevice>()
    private var scanning = false

	// --- RSSI scan helpers (used by BleHub auto-connect) ---
	//
	// rssiTargets   : optional filter (MAC addresses we care about).
	// rssiMap       : strongest RSSI per target address seen so far.
	// rssiCallback  : single callback invoked when the one-shot scan ends.
	//
	// These are only populated while scanForRssiOnce() is active.
    @Volatile private var rssiTargets: Set<String>? = null
    private val rssiMap = mutableMapOf<String, Int>()
    @Volatile private var rssiCallback: ((Map<String, Int>) -> Unit)? = null

	// Tracks the MTU negotiated with the current GATT connection.
	// Default is 23 until onMtuChanged() is called.
	@Volatile private var currentMtu: Int = 23

	// Quick readiness check for UI / higher layers.
	// Returns true if the adapter exists and is currently turned ON.
    fun isBluetoothReady(): Boolean = adapter?.isEnabled == true

	// Refresh the list of bonded (paired) devices and merge them into
	// devicesMap.
	//
	// Note: This does NOT start a BLE scan - it's just a query to the
	// system's bondedDevices set. We swallow SecurityException in case
	// BLUETOOTH_CONNECT permission is missing.	
    @SuppressLint("MissingPermission")
    @RequiresPermission(allOf = [Manifest.permission.BLUETOOTH_CONNECT])
    fun refreshBonded() {
        val bonded = try { adapter?.bondedDevices.orEmpty() } catch (_: SecurityException) { emptySet() }
        var changed = false
        for (d in bonded) {
            val name = d.name ?: "Unknown"
            val updated = BtDevice(name, d.address, true)
            val prev = devicesMap[d.address]
            if (prev == null || prev != updated) {
                devicesMap[d.address] = updated
                changed = true
            }
        }
        if (changed) postList()
    }

	// Publish the current devicesMap as a sorted list via LiveData.
	//
	// Sort order:
	//   1) Bonded devices first
	//   2) Then by case-insensitive name
	//
	// All mutations to LiveData happen on the main thread.
    private fun postList() {
        main.post {
            devices.value = devicesMap.values.sortedWith(
                compareByDescending<BtDevice> { it.bonded }.thenBy { it.name.lowercase() }
            )
        }
    }

	////////////////////////////////////////////////////////////////////
	// start()
	//
	// Starts a "regular" BLE scan and keeps merging results into devicesMap.
	// This is used by the device picker UI.
	//
	// - If a scan is already in progress or adapter is null, it does nothing.
	// - Also refreshes bonded devices before starting the scan, so the
	//   UI always shows known devices even if nothing is advertising.
	////////////////////////////////////////////////////////////////////
    @SuppressLint("MissingPermission")
    @RequiresPermission(allOf = [Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT])		
    fun start() {
        if (scanning || adapter == null) return
        scanning = true
        refreshBonded()
        try { scanner?.startScan(scanCb) } catch (_: SecurityException) {}
    }

	// Stop the regular scan, if running.
	// Safe to call multiple times; silently ignores SecurityException.
    @SuppressLint("MissingPermission")
    fun stop() {
        if (!scanning) return
        scanning = false
        try { scanner?.stopScan(scanCb) } catch (_: SecurityException) {}
    }

    ////////////////////////////////////////////////////////////////
    // One-shot RSSI scan for a set of target MAC addresses.
    //
    // Runs a BLE scan for [durationMs] and returns a map:
    //      address -> strongest RSSI seen during that window.
    //
    // Notes:
    //  - If targetAddresses is empty or we can't get a scanner,
    //    we return an empty map immediately.
    //  - We temporarily stop any regular scan() so the RSSI scan
    //    owns the scanCb for its duration.
    //  - Errors / missing permissions are reported as empty maps,
    //    not thrown exceptions.
    //
    // Used by BleHub to choose the "best" dongle when multiple
    // provisioned devices are available.
    ////////////////////////////////////////////////////////////////
    @SuppressLint("MissingPermission")
    @RequiresPermission(allOf = [Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT])
    fun scanForRssiOnce(
        targetAddresses: Set<String>,
        durationMs: Long = 1200L,
        onDone: (Map<String, Int>) -> Unit
    ) {
        val scanner = scanner
        if (targetAddresses.isEmpty() || scanner == null) {
            onDone(emptyMap())
            return
        }

		// If a UI scan is running, stop it so that scanCb is used only
		// for this one-shot RSSI measurement.
        if (scanning) 
		{
            try { scanner.stopScan(scanCb) } catch (_: SecurityException) {}
            scanning = false
        }

        rssiTargets = targetAddresses
        rssiMap.clear()
        rssiCallback = onDone

        scanning = true
        // Also merge bonded devices as usual (for UI list)
        refreshBonded()

        try {
            scanner.startScan(scanCb)
        } catch (_: SecurityException) {
            // No permission or other issue – fall back to "no RSSI info"
            scanning = false
            rssiTargets = null
            rssiCallback = null
            onDone(emptyMap())
            return
        }

        // Schedule scan stop + callback after [durationMs].
        // We always invoke the callback exactly once, either here
        // or if scanning was stopped externally before the timeout.
        main.postDelayed({
            if (!scanning) {
                // Already stopped elsewhere; make sure we call callback once.
                val cb = rssiCallback
                rssiTargets = null
                rssiCallback = null
                val result = HashMap(rssiMap)
                rssiMap.clear()
                cb?.invoke(result)
                return@postDelayed
            }

            try { scanner.stopScan(scanCb) } catch (_: SecurityException) {}
            scanning = false

            val cb = rssiCallback
            rssiTargets = null
            rssiCallback = null

            val result = HashMap(rssiMap)
            rssiMap.clear()
            cb?.invoke(result)
        }, durationMs)
    }
	
	////////////////////////////////////////////////////////////////
	// Shared ScanCallback for both:
	//   - regular UI scan (start/stop)
	//   - one-shot RSSI scan (scanForRssiOnce)
	//
	// Always updates devicesMap for the UI, and optionally tracks RSSI
	// if rssiTargets is non-null.
	////////////////////////////////////////////////////////////////
    private val scanCb = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val dev = result.device ?: return
            val address = dev.address ?: return
            val name = dev.name ?: result.scanRecord?.deviceName ?: "Unknown"
            val bonded = try { dev.bondState == BluetoothDevice.BOND_BONDED } catch (_: SecurityException) { false }

            val existing = devicesMap[address]
            val updated = BtDevice(name, address, bonded)
            if (existing == null || existing != updated) {
                devicesMap[address] = updated
                postList()
            }
			
            // If we are in RSSI-scan mode, track the strongest RSSI per target device address.
            rssiTargets?.let { targets ->
                if (targets.contains(address)) {
                    val rssi = result.rssi
                    val prev = rssiMap[address]
                    if (prev == null || rssi > prev) {
                        rssiMap[address] = rssi
                    }
                }
            }			
        }
    }

	////////////////////////////////////////////////////////////////
	// Request a bond with the given MAC address.
	//
	// This will trigger the system pairing UI if needed. We don't show
	// any UI ourselves; we just return whether createBond() was accepted.
	//
	// NOTE: Success here means "pairing requested", not necessarily
	// "pairing has fully completed".
	////////////////////////////////////////////////////////////////
    @SuppressLint("MissingPermission")
    @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
    fun pair(address: String): Boolean {
        val dev = try { adapter?.getRemoteDevice(address) } catch (_: IllegalArgumentException) { null } ?: return false
        return try { dev.createBond() } catch (_: SecurityException) { false }
    }

	////////////////////////////////////////////////////////////////
	// Remove an existing bond using reflection.
	//
	// Android doesn't expose a public unpair() API, so we call the
	// hidden "removeBond" method. This may break on some OEM builds,
	// which is why all reflection is wrapped in try/catch.
	////////////////////////////////////////////////////////////////
    @SuppressLint("MissingPermission")
    @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
    fun unpair(address: String): Boolean {
        val dev = try { adapter?.getRemoteDevice(address) } catch (_: IllegalArgumentException) { null } ?: return false
        return try {
            val m = dev.javaClass.getMethod("removeBond")
            (m.invoke(dev) as? Boolean) == true
        } catch (_: Throwable) { false }
    }
	
	// --- Persistent GATT connection state ---
	//
	// gatt             : current live BluetoothGatt, if any.
	// resultCb         : callback for the "current operation" (connect or write).
	// connectedAddress : MAC of the device we think we are connected to.
	// discovered       : true once services have been discovered.
	//
	// lastCharacteristic : last characteristic we wrote to; used mostly for
	//                      debugging and future extensions.
	//
	// closeAfterOp     : if true, cleanupGatt() after succeed()/fail().
	//                    BleHub uses persistent mode (false) so the link
	//                    stays up across multiple commands.
	//
	// notifyCharacteristic : the RX / notification characteristic we listen on.
	// notificationsEnabled  : tracks whether CCCD write was successful.
	/////////////////////////////////////////
	@Volatile private var gatt: android.bluetooth.BluetoothGatt? = null
	@Volatile private var resultCb: ((Boolean, String?) -> Unit)? = null

	// Persistent-connection support:
	@Volatile private var connectedAddress: String? = null
	@Volatile private var discovered = false
	@Volatile private var lastCharacteristic: android.bluetooth.BluetoothGattCharacteristic? = null
	@Volatile private var closeAfterOp = true   // legacy single-shot = true; persistent = false

    @Volatile private var notifyCharacteristic: android.bluetooth.BluetoothGattCharacteristic? = null
    @Volatile private var notificationsEnabled = false
	
	// --- Notification handling ---
	//
	// notifListener : one-shot waiter for "next notification" (awaitNextNotification).
	// notifTimeouts : handler for scheduling per-waiter timeouts.
	// notifBuffer   : holds notifications that arrive when no waiter is present.
	//
	// streamListener: long-lived consumer used when BleHub wants a continuous
	//                 stream of notifications (e.g. for binary frame parsing).
	//
	// Only one of (streamListener, notifListener) is active at a time.
	// Stream has priority: if it's set, one-shot waiter is not used.
	/////////////////////////////
	@Volatile private var notifListener: ((ByteArray?) -> Unit)? = null
	private val notifTimeouts = Handler(Looper.getMainLooper())

	// queue to hold packets that arrive when no waiter is set
	private val notifBuffer: ArrayDeque<ByteArray> = ArrayDeque()

	// streaming listener: active while we want to collect multiple notifications
	@Volatile private var streamListener: ((ByteArray) -> Unit)? = null

	////////////////////////////////////////////////////////////////
	// Start consuming all future notifications via [onChunk].
	//
	// Also immediately flushes any buffered notifications so that
	// the stream sees *everything* in order, even if some data
	// arrived before the stream was registered.
	////////////////////////////////////////////////////////////////
	fun startNotificationStream(onChunk: (ByteArray) -> Unit) {
		streamListener = onChunk
		// Immediately deliver anything that arrived before the stream was started
		synchronized(notifBuffer) {
			while (notifBuffer.isNotEmpty()) {
				onChunk(notifBuffer.removeFirst())
			}
		}
	}

	////////////////////////////////////////////////////////////////
	// Stop streaming; subsequent notifications will go to a one-shot
	// waiter (if any) or be buffered for the next waiter/stream.
	////////////////////////////////////////////////////////////////
	fun stopNotificationStream() {
		streamListener = null
	}

	// Write watchdog: if a write does not complete within writeTimeoutMs,
	// we treat it as a failure and optionally close the GATT.
	private val writeTimeoutMs = 10_000L

	// Connect watchdog used in auto-connect flows.
	//
	// If we never reach onServicesDiscovered() (discovered == false) before
	// this runnable fires, we fail the connect attempt with "Connect timeout".	
	private val timeoutRunnable = Runnable {
		resultCb?.invoke(false, "Timeout while writing characteristic")
		if (closeAfterOp) cleanupGatt()
	}

	// watchdog for initial GATT connect (auto-connect path)
	private val connectTimeoutRunnable = Runnable {
		// If we never reached service discovery, treat as connect timeout
		if (!discovered) {
			fail("Connect timeout")
		}
	}

	////////////////////////////////////////////////////////////////
	// Complete the current operation successfully.
	// Clears pending timeouts and invokes resultCb(true).
	////////////////////////////////////////////////////////////////
	fun succeed() {
		main.removeCallbacks(connectTimeoutRunnable)      
		val cb = resultCb
		resultCb = null
		cb?.invoke(true, null)
		if (closeAfterOp) cleanupGatt()
	}

	////////////////////////////////////////////////////////////////
	// Complete the current operation with failure.
	// Clears pending timeouts and invokes resultCb(false, msg).	
	////////////////////////////////////////////////////////////////
	fun fail(msg: String) {
		main.removeCallbacks(connectTimeoutRunnable)      
		val cb = resultCb
		resultCb = null
		cb?.invoke(false, msg)
		if (closeAfterOp) cleanupGatt()
	}

	////////////////////////////////////////////////////////////////////
	// cleanupGatt()
	//
	// Fully tear down the current GATT connection:
	//
	//  - Cancel write + connect timeouts.
	//  - disconnect() + close() the BluetoothGatt.
	//  - Reset all connection-related fields and notification state.
	//  - Clear notification buffers and listeners.
	//  - Force single-shot mode by setting closeAfterOp = true.
	//
	// This is the only place that should directly close the GATT.
	////////////////////////////////////////////////////////////////////
	private fun cleanupGatt() {
		try {
			main.removeCallbacks(timeoutRunnable)
			main.removeCallbacks(connectTimeoutRunnable)   // connection timeout
		} catch (_: Throwable) {}
	
		try { gatt?.disconnect() } catch (_: Throwable) {}
		try { gatt?.close() } catch (_: Throwable) {}
		gatt = null
		resultCb = null
		discovered = false
		connectedAddress = null
		lastCharacteristic = null
		// notify
        notifyCharacteristic = null
        notificationsEnabled = false
		
		synchronized(notifBuffer) { notifBuffer.clear() }
		try { notifTimeouts.removeCallbacksAndMessages(null) } catch (_: Throwable) {}
		notifListener = null
		
		closeAfterOp = true
	}

	////////////////////////////////////////////////////////////////////
	// connect()
	//
	// Establish a persistent GATT connection to [address]:
	//
	//  - Stops any active scan (to avoid conflicting radios).
	//  - Sets up resultCb and connection state.
	//  - Optionally arms a connect timeout via connectTimeoutRunnable.
	//  - Posts the actual connectGatt() call to the main thread.
	//
	// Completion:
	//  - On success: we report via succeed() after services are discovered
	//    (and CCCD has been written, if needed).
	//  - On failure: fail(msg) is called from the callback chain.
	////////////////////////////////////////////////////////////////////
	@android.annotation.SuppressLint("MissingPermission")
	@androidx.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
	fun connect(
		address: String,
		connectTimeoutMs: Long? = null,
		onResult: (Boolean, String?) -> Unit
	) {
		stop() // pause scanning
		val dev = try { adapter?.getRemoteDevice(address) } catch (_: IllegalArgumentException) { null }
		if (dev == null) { onResult(false, "Invalid device address"); return }
		resultCb = onResult
		closeAfterOp = false
		connectedAddress = address
		discovered = false

		// arm connect watchdog if requested
		main.removeCallbacks(connectTimeoutRunnable)
		if (connectTimeoutMs != null && connectTimeoutMs > 0) {
			main.postDelayed(connectTimeoutRunnable, connectTimeoutMs)
		}

		main.post {
			try {
				// Do NOT teardown after this - we keep a live GATT for the app session
				try { gatt?.disconnect() } catch (_: Throwable) {}
				try { gatt?.close() } catch (_: Throwable) {}
				gatt = dev.connectGatt(context, /*autoConnect*/ false, persistentGattCb)
			} catch (t: Throwable) {
				fail("Exception during connect: ${t.message}")
			}
		}

		// We return via onServicesDiscovered - succeed()/fail()
	}

	////////////////////////////////////////////////////////////////////
	// Persistent GATT callback used for the entire app session.
	//
	// Responsibilities:
	//  - Bump connection priority on connect.
	//  - Negotiate MTU (aiming for 247 bytes).
	//  - Discover services and locate NUS TX/RX characteristics.
	//  - Enable notifications on RX (write CCCD).
	//  - Route notifications to stream / one-shot listeners.
	//  - Handle write completions via onCharacteristicWrite().
	////////////////////////////////////////////////////////////////////
	private val persistentGattCb = object : android.bluetooth.BluetoothGattCallback() 
	{
		override fun onConnectionStateChange(g: android.bluetooth.BluetoothGatt, status: Int, newState: Int) {
			
			// Connected: request high priority and a larger MTU before service discovery
			if (status == android.bluetooth.BluetoothGatt.GATT_SUCCESS &&
				newState == android.bluetooth.BluetoothProfile.STATE_CONNECTED) {

				// Prefer faster link for the initial handshake
				if (android.os.Build.VERSION.SDK_INT >= 21) {
					try {
						g.requestConnectionPriority(android.bluetooth.BluetoothGatt.CONNECTION_PRIORITY_HIGH)
					} catch (_: Throwable) {}
				}

				// Request a larger MTU so the dongle can send the whole line in one notify
				if (android.os.Build.VERSION.SDK_INT >= 21) {
					val ok = try { g.requestMtu(247) } catch (_: Throwable) { false }
					if (!ok) {
						// If request failed, proceed anyway
						g.discoverServices()
					}
				} else {
					g.discoverServices()
				}
				
			// Any other transition (disconnect / error) is treated as a failure
            // for the current operation. The app can decide whether to retry.
			} else {
				fail("Connection state=$newState status=$status")
			}
		}

        override fun onServicesDiscovered(g: android.bluetooth.BluetoothGatt, status: Int) {
            if (status == android.bluetooth.BluetoothGatt.GATT_SUCCESS) {
				// Service discovery succeeded – mark as ready
				discovered = true  
		
                // Look up the Nordic UART (NUS) service and its characteristics.
                // TX = write characteristic, RX = notify characteristic.
                val svc = g.getService(BleHub.SERVICE_UUID)
                lastCharacteristic = svc?.getCharacteristic(BleHub.CHAR_UUID) // TX (write)
                notifyCharacteristic = svc?.getCharacteristic(BleHub.RX_UUID) // RX (notify)

                if (notifyCharacteristic != null) {
                    // Enable notifications on RX
                    g.setCharacteristicNotification(notifyCharacteristic, true)
                    val cccd = notifyCharacteristic!!.getDescriptor(
                        java.util.UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
                    )
                    if (cccd != null) {
                        cccd.value = android.bluetooth.BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                        // Write descriptor - completion handled in onDescriptorWrite
                        if (!g.writeDescriptor(cccd)) {
                            // Could not write CCCD - still try to continue
                            notificationsEnabled = false
                            succeed()
                        }
                    } else {
                        // No CCCD available - continue anyway
                        notificationsEnabled = false
                        succeed()
                    }
                } else {
                    // No RX, but we can still write-only
                    notificationsEnabled = false
                    succeed()
                }
            } else {
                fail("Service discovery failed status=$status")
            }
        }

        override fun onDescriptorWrite(
            g: android.bluetooth.BluetoothGatt,
            descriptor: android.bluetooth.BluetoothGattDescriptor,
            status: Int
        ) {
            // We don't fail the connect on CCCD errors – we just track whether
            // notifications are actually enabled and complete the connect().			
            if (status == android.bluetooth.BluetoothGatt.GATT_SUCCESS &&
                descriptor.characteristic == notifyCharacteristic) {
                notificationsEnabled = true
            }
            // Signal connect() completion (do not close)
            succeed()
        }

		override fun onCharacteristicChanged(
			g: android.bluetooth.BluetoothGatt,
			characteristic: android.bluetooth.BluetoothGattCharacteristic
		) {
            // Incoming notification from RX characteristic:
            //
            // 1) If a streaming listener is active -> deliver there and return.
            // 2) Else if a one-shot listener is waiting -> deliver once and clear.
            // 3) Else buffer the data for future consumers.			
			if (characteristic == notifyCharacteristic) {
				val data = characteristic.value

				// 1) if a stream is active, deliver there (do NOT consume one-shot)
				streamListener?.let { streamCb ->
					streamCb(data)
					return
				}

				// 2) otherwise deliver to one-shot waiter if present
				val cb = notifListener
				if (cb != null) {
					notifListener = null
					try { notifTimeouts.removeCallbacksAndMessages(null) } catch (_: Throwable) {}
					cb(data)
				} else {
					// 3) no one is listening -> buffer for the next waiter
					synchronized(notifBuffer) { notifBuffer.addLast(data.copyOf()) }
				}
			}
		}

		override fun onMtuChanged(
			g: android.bluetooth.BluetoothGatt,
			mtu: Int,
			status: Int
		) {
            // Record negotiated MTU and continue with service discovery.
            // We ignore status here and always try to discover services.			
			currentMtu = mtu
			// Continue to service discovery after MTU negotiation
			try { g.discoverServices() } catch (_: Throwable) {}
		}

		override fun onCharacteristicWrite(
			g: android.bluetooth.BluetoothGatt,
			characteristic: android.bluetooth.BluetoothGattCharacteristic,
			status: Int
		) {
            // Write completed (with or without response).
            // Clear the write timeout and signal success/failure for
            // the current operation.			
			main.removeCallbacks(timeoutRunnable)
			if (status == android.bluetooth.BluetoothGatt.GATT_SUCCESS) succeed()
			else fail("Write failed status=$status")
		}
	}

	////////////////////////////////////////////////////////////////////
	// writeOrConnect()
	//
	// High-level helper used by BleHub to send a payload to a given
	// [serviceUuid]/[characteristicUuid] on [address].
	//
	// Behaviour:
	//  - If we are already connected to [address] (gatt != null and
	//    connectedAddress matches), write immediately.
	//  - Otherwise, connect() first, then perform the write.
	//
	// The write is done in "persistent" mode: we leave the GATT open
	// (closeAfterOp = false) so multiple app commands can share the
	// same connection.
	//
	// writeType:
	//   - Defaults to WRITE_TYPE_DEFAULT.
	//   - If the characteristic only supports WRITE_NO_RESPONSE,
	//     we switch to WRITE_TYPE_NO_RESPONSE and complete immediately
	//     without waiting for onCharacteristicWrite().
	////////////////////////////////////////////////////////////////////
	@android.annotation.SuppressLint("MissingPermission")
	@androidx.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
	fun writeOrConnect(
		address: String,
		serviceUuid: java.util.UUID,
		characteristicUuid: java.util.UUID,
		payload: ByteArray,
		writeType: Int = android.bluetooth.BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
		onResult: (Boolean, String?) -> Unit
	) {
		//resultCb = onResult
		closeAfterOp = false // do not close after write — persistent mode

		var wroteOnce = false
		val doWrite = fun() 
		{
			// guard: avoid duplicate writes if callback fires twice
			if (wroteOnce) return  
			wroteOnce = true
		
			val g = gatt
			if (g == null) {
				fail("Not connected")
				return
			}

			val service = g.getService(serviceUuid)
			val ch = service?.getCharacteristic(characteristicUuid)
			if (ch == null) {
				fail("Characteristic not found")
				return
			}
			lastCharacteristic = ch
			
			resultCb = onResult

			var resolvedWriteType = writeType
			val props = ch.properties
			val hasWriteResp = (props and android.bluetooth.BluetoothGattCharacteristic.PROPERTY_WRITE) != 0
			val hasWriteNoResp = (props and android.bluetooth.BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0
			
            // If caller picked WRITE_TYPE_DEFAULT, infer the most appropriate
            // write type based on the characteristic's properties.			
			if (resolvedWriteType == android.bluetooth.BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT &&
				!hasWriteResp && hasWriteNoResp) {
				resolvedWriteType = android.bluetooth.BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
			}
			val isNoResponse =
				(resolvedWriteType == android.bluetooth.BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)

            // API 33+ has the newer writeCharacteristic() API that returns
            // a BluetoothStatusCodes result; older APIs use the legacy
            // setter + boolean return value.
			if (android.os.Build.VERSION.SDK_INT >= 33) 
			{
				val rc = g.writeCharacteristic(ch, payload, resolvedWriteType)
				if (rc != android.bluetooth.BluetoothStatusCodes.SUCCESS) {
					fail("writeCharacteristic rc=$rc")
					return
				}
				if (isNoResponse) {
					succeed()
					return
				}
				
			} else 
			{
				ch.writeType = resolvedWriteType
				ch.value = payload
				if (!g.writeCharacteristic(ch)) {
					fail("writeCharacteristic returned false")
					return
				}
				if (isNoResponse) {
					succeed()
					return
				}
			}

            // For writes with response, arm the write timeout and wait
            // for onCharacteristicWrite() to complete the operation.
			// Wait for onCharacteristicWrite with a timeout
			main.removeCallbacks(timeoutRunnable)
			main.postDelayed(timeoutRunnable, writeTimeoutMs)
		}

        // Fast path: reuse existing live GATT to same address if available;
        // otherwise, connect first and then perform the write.
		if (gatt != null && connectedAddress == address && discovered) {
		//if (gatt != null && connectedAddress == address) {
			doWrite.invoke()
		} else {
			connect(address) { ok, err ->
				if (!ok) onResult(false, err) else doWrite.invoke()
			}
		}
	}

	////////////////////////////////////////////////////////////////////
	// Public "drop connection" API used by BleHub / Settings.
	//
	// Simply delegates to cleanupGatt(), which will disconnect/close
	// the GATT and reset all associated state.
	////////////////////////////////////////////////////////////////////
	fun disconnect() {
		cleanupGatt()
	}

	////////////////////////////////////////////////////////////////////
	// awaitNextNotification()
	//
	// Wait for a single notification on the RX characteristic.
	//
	// - If there is already buffered data, returns it immediately
	//   without starting a timeout.
	// - Otherwise, installs a one-shot listener and arms a timeout.
	//   On timeout, invokes callback with null.
	//
	// NOTE: If a streamListener is active, notifications will go there
	// instead and this waiter will never see them.
	////////////////////////////////////////////////////////////////////
	fun awaitNextNotification(timeoutMs: Long, onResult: (ByteArray?) -> Unit) 
	{
		// if something is already buffered, return it immediately
		synchronized(notifBuffer) {
			if (notifBuffer.isNotEmpty()) {
				onResult(notifBuffer.removeFirst())
				return
			}
		}
		// otherwise arm a one-shot listener + timeout
		notifListener = onResult
		notifTimeouts.postDelayed({
			val cb = notifListener
			notifListener = null
			cb?.invoke(null) // timeout
		}, timeoutMs)
	}
	
// end of class
}
