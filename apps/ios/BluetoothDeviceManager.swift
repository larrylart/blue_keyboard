

import Foundation
import CoreBluetooth
import Combine
import Dispatch

struct DiscoveredDevice: Identifiable, Equatable {
    let id: String          // peripheral.identifier.uuidString
    let name: String
    let rssi: Int
    fileprivate let peripheral: CBPeripheral
}

final class BluetoothDeviceManager: NSObject, ObservableObject {
    // Nordic UART
    let serviceUUID = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    let txUUID      = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E") // write
    let rxUUID      = CBUUID(string: "6E400003-B5A3-F393-E0A9-E50E24DCCA9E") // notify

    @Published private(set) var devices: [DiscoveredDevice] = []
    @Published private(set) var isPoweredOn: Bool = false
	@Published private(set) var isConnected: Bool = false

	private let centralQueue = DispatchQueue(label: "bk.central")
	// Queue-specific marker to avoid centralQueue.sync deadlocks
	private let queueKey = DispatchSpecificKey<UInt8>()
	private let queueToken: UInt8 = 1
	
    private var central: CBCentralManager!
    private var map: [UUID: DiscoveredDevice] = [:]
	private var order: [UUID] = []
	
    private var activePeripheral: CBPeripheral?
    private var txChar: CBCharacteristic?
    private var rxChar: CBCharacteristic?

    private var connectCb: ((Bool, String?) -> Void)?
    private var writeCb: ((Bool, String?) -> Void)?

    // Notification delivery
    private var oneShotNotif: ((Data?) -> Void)?
    private var notifBuffer: [Data] = []
    private var streamCb: ((Data) -> Void)?

	private var writeQueue: [Data] = []
	private var writeInFlight = false
	private var writeType: CBCharacteristicWriteType = .withResponse

	// Optional logger hook for UI (BleHub sets this)
	var logger: ((String) -> Void)?

	private func log(_ s: String) {
		logger?(s)
	}

	private func deliverMain(_ fn: @escaping () -> Void) {
		if Thread.isMainThread { fn() }
		else { DispatchQueue.main.async(execute: fn) }
	}

	private func finishConnect(_ ok: Bool, _ msg: String?) {
		let cb = connectCb
		connectCb = nil
		guard let cb else { return }
		deliverMain { cb(ok, msg) }
	}

	private func finishWrite(_ ok: Bool, _ msg: String?) {
		let cb = writeCb
		writeCb = nil
		guard let cb else { return }
		deliverMain { cb(ok, msg) }
	}

	private func mapCBError(_ error: Error) -> String {
		let ns = error as NSError
		if ns.domain == CBErrorDomain, let code = CBError.Code(rawValue: ns.code) {
			switch code {
			case .peerRemovedPairingInformation:
				return "Peer removed pairing information. iPhone: Settings → Bluetooth → (i) → Forget This Device. Then clear bonds on dongle and pair again."
			case .notConnected:
				return "Not connected (CBError.notConnected)"
			case .connectionTimeout:
				return "Connection timeout"
			case .connectionFailed:
				return "Connection failed"
			default:
				return "CBError(\(code.rawValue)): \(error.localizedDescription)"
			}
		}
		return error.localizedDescription
	}

    override init() {
        super.init()
        //central = CBCentralManager(delegate: self, queue: DispatchQueue(label: "bk.central"))
		//central = CBCentralManager(delegate: self, queue: centralQueue)

		// mark the queue so we can detect "already on centralQueue"
		centralQueue.setSpecific(key: queueKey, value: queueToken)
		central = CBCentralManager(delegate: self, queue: centralQueue)	
    }

	func startScan() {
		centralQueue.async { [weak self] in
			guard let self else { return }
			guard self.isPoweredOn else { return }

			self.map.removeAll()
			self.order.removeAll()
			DispatchQueue.main.async { self.devices = [] }

			self.central.scanForPeripherals(withServices: [self.serviceUUID], options: [
				CBCentralManagerScanOptionAllowDuplicatesKey: true
			])
		}
	}

	func stopScan() {
		centralQueue.async { [weak self] in
			self?.central.stopScan()
		}
	}


	func disconnect() {
		centralQueue.async { [weak self] in
			guard let self else { return }
			if let p = self.activePeripheral {
				self.central.cancelPeripheralConnection(p)
				// DON'T cleanup here; let didDisconnectPeripheral do it
				return				
			}
			self.cleanupUnlocked()
		}
	}

	private func cleanupUnlocked() {
		activePeripheral = nil
		txChar = nil
		rxChar = nil
		connectCb = nil
		writeCb = nil
		oneShotNotif = nil
		streamCb = nil
		notifBuffer.removeAll()
		writeQueue.removeAll()
		writeInFlight = false
	}

	private func cleanup() {
		// If we're already on centralQueue, don't sync (would deadlock)
		if DispatchQueue.getSpecific(key: queueKey) == queueToken {
			cleanupUnlocked()
		} else {
			centralQueue.sync { [weak self] in
				self?.cleanupUnlocked()
			}
		}
	}

	func connect(peripheralId: String, onDone: @escaping (Bool, String?) -> Void) 
	{
		centralQueue.async { [weak self] in
			guard let self else { return }

			// Capture old BEFORE cleanup wipes it
			let old = self.activePeripheral

			// Clear state for the new attempt (but don't lose the old reference)
			self.cleanupUnlocked()

			if let old {
				self.log("BLE: cancelling previous peripheral instance \(old.identifier.uuidString)")
				self.central.cancelPeripheralConnection(old)
				// don't return here - (CoreBluetooth will handle the old disconnect async)
			}

			guard let uuid = UUID(uuidString: peripheralId) else {
				DispatchQueue.main.async { onDone(false, "Bad device id") }
				return
			}

			let hits = self.central.retrievePeripherals(withIdentifiers: [uuid])
			guard let p = hits.first else {
				DispatchQueue.main.async { onDone(false, "Peripheral not found (retrievePeripherals)") }
				return
			}

			self.activePeripheral = p
			p.delegate = self

			self.connectCb = onDone
			self.log("BLE: connect \(p.identifier.uuidString)")
			self.central.connect(p, options: nil)
		}
	}

	func write(_ data: Data, onDone: @escaping (Bool, String?) -> Void) {
		centralQueue.async { [weak self] in
			guard let self else { return }
			guard let p = self.activePeripheral, let tx = self.txChar else {
				DispatchQueue.main.async { onDone(false, "Not connected") }
				return
			}

			let props = tx.properties
			self.writeType = props.contains(.writeWithoutResponse) ? .withoutResponse : .withResponse

			let maxLen = p.maximumWriteValueLength(for: self.writeType)
			if maxLen <= 0 {
				DispatchQueue.main.async { onDone(false, "Bad MTU/write length") }
				return
			}

			guard data.count <= maxLen else {
				DispatchQueue.main.async {
					onDone(false, "Frame too large for negotiated MTU (need >= \(data.count), have \(maxLen)).")
				}
				return
			}

			self.writeCb = onDone
			p.writeValue(data, for: tx, type: self.writeType)

			if self.writeType == .withoutResponse {
				finishWrite(true, nil)
			}

		}
	}


	func writeControl(_ data: Data, onDone: @escaping (Bool, String?) -> Void) {
		centralQueue.async { [weak self] in
			guard let self else { return }

			guard let p = self.activePeripheral, let tx = self.txChar else {
				DispatchQueue.main.async { onDone(false, "Not connected") }
				return
			}

			self.writeType = .withResponse
			let maxLen = p.maximumWriteValueLength(for: .withResponse)
			if maxLen <= 0 {
				DispatchQueue.main.async { onDone(false, "Bad MTU/write length") }
				return
			}

			guard data.count <= maxLen else {
				DispatchQueue.main.async {
					onDone(false, "Control frame too large for negotiated MTU (need >= \(data.count), have \(maxLen)).")
				}
				return
			}

			self.writeCb = onDone
			p.writeValue(data, for: tx, type: .withResponse)
		}
	}


	private func drainWriteQueue() {
		guard let p = activePeripheral, let tx = txChar else { return }
		guard !writeInFlight else { return }

		if writeQueue.isEmpty {
			finishWrite(true, nil)
			return
		}

		let chunk = writeQueue.removeFirst()

		if writeType == .withoutResponse {
			// FLOW CONTROL: don't flood
			if !p.canSendWriteWithoutResponse {
				// put chunk back and wait for peripheralIsReady(...)
				writeQueue.insert(chunk, at: 0)
				return
			}
			p.writeValue(chunk, for: tx, type: .withoutResponse)
			// Do NOT recurse blindly; try to send next chunk only if still allowed
			centralQueue.async { [weak self] in self?.drainWriteQueue() }
			return
		}

		// withResponse path (unchanged)
		writeInFlight = true
		p.writeValue(chunk, for: tx, type: .withResponse)
	}

	func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
		drainWriteQueue()
	}


	func awaitNextNotification(timeoutMs: Int, onDone: @escaping (Data?) -> Void) {
		centralQueue.async { [weak self] in
			guard let self else { return }

			// If buffered, deliver immediately
			if !self.notifBuffer.isEmpty {
				let d = self.notifBuffer.removeFirst()
				DispatchQueue.main.async { onDone(d) }
				return
			}

			// Register one-shot waiter
			self.oneShotNotif = { data in
				DispatchQueue.main.async { onDone(data) }
			}

			// Timeout (also scheduled onto centralQueue so state is consistent)
			self.centralQueue.asyncAfter(deadline: .now() + .milliseconds(timeoutMs)) { [weak self] in
				guard let self else { return }
				if let cb = self.oneShotNotif {
					self.oneShotNotif = nil
					cb(nil)
				}
			}
		}
	}

/*	func startNotificationStream(_ onChunk: @escaping (Data) -> Void) {
		// Stream mode owns notifications; cancel any pending one-shot waiter
		oneShotNotif = nil
		//streamCb = onChunk

		// run on main thread
		streamCb = { data in
			DispatchQueue.main.async { onChunk(data) }
		}

		// Flush buffered
		//while !notifBuffer.isEmpty {
		//	onChunk(notifBuffer.removeFirst())
		//}
		

		// run on main thread
		while !notifBuffer.isEmpty {
			let d = notifBuffer.removeFirst()
			DispatchQueue.main.async { onChunk(d) }
		}
	
	}
*/

	// take 2 - better?
	func startNotificationStream(_ onChunk: @escaping (Data) -> Void) {
		centralQueue.async { [weak self] in
			guard let self else { return }

			// Stream callback (deliver to main)
			self.streamCb = { data in
				DispatchQueue.main.async { onChunk(data) }
			}

			// Cancel one-shot waiter to avoid conflicts
			self.oneShotNotif = nil

			// Flush buffered safely (we are already on centralQueue)
			let queuedData = self.notifBuffer
			self.notifBuffer.removeAll()

			if !queuedData.isEmpty {
				DispatchQueue.main.async {
					for chunk in queuedData { onChunk(chunk) }
				}
			}
		}
	}

	func stopNotificationStream() {
		centralQueue.async { [weak self] in
			self?.streamCb = nil
			self?.oneShotNotif = nil
		}
	}

	func clearNotifBuffer() {
		centralQueue.async { [weak self] in
			self?.notifBuffer.removeAll()
			self?.oneShotNotif = nil
		}
	}

}

extension BluetoothDeviceManager: CBCentralManagerDelegate {
	func centralManagerDidUpdateState(_ central: CBCentralManager) {
		let on = (central.state == .poweredOn)
		DispatchQueue.main.async { [weak self] in
			self?.isPoweredOn = on
		}
		if !on {
			stopScan()
			disconnect()
		}
	}

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String : Any],
                        rssi RSSI: NSNumber) {
        let name = peripheral.name ?? (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? "Unknown"
        let dev = DiscoveredDevice(id: peripheral.identifier.uuidString,
                                   name: name,
                                   rssi: RSSI.intValue,
                                   peripheral: peripheral)
								   
        /*
		map[peripheral.identifier] = dev
        // publish sorted, stronger signal first
        let list = map.values.sorted { $0.rssi > $1.rssi }
        DispatchQueue.main.async { self.devices = list }
		*/
		
		let isNew = (map[peripheral.identifier] == nil)
		map[peripheral.identifier] = dev
		if isNew { order.append(peripheral.identifier) }

		// publish in stable order (no jumping while RSSI updates)
		let list = order.compactMap { map[$0] }
		DispatchQueue.main.async { self.devices = list }
		
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
		DispatchQueue.main.async { self.isConnected = true }
		log("BLE: didConnect \(peripheral.identifier.uuidString)")
		
		// flush cached services/characteristics so notify is re-established reliably
		peripheral.delegate = self
		//peripheral.services = nil		
		
        //peripheral.discoverServices([serviceUUID])
		log("BLE: discoverServices(nil)")
		peripheral.discoverServices(nil)
		
    }

	func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
		DispatchQueue.main.async { self.isConnected = false }
		let msg = error.map(mapCBError) ?? "Connect failed"
		log("BLE: didFailToConnect: \(msg)")
		finishConnect(false, msg)
	}


	func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
		DispatchQueue.main.async { self.isConnected = false }
		let msg = error.map(mapCBError) ?? "Disconnected"
		log("BLE: didDisconnect: \(msg)")

		// Only fail the connect callback if we were *still connecting*
		if connectCb != nil {
			finishConnect(false, msg)
		}
		cleanupUnlocked()
	}
}

extension BluetoothDeviceManager: CBPeripheralDelegate {

	func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
		if let error {
			let cb = connectCb; connectCb = nil
			let msg = mapCBError(error)
			log("BLE: didDiscoverServices ERROR: \(msg)")
			finishConnect(false, msg)
			return
		}

		let count = peripheral.services?.count ?? 0
		log("BLE: didDiscoverServices OK services=\(count)")

		guard let svc = peripheral.services?.first(where: { $0.uuid == serviceUUID }) else {
			let cb = connectCb; connectCb = nil
			log("BLE: service \(serviceUUID) not found (services=\(peripheral.services?.map{$0.uuid.uuidString} ?? []))")
			finishConnect(false, "Service not found")
			return
		}

		log("BLE: discovering characteristics for service \(svc.uuid)")
		peripheral.discoverCharacteristics([txUUID, rxUUID], for: svc)
	}

	func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
		if let error {
			let cb = connectCb; connectCb = nil
			let msg = mapCBError(error)
			log("BLE: didDiscoverCharacteristics ERROR: \(msg)")
			finishConnect(false, msg)
			return
		}

		let uuids = service.characteristics?.map { $0.uuid.uuidString } ?? []
		log("BLE: didDiscoverCharacteristics OK chars=\(uuids)")

		txChar = service.characteristics?.first(where: { $0.uuid == txUUID })
		rxChar = service.characteristics?.first(where: { $0.uuid == rxUUID })

		guard let rx = rxChar, let tx = txChar else {
			let cb = connectCb; connectCb = nil
			log("BLE: missing chars tx=\(txChar != nil) rx=\(rxChar != nil)")
			finishConnect(false, "Characteristics not found")
			return
		}

		log("BLE: enabling notify on RX, txProps=\(tx.properties)")
		//peripheral.setNotifyValue(true, for: rx)
		/* this causing instability in ble on ios 16
		if rx.isNotifying {
			log("BLE: RX already notifying -> toggle")
			peripheral.setNotifyValue(false, for: rx)
			DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
				peripheral.setNotifyValue(true, for: rx)
			}
		} else {
			peripheral.setNotifyValue(true, for: rx)
		}		
		*/
		
		if !rx.isNotifying {
			log("BLE: found rxChar uuid=\(rx.uuid.uuidString) props=\(rx.properties)")
			log("BLE: calling setNotifyValue(true) for RX")		
			peripheral.setNotifyValue(true, for: rx)
		} else {
			log("BLE: RX already notifying (leave it on)")
		}
		
	}


	func peripheral(_ peripheral: CBPeripheral,
					didUpdateNotificationStateFor characteristic: CBCharacteristic,
					error: Error?) {
		guard characteristic.uuid == rxUUID else { return }

		if let error {
			let msg = mapCBError(error)
			log("BLE: notify enable ERROR: \(msg)")
			finishConnect(false, msg)
			return
		}

		log("BLE: notify state OK isNotifying=\(characteristic.isNotifying)")
		guard characteristic.isNotifying else {
			finishConnect(false, "Notify not enabled")
			return
		}

		if characteristic.properties.contains(.read) {
			peripheral.readValue(for: characteristic)
		}

		DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) {
			self.finishConnect(true, nil)
		}
	}



	func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
		if let error {
			writeQueue.removeAll()
			writeInFlight = false
			let msg = mapCBError(error)
			log("BLE: didWriteValueFor error: \(msg)")
			finishWrite(false, msg)
			return
		}

		// withResponse: keep draining until empty, which calls finishWrite(true,nil)
		if writeType == .withResponse {
			writeInFlight = false
			drainWriteQueue()
			return
		}

		// fallback
		finishWrite(true, nil)

	}

	func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard characteristic.uuid == rxUUID else { return }

        if let error {
            log("BLE: RX error: \(mapCBError(error))")
            return
        }
        //guard let v = characteristic.value else { return }
		guard let v = characteristic.value, !v.isEmpty else { return }

        let head = v.prefix(16).map { String(format: "%02X", $0) }.joined(separator: " ")
        log("BLE: RX chunk n=\(v.count) head=\(head)")

        // FIX: Priority delivery. Do NOT buffer if a consumer exists.
        
		// 1) Stream listener (deliver on main)
		if let s = streamCb {
			deliverMain { s(v) }
			return
		}

		// 2) One-shot waiter (deliver on main)
		if let cb = oneShotNotif {
			oneShotNotif = nil
			deliverMain { cb(v) }
			return
		}
		
        // 3. Buffer if no active listeners
        notifBuffer.append(v)
        log("BLE: RX buffered (no stream/oneshot) n=\(v.count)")
    }

}
