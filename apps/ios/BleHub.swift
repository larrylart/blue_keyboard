import Foundation
import Combine
import CryptoKit

struct SentMessage: Identifiable {
    let id = UUID()
    let text: String
}

@MainActor
final class BleHub: ObservableObject {
    static let shared = BleHub()

    // UI sets this (SettingsView). BleHub calls it only when allowProvisioning == true.
    typealias PasswordPromptHandler = (_ reason: String, _ reply: @escaping (String?) -> Void) -> Void
    var passwordPromptHandler: PasswordPromptHandler?


    @Published private(set) var discoveredDevices: [DiscoveredDevice] = []
    @Published private(set) var isConnected: Bool = false
    @Published private(set) var isSecureConnected: Bool = false
    @Published private(set) var statusText: String = "Disconnected"

	//@Published var sentHistory: [String] = []
	@Published private(set) var sentHistory: [SentMessage] = [] // Change String to SentMessage

	@Published var lastError: String? = nil
	@Published var logLines: [String] = []

	/*private func log(_ s: String) {
		DispatchQueue.main.async {
			self.logLines.append(s)
			if self.logLines.count > 200 { self.logLines.removeFirst(self.logLines.count - 200) }
		}
	}*/
	
	private func log(_ s: String) {
		if Thread.isMainThread {
			logLines.append(s)
			if logLines.count > 200 { logLines.removeFirst(logLines.count - 200) }
		} else {
			DispatchQueue.main.async { [weak self] in
				self?.log(s)
			}
		}
	}

	private func fail(_ msg: String) {
		lastError = msg
		statusText = msg
		log("ERROR: \(msg)")
	}

	func showError(_ msg: String) {
		lastError = msg
	}

    private let mgr = BluetoothDeviceManager()
    private var cancellables = Set<AnyCancellable>()

    private let framer = BKFramer()

	// PERSISTENT RX STATE
    private var rxQueue: [BKFrame] = []
    private var rxWaiter: ((@MainActor (BKFrame) -> Bool, @MainActor (BKFrame?) -> Void))?

	// Force strict sequencing: only one "operation" in flight at a time
	private var cmdQueue: [() -> Void] = []
	private var cmdRunning = false

	private func enqueueCommand(_ name: String, _ work: @escaping (@escaping () -> Void) -> Void) {
		self.cmdQueue.append { [weak self] in
			guard let self else { return }
			self.cmdRunning = true
			self.log("CMD: begin \(name)")
			work {
				self.log("CMD: end \(name)")
				self.finishCommand()
			}
		}

		if !self.cmdRunning {
			self.cmdQueue.removeFirst()()
		}
	}

	private func finishCommand() {
		if self.cmdQueue.isEmpty {
			self.cmdRunning = false
			return
		}
		let next = self.cmdQueue.removeFirst()
		next()
	}

    // MTLS state
	private struct MtlsState {
		var sid: Int = 0
		var sessKey: Data? = nil  // 32

		// Derived from sessKey:
		// kEnc = HMAC(sessKey, "ENC")
		// kMac = HMAC(sessKey, "MAC")
		// kIv  = HMAC(sessKey, "IVK")
		var kEnc: Data? = nil     // 32
		var kMac: Data? = nil     // 32
		var kIv:  Data? = nil     // 32

		var seqOut: Int = 0
	}

    private var mtls: MtlsState? = nil
    private var fastKeysSessionEnabled: Bool = false

    private init() {
        mgr.$devices
            .receive(on: DispatchQueue.main)
            .assign(to: &$discoveredDevices)

        mgr.$isPoweredOn
            .receive(on: DispatchQueue.main)
            .sink { [weak self] on in
                self?.statusText = on ? "Bluetooth ready" : "Bluetooth off"
            }
            .store(in: &cancellables)

		mgr.$isConnected
			.receive(on: DispatchQueue.main)
			.sink { [weak self] on in
				self?.isConnected = on
				if !on {
					self?.isSecureConnected = false
					self?.statusText = "Disconnected"
				}
			}
			.store(in: &cancellables)
			
			mgr.logger = { [weak self] s in
				Task { @MainActor in
					self?.log(s)
				}
			}
		

//DispatchQueue.main.async {
//    self.sentHistory = ["BOOT TEST: if you don't see this, rendering is broken"]
//}		

//self.sentHistory = ["BOOT TEST: if you don't see this, rendering is broken"]

    }

    // MARK: - UI entrypoints

    func startScan() { mgr.startScan() }

	func startupAutoConnect() {
		enqueueCommand("startupAutoConnect") { finish in
			self.log("AUTO: startupAutoConnect useExternal=\(Preferences.useExternalKeyboardDevice) id=\(Preferences.selectedPeripheralId ?? "nil")")
			guard Preferences.useExternalKeyboardDevice else { finish(); return }
			guard let id = Preferences.selectedPeripheralId else { finish(); return }
			self.connectAndEstablishSecure(peripheralId: id, allowProvisioning: false) { _, _ in
				finish()
			}
		}
	}

	func connectSelectedDevice(_ cb: @escaping (Bool, String?) -> Void) {
		enqueueCommand("connectSelectedDevice") { finish in
			guard Preferences.useExternalKeyboardDevice else { cb(false, "Disabled in settings"); finish(); return }
			guard let id = Preferences.selectedPeripheralId else { cb(false, "No device selected"); finish(); return }

			self.connectAndEstablishSecure(peripheralId: id, allowProvisioning: false) { ok, err in
				cb(ok, err)
				finish()
			}
		}
	}

    /// "Settings connect" is allowed to run provisioning paths.
	func connectFromSettings(_ cb: @escaping (Bool, String?) -> Void) {
		enqueueCommand("connectFromSettings") { finish in
			self.log("UI: connectFromSettings id=\(Preferences.selectedPeripheralId ?? "nil")")
			guard Preferences.useExternalKeyboardDevice else { cb(false, "Disabled in settings"); finish(); return }
			guard let id = Preferences.selectedPeripheralId else { cb(false, "No device selected"); finish(); return }

			self.connectAndEstablishSecure(peripheralId: id, allowProvisioning: true) { ok, err in
				cb(ok, err)
				finish()
			}
		}
	}

	private func onSecureSessionReady(_ deviceId: String, _ done: @escaping (Bool, String?) -> Void) {
		// once MTLS is up, refresh layout and store it.
		//self.getLayout { ok, code, err in
		self.getLayoutInternal { ok, code, err in
			if ok, let code, !code.isEmpty {
				Preferences.keyboardLayout = code
			} else {
				self.log("CONNECT: getLayout failed after secure connect: \(err ?? "nil")")
			}
			
			// FIX: Mark session as fully secure/ready only AFTER layout fetch prevents race conditions
            DispatchQueue.main.async {
                self.isSecureConnected = true
                self.statusText = "Secure connected"
            }
			
			done(true, nil) // connection is still considered successful
		}
	}

	private func resetRxState() {
		rxQueue.removeAll()
		rxWaiter = nil
		framer.reset()
	}

	// Cancels any in-flight connect/handshake immediately.
	// Use this for Settings toggle OFF / device switching.
	@MainActor
	func disconnectNowAndCancelPendingWork() {
		// 1) Drop queued work and unblock the command runner
		cmdQueue.removeAll()
		cmdRunning = false

		// 2) Cancel CoreBluetooth connection attempt / active link immediately
		mgr.disconnect()

		// 3) Reset protocol state so new connects can start cleanly
		resetRxState()
		mtls = nil
		fastKeysSessionEnabled = false
		framer.reset()

		isConnected = false
		isSecureConnected = false
		statusText = "Disconnected"
	}

	private func disconnectInline() {
		mgr.disconnect()
		resetRxState()
		isConnected = false
		isSecureConnected = false
		statusText = "Disconnected"
		mtls = nil
		fastKeysSessionEnabled = false
		framer.reset()
	}

/*
	func disconnect() {
		enqueueCommand("disconnect") { finish in
			self.mgr.disconnect()
			self.resetRxState()

			self.isConnected = false
			self.isSecureConnected = false
			self.statusText = "Disconnected"
				
			self.mtls = nil
			self.fastKeysSessionEnabled = false
			self.framer.reset()
			finish()
		}
	}
*/

	// new fix
	func disconnect() {
		// If disconnect is called from UI, it must preempt.
		disconnectNowAndCancelPendingWork()
	}

    // Secure operations (C*/D*/etc.)

	func sendStringAwaitHash(_ value: String, dontVerify: Bool = false, timeoutMs: Int = 6000, _ cb: @escaping (Bool, String?) -> Void) {
		enqueueCommand("sendStringAwaitHash") { finish in
			self.ensureSecure(allowProvisioning: false) { okConn, errConn in
				guard okConn else { cb(false, errConn); finish(); return }

				let raw = Preferences.sendNewlineAfterText ? (value + "\n") : value
				let bytes = Data(raw.utf8)
				let expected = BKCrypto.md5(bytes)

				self.log("SEND: sendStringAwaitHash rawLen=\(bytes.count) expMD5=\(expected.map{String(format:"%02X",$0)}.joined())")
				self.sendAppFrame(op: 0xD0, payload: bytes) { ok, err in
					self.log("SEND: D0 write ok=\(ok) err=\(err ?? "nil")")
					if !ok { self.showError(err ?? "Send string failed"); cb(false, err); finish(); return }

					if dontVerify { cb(true, nil); finish(); return }

					self.awaitAppReply(timeoutMs: timeoutMs, expectOp: 0xD1) { pay in
						guard let pay, pay.count >= 16 else { cb(false, "No/Bad D1"); finish(); return }

						if pay.count == 16 {
							cb(pay == expected, pay == expected ? nil : "Hash mismatch")
							finish()
							return
						}

						let status = Int(pay[0])
						let got = pay.subdata(in: 1..<17)

						if status != 0 { cb(false, "Non-zero status"); finish(); return }
						cb(got == expected, got == expected ? nil : "Hash mismatch")
						finish()
					}
				}
			}
		}
	}


	func setLayoutString(_ layout: String, timeoutMs: Int = 4000, _ cb: @escaping (Bool, String?) -> Void) {
		enqueueCommand("setLayoutString") { finish in
			self.ensureSecure(allowProvisioning: false) { okConn, errConn in
				guard okConn else { cb(false, errConn); finish(); return }

				self.sendAppFrame(op: 0xC0, payload: Data(layout.utf8)) { ok, err in
					if !ok { cb(false, err); finish(); return }

					self.awaitAppReply(timeoutMs: timeoutMs, expectOp: 0x00) { pay in
						cb(pay != nil && pay!.isEmpty, pay == nil ? "No ACK" : nil)
						finish()
					}
				}
			}
		}
	}

	private func getLayoutInternal(timeoutMs: Int = 4000, _ cb: @escaping (Bool, String?, String?) -> Void) {
		self.ensureSecure(allowProvisioning: false) { okConn, errConn in
			guard okConn else { cb(false, nil, errConn); return }

			self.sendAppFrame(op: 0xC1, payload: Data()) { ok, err in
				if !ok { cb(false, nil, err); return }
				self.awaitAppReply(timeoutMs: timeoutMs, expectOp: 0xC2) { pay in
					guard let pay else { cb(false, nil, "No C2"); return }
					let s = String(data: pay, encoding: .utf8) ?? ""
					if let rng = s.range(of: "LAYOUT=") {
						let tail = s[rng.upperBound...]
						let code = tail.split(whereSeparator: { ch in
								ch == " " || ch == "\n" || ch == "\r" || ch == "\t" || ch == ";"
							})
							.first
							.map(String.init)

						cb(code != nil, code, code == nil ? "LAYOUT missing" : nil)
					} else {
						cb(false, nil, "Bad C2")
					}
				}
			}
		}
	}



	func getLayout(timeoutMs: Int = 4000, _ cb: @escaping (Bool, String?, String?) -> Void) {
		enqueueCommand("getLayout") { finish in
			self.ensureSecure(allowProvisioning: false) { okConn, errConn in
				guard okConn else { cb(false, nil, errConn); finish(); return }

				self.sendAppFrame(op: 0xC1, payload: Data()) { ok, err in
					if !ok { cb(false, nil, err); finish(); return }
					self.awaitAppReply(timeoutMs: timeoutMs, expectOp: 0xC2) { pay in
						guard let pay else { cb(false, nil, "No C2"); finish(); return }
						let s = String(data: pay, encoding: .utf8) ?? ""
						if let rng = s.range(of: "LAYOUT=") {
							let tail = s[rng.upperBound...]
							let code = tail.split(whereSeparator: { $0 == " " || $0 == "\n" || $0 == "\r" || $0 == "\t" }).first.map(String.init)
							cb(code != nil, code, code == nil ? "LAYOUT missing" : nil)
							finish()
						} else {
							cb(false, nil, "Bad C2")
							finish()
						}
					}
				}
			}
		}
	}

	func enableFastKeys(timeoutMs: Int = 4000, _ cb: @escaping (Bool, String?) -> Void) {
		enqueueCommand("enableFastKeys") { finish in
			if self.fastKeysSessionEnabled { cb(true, nil); finish(); return }

			self.ensureSecure(allowProvisioning: false) { okConn, errConn in
				guard okConn else { cb(false, errConn); finish(); return }

				self.sendAppFrame(op: 0xC8, payload: Data([0x01])) { ok, err in
					if !ok { cb(false, err); finish(); return }

					self.awaitAppReply(timeoutMs: timeoutMs, expectOp: 0x00) { pay in
						let okAck = (pay != nil && pay!.isEmpty)
						if okAck {
							self.fastKeysSessionEnabled = true
							cb(true, nil)
						} else {
							cb(false, "No ACK for fast keys")
						}
						finish()
					}
				}
			}
		}
	}

	func sendRawKeyTap(mods: Int, usage: Int, repeat rep: Int = 1, _ cb: @escaping (Bool, String?) -> Void) {
		enqueueCommand("sendRawKeyTap") { finish in
			self.ensureSecure(allowProvisioning: false) { okConn, errConn in
				guard okConn else { cb(false, errConn); finish(); return }
				guard self.fastKeysSessionEnabled else { cb(false, "Fast keys not enabled"); finish(); return }

				let r = UInt8(max(1, min(255, rep)))
				var payload = Data([UInt8(mods & 0xFF), UInt8(usage & 0xFF)])
				if r > 1 { payload.append(r) }

				self.sendRawFrame(op: 0xE0, payload: payload) { ok, err in
					cb(ok, err)
					finish()
				}
			}
		}
	}

    // MARK: - Provisioning (APPKEY)

	func provisionAppKey(password: String, timeoutMs: Int = 6000, _ cb: @escaping (Bool, String?) -> Void) {
		enqueueCommand("provisionAppKey") { finish in
			guard let id = Preferences.selectedPeripheralId else { cb(false, "No device selected"); finish(); return }

			self.connectAndEstablishSecure(peripheralId: id, allowProvisioning: true) { ok, err in
				if !ok && err != "unprovisioned" { cb(false, err); finish(); return }

				self.ensureAppKeyCachedBinary(deviceId: id, password: password, timeoutMs: timeoutMs, forceFetch: true) { ok2, err2 in
					if !ok2 { cb(false, err2); finish(); return }

					// inline disconnect logic (avoid re-entering queued disconnect)
					self.mgr.disconnect()
					self.resetRxState()
					DispatchQueue.main.async {
						self.isConnected = false
						self.isSecureConnected = false
						self.statusText = "Disconnected"
					}
					self.mtls = nil
					self.fastKeysSessionEnabled = false
					self.framer.reset()

					self.connectAndEstablishSecure(peripheralId: id, allowProvisioning: false) { ok3, err3 in
						cb(ok3, err3)
						finish()
					}
				}
			}
		}
	}

    private func ensureAppKeyCachedBinary(deviceId: String, password: String, timeoutMs: Int, forceFetch: Bool, _ cb: @escaping (Bool, String?) -> Void) {
        if !forceFetch, let existing = BleAppSec.getKey(deviceId: deviceId), existing.count == 32 {
            cb(true, nil); return
        }

        // A0 -> A2 challenge
		log("PROV: sending A0 (challenge request)")
		sendRawFrame(op: 0xA0, payload: Data(), control: true) { ok, err in
			Task { @MainActor in
				if !ok { cb(false, err); return }
				self.awaitNextFrame(timeoutMs: timeoutMs, predicate: { $0.op == 0xA2 || $0.op == 0xFF }) { f in
					guard let f else { cb(false, "No CHALLENGE"); return }
					if f.op == 0xFF { cb(false, self.mapAppKeyErrorFromDevice(f.payload)); return }
					guard f.payload.count == 36 else { cb(false, "Bad CHALLENGE"); return }

					let salt = f.payload.subdata(in: 0..<16)
					let iters = Int(UInt32(littleEndian: f.payload.subdata(in: 16..<20).withUnsafeBytes { $0.load(as: UInt32.self) }))
					let chal = f.payload.subdata(in: 20..<36)

					let msg = Data("APPKEY".utf8) + chal

					// raw
					let passRaw = Data(password.utf8)
					let passNorm = Data(password.trimmingCharacters(in: .whitespacesAndNewlines).precomposedStringWithCompatibilityMapping.utf8)

					guard let verifRaw = BKCrypto.pbkdf2Sha256(password: passRaw, salt: salt, iterations: max(1, iters), keyLen: 32),
						  let verifNorm = BKCrypto.pbkdf2Sha256(password: passNorm, salt: salt, iterations: max(1, iters), keyLen: 32)
					else {
						cb(false, "PBKDF2 failed"); return
					}

					let macRaw = BKCrypto.hmacSha256(verifRaw, msg)
					let macNorm = BKCrypto.hmacSha256(verifNorm, msg)

					func tryProof(_ mac: Data, verif: Data, chal: Data, then: @escaping (Bool, String?) -> Void) {
						Task { @MainActor in
							self.log("PROV: sending A3 (proof), mac16=\(mac.prefix(2).map{String(format:"%02X",$0)}.joined())..")

							self.sendRawFrame(op: 0xA3, payload: mac, control: true) { ok2, err2 in
								Task { @MainActor in
									if !ok2 { then(false, err2); return }

									self.awaitNextFrame(timeoutMs: timeoutMs, predicate: { $0.op == 0xA1 || $0.op == 0xFF }) { f2 in
										guard let f2 else { then(false, "No A1"); return }
										if f2.op == 0xFF { then(false, self.mapAppKeyErrorFromDevice(f2.payload)); return }

										if let key32 = self.tryUnwrapA1Maybe(verif: verif, chal: chal, payload: f2.payload) {
											_ = BleAppSec.putKey(deviceId: deviceId, key32: key32)
											then(true, nil)
										} else {
											then(false, "Proof rejected")
										}
									}
								}
							}
						}
					}

					// RAW first, then normalized retry (Android re-issues A0 for new challenge; we do same)
					tryProof(macRaw, verif: verifRaw, chal: chal) { okP, errP in
						if okP { cb(true, nil); return }
						// re-issue A0 for fresh challenge
						self.sendRawFrame(op: 0xA0, payload: Data(), control: true) { okR, errR in
							Task { @MainActor in
								if !okR { cb(false, errR); return }

								self.awaitNextFrame(timeoutMs: timeoutMs, predicate: { $0.op == 0xA2 || $0.op == 0xFF }) { fR in
									guard let fR else { cb(false, "No CHALLENGE (retry)"); return }
									if fR.op == 0xFF { cb(false, self.mapAppKeyErrorFromDevice(fR.payload)); return }
									guard fR.payload.count == 36 else { cb(false, "Bad CHALLENGE (retry)"); return }

									let saltR = fR.payload.subdata(in: 0..<16)
									let itersR = Int(UInt32(littleEndian: fR.payload.subdata(in: 16..<20).withUnsafeBytes { $0.load(as: UInt32.self) }))
									let chalR = fR.payload.subdata(in: 20..<36)
									let msgR = Data("APPKEY".utf8) + chalR

									guard let verifR = BKCrypto.pbkdf2Sha256(password: passNorm, salt: saltR, iterations: max(1, itersR), keyLen: 32) else {
										cb(false, "PBKDF2 failed"); return
									}

									let macR = BKCrypto.hmacSha256(verifR, msgR)
									tryProof(macR, verif: verifR, chal: chalR) { okFinal, errFinal in
										cb(okFinal, errFinal)
									}
								}
							}
						}

					}
				}
			}
        }
    }

    private func tryUnwrapA1Maybe(verif: Data, chal: Data, payload: Data) -> Data? {
        if payload.count == 32 { return payload }
        if payload.count == 48 {
            let cipher = payload.subdata(in: 0..<32)
            let macIn = payload.subdata(in: 32..<48)

            let wrapKey = BKCrypto.hmacSha256(verif, Data("AKWRAP".utf8) + chal)
            let macExp = BKCrypto.hmacSha256(wrapKey, Data("AKMAC".utf8) + chal + cipher).prefix(16)
            guard macExp == macIn else { return nil }

            let iv16 = BKCrypto.hmacSha256(verif, Data("AKIV".utf8) + chal).prefix(16)
            guard let plain = BKCrypto.aesCtr(key: wrapKey, iv: iv16, data: cipher), plain.count == 32 else { return nil }
            return plain
        }
        return nil
    }

    private func mapAppKeyErrorFromDevice(_ payload: Data) -> String {
        if payload.isEmpty { return "Device error" }
        let raw = String(data: payload, encoding: .utf8) ?? ""

		if raw.uppercased().contains("LOCKED_SINGLE_NEED_RESET") {
			return "Dongle is locked (single-app strict mode). To provision a new app you must factory reset the dongle."
		}
	
        if raw.contains("MULTI_APP_DISABLED") { return "Dongle does not allow multi-app provisioning. Reset APPKEY on dongle." }
        if raw.contains("BAD_PROOF") { return "Bad password / proof." }
        if raw.contains("NO_CHALLENGE") { return "Device refused challenge." }
        return raw.isEmpty ? "Device error" : raw
    }

    // MARK: - Core connect + handshake

	private func connectAndEstablishSecure(peripheralId: String, allowProvisioning: Bool, _ cb: @escaping (Bool, String?) -> Void) {
		DispatchQueue.main.async {
			self.statusText = "Connecting…"
			self.isConnected = false
			self.isSecureConnected = false
		}
		framer.reset()
		resetRxState()
		mgr.connect(peripheralId: peripheralId) { ok, err in
			// 1. Hop to Main Thread immediately so all logic below is thread-safe
			DispatchQueue.main.async {		
				if !ok {
					self.statusText = "Connect failed"
					cb(false, err)
					return
				}

				Preferences.selectedPeripheralId = peripheralId
				if let d = self.discoveredDevices.first(where: { $0.id == peripheralId }) {
					Preferences.selectedPeripheralName = d.name
				}
				//DispatchQueue.main.async {
					self.isConnected = true
					self.statusText = "Connected"
				//}

				self.log("BLEHUB: installing notification stream consumer")

				// FIX: Start the continuous stream HERE.
				// This ensures we never miss data between handshake steps.
				self.mgr.startNotificationStream { [weak self] chunk in
					guard let self else { return }   // <-- unwrap here

					Task { @MainActor in
						self.log("BLEHUB: onChunkReceived n=\(chunk.count) head=\(chunk.prefix(8).map { String(format:"%02X", $0) }.joined(separator:" "))")
						self.onChunkReceived(chunk)
					}
				}

				let hasLocalKey = (BleAppSec.getKey(deviceId: peripheralId) != nil)

				self.waitForB0(timeoutMs: hasLocalKey ? 6000 : 8000) { b0 in
					guard let b0 else {
						self.statusText = "No B0"
						cb(false, "No B0")
						return
					}

					// 1) If we have no local key => provisioning path
					if BleAppSec.getKey(deviceId: peripheralId) == nil {
						self.log("CONNECT: no local APPKEY -> prompt -> A0 provisioning")

						self.requestProvisionPasswordIfAllowed(
							allowProvisioning: allowProvisioning,
							reason: "This app is not provisioned for this dongle. Enter dongle password to provision APPKEY."
						) { pw in
							guard let pw else { cb(false, "Provision cancelled"); return }

							// Runs A0/A1/A2/A3 and stores key
							self.ensureAppKeyCachedBinary(
								deviceId: peripheralId,
								password: pw,
								timeoutMs: 6000,
								forceFetch: true
							) { okP, errP in
								if !okP { cb(false, errP); return }

								// After provisioning: reconnect and do B0/B1 handshake
								self.disconnectInline()
								self.connectAndEstablishSecure(peripheralId: peripheralId, allowProvisioning: false, cb)
							}
						}
						return
					}

					// 2) Local key exists => do handshake from this B0
					self.doBinaryHandshakeFromB0(deviceId: peripheralId, b0Payload: b0) { okH, errH in
						if okH {
							self.onSecureSessionReady(peripheralId, cb)
							return
						}

						// BADMAC -> clear key -> prompt -> provision -> reconnect
						let e = errH ?? "Handshake failed"
						let isBadMac =
							e.lowercased().contains("badmac") ||
							e.lowercased().contains("bad mac") ||
							e.lowercased().contains("sfin mismatch") ||
							e.lowercased().contains("bad key")

						if allowProvisioning && isBadMac {
							self.log("CONNECT: BADMAC-like handshake failure -> reprovision")
							BleAppSec.clearKey(deviceId: peripheralId)

							self.requestProvisionPasswordIfAllowed(
								allowProvisioning: true,
								reason: "Key mismatch. Enter dongle password to reprovision APPKEY."
							) { pw in
								guard let pw else { cb(false, "Provision cancelled"); return }

								self.ensureAppKeyCachedBinary(
									deviceId: peripheralId,
									password: pw,
									timeoutMs: 6000,
									forceFetch: true
								) { okP, errP in
									if !okP { cb(false, errP); return }
									self.disconnectInline()
									self.connectAndEstablishSecure(peripheralId: peripheralId, allowProvisioning: false, cb)
								}
							}
							return
						}

						self.isSecureConnected = false
						self.statusText = "Handshake failed"
						cb(false, errH)
					}
				}
			}
		}
	}

	private func waitForB0(timeoutMs: Int, _ onDone: @escaping (Data?) -> Void) {
		log("CONNECT: waitForB0 timeoutMs=\(timeoutMs)")

		self.awaitNextFrame(timeoutMs: timeoutMs, predicate: { $0.op == 0xB0 }) { f in
			guard let f else { onDone(nil); return }
			onDone(f.payload)
		}
	}

	// lookup text banner
	private func parsePlainInfo(_ bytes: Data) -> (String,String,String)? {
		let s = String(decoding: bytes, as: UTF8.self)
		let range = NSRange(s.startIndex..<s.endIndex, in: s)

		// 1) LAYOUT is required
		guard let layoutRe = try? NSRegularExpression(pattern: #"(?i)\bLAYOUT=([A-Z0-9_]+)\b"#),
			  let m1 = layoutRe.firstMatch(in: s, range: range),
			  let r1 = Range(m1.range(at: 1), in: s)
		else { return nil }

		let layout = String(s[r1])

		// 2) PROTO/FW are optional (empty if not present yet)
		func capture(_ pat: String) -> String {
			guard let re = try? NSRegularExpression(pattern: pat),
				  let m = re.firstMatch(in: s, range: range),
				  let r = Range(m.range(at: 1), in: s)
			else { return "" }
			return String(s[r])
		}

		let proto = capture(#"(?i)\bPROTO=([0-9.]+)\b"#)
		let fw    = capture(#"(?i)\bFW=([0-9.]+)\b"#)

		Preferences.keyboardLayout = layout
		return (layout, proto, fw)
	}

    // MARK: - Framing helpers

	private func sendRawFrame(op: UInt8, payload: Data, control: Bool = false, _ cb: @escaping (Bool, String?) -> Void) {
		var frame = Data([op])
		frame.append(UInt16(payload.count).littleEndianData)
		frame.append(payload)

		log("TX: op=0x\(String(format: "%02X", op)) len=\(payload.count) control=\(control)")
		if control {
			mgr.writeControl(frame, onDone: cb)
		} else {
			mgr.write(frame, onDone: cb)
		}
	}

	private func awaitNextFrame(
		timeoutMs: Int,
		predicate: @escaping @MainActor (BKFrame) -> Bool,
		_ cb: @escaping @MainActor (BKFrame?) -> Void
	)
	{

		// 1) If already queued, return immediately
		if let idx = rxQueue.firstIndex(where: predicate) {
			let f = rxQueue.remove(at: idx)
			cb(f)
			return
		}

		// 2) STRICT: no overlapping waits allowed
		if rxWaiter != nil {
			log("BUG: overlapping awaitNextFrame() - sequencing violated")
			cb(nil)
			return
		}

		// 3) Install waiter
		rxWaiter = (predicate, cb)

		// 4) Timeout (still on main actor)
		DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(timeoutMs)) { [weak self] in
			guard let self else { return }
			if self.rxWaiter != nil {
				self.rxWaiter = nil
				cb(nil)
			}
		}
	}

    // MARK: - MTLS handshake + B3

	private func doBinaryHandshakeFromB0(deviceId: String, b0Payload: Data, _ cb: @escaping (Bool, String?) -> Void) {

		// B0 = srvPub65(65) || sidLE32(4)
		guard b0Payload.count == 69 else {
			cb(false, "Bad B0 len=\(b0Payload.count)")
			return
		}

		let srvPub65 = b0Payload.subdata(in: 0..<65)

		// Dongle format: B0 = srvPub65(65) || sidBE32(4)
		let sidBE = b0Payload.subdata(in: 65..<69)
		let sid = Int(UInt32(bigEndian: sidBE.withUnsafeBytes { $0.load(as: UInt32.self) }))

		guard let appKey = BleAppSec.getKey(deviceId: deviceId), appKey.count == 32 else {
			cb(false, "APPKEY missing")
			return
		}

		// Generate ephemeral client P-256 keypair
		let (priv, cliPub65) = BKCrypto.p256KeyAgreement()

		// B1 mac16 = HMAC16(APPKEY, "KEYX" || sidBE || srvPub65 || cliPub65)
		let keyxMsg = Data("KEYX".utf8) + sidBE + srvPub65 + cliPub65
		let mac16 = BKCrypto.hmac16(appKey, keyxMsg)

		let b1Payload = cliPub65 + mac16

		log("MTLS: got B0 sid=\(sid) srvPub[0]=0x\(String(format:"%02X", srvPub65.first ?? 0))")
		log("MTLS: sending B1 (cliPub+mac16)")
		
		// clean notification buffer before sending ????
		//self.mgr.clearNotifBuffer() 

		self.sendRawFrame(op: 0xB1, payload: b1Payload, control: true) { ok, err in
			if !ok { cb(false, err); return }

			self.awaitNextFrame(timeoutMs: 6000, predicate: { $0.op == 0xB2 || $0.op == 0xFF }) { f in
				guard let f else { cb(false, "B2 timeout"); return }
				
				self.log("MTLS: got B2 len=\(f.payload.count)")
				
				if f.op == 0xFF {
					cb(false, self.mapAppKeyErrorFromDevice(f.payload))
					return
				}
				guard f.payload.count == 16 else { cb(false, "Bad B2"); return }

				// shared = ECDH(priv, srvPub65)
				guard let shared = BKCrypto.ecdhSharedSecret(priv: priv, srvPubUncompressed65: srvPub65) else {
					cb(false, "ECDH failed"); return
				}

				// sess = HKDF-SHA256(salt=APPKEY, ikm=shared, info="MT1"||sidBE||srvPub65||cliPub65)
				let sidBE32 = UInt32(sid).bigEndianData
				let info = Data("MT1".utf8) + sidBE32 + srvPub65 + cliPub65
				let sessKey = BKCrypto.hkdfSha256(salt: appKey, ikm: shared, info: info, outLen: 32)

				// Derive per-purpose keys (match dongle mtls.cpp)
				let kEnc = BKCrypto.hmacSha256(sessKey, Data("ENC".utf8))          // 32
				let kMac = BKCrypto.hmacSha256(sessKey, Data("MAC".utf8))          // 32
				let kIv  = BKCrypto.hmacSha256(sessKey, Data("IVK".utf8))          // 32

				// Verify B2 as HMAC16(kMac, "SFIN"||sidBE||srvPub65||cliPub65)
				let sfinMsg = Data("SFIN".utf8) + sidBE + srvPub65 + cliPub65
				let exp = BKCrypto.hmac16(kMac, sfinMsg)

				guard exp == f.payload else {
					self.log("MTLS: SFIN mismatch")
					cb(false, "SFIN mismatch")
					return
				}

				self.mtls = MtlsState(sid: sid, sessKey: sessKey, kEnc: kEnc, kMac: kMac, kIv: kIv, seqOut: 0)
				self.fastKeysSessionEnabled = false
				cb(true, nil)

			}
		}
	}

	private func ensureSecure(allowProvisioning: Bool, _ cb: @escaping (Bool, String?) -> Void) {
		if mtls?.sessKey != nil {
			cb(true, nil)
			return
		}
		guard Preferences.useExternalKeyboardDevice else { cb(false, "Disabled in settings"); return }
		guard let id = Preferences.selectedPeripheralId else { cb(false, "No device selected"); return }

		connectAndEstablishSecure(peripheralId: id, allowProvisioning: allowProvisioning) { ok, err in
			cb(ok, err)
		}
	}

    
	private func sendAppFrame(op: UInt8, payload: Data, _ cb: @escaping (Bool, String?) -> Void) {
		guard mtls?.sessKey != nil else {
			cb(false, "Not secure (MTLS not established)")
			return
		}
		// Build inner [op][len_le16][payload]
		var inner = Data([op])
		inner.append(UInt16(payload.count).littleEndianData)
		inner.append(payload)

		let b3 = wrapB3(inner)
		mgr.write(b3, onDone: cb)
	}


    private func awaitAppReply(timeoutMs: Int, expectOp: UInt8, _ cb: @escaping (Data?) -> Void) {
        // Pull frames until we get a B3 that decrypts to inner op = expectOp
        let start = Date()
        func timeLeft() -> Int {
            max(0, timeoutMs - Int(Date().timeIntervalSince(start) * 1000))
        }

        func step() {
            let tl = timeLeft()
            if tl <= 0 { cb(nil); return }
            self.awaitNextFrame(timeoutMs: tl, predicate: { $0.op == 0xB3 }) { f in
                guard let f else { cb(nil); return }
                if let inner = self.unwrapB3(f.payload),
                   inner.count >= 3,
                   inner[0] == expectOp {
                    let len = Int(UInt16(littleEndian: inner.subdata(in: 1..<3).withUnsafeBytes { $0.load(as: UInt16.self) }))
                    if inner.count >= 3+len {
                        cb(inner.subdata(in: 3..<(3+len)))
                        return
                    }
                }
                // otherwise keep waiting
                step()
            }
        }
        step()
    }

    private func wrapB3(_ plainAppFrame: Data) -> Data {
		guard var st = mtls,
			  let kEnc = st.kEnc,
			  let kMac = st.kMac,
			  let kIv  = st.kIv
		else { return Data() }

		let seq = st.seqOut & 0xFFFF
		let dir: UInt8 = 0x43 // 'C'

		let iv = mtlsIv(key: kIv, sid: st.sid, dir: dir, seq: seq)
		let enc = BKCrypto.aesCtr(key: kEnc, iv: iv, data: plainAppFrame) ?? Data()

		let macData =
			Data("ENCM".utf8)
			+ UInt32(st.sid).bigEndianData
			+ Data([dir])
			+ UInt16(seq).bigEndianData
			+ enc

		let mac = BKCrypto.hmac16(kMac, macData)

		// Dongle B3 header uses BIG-endian seq/clen
		var pay = Data()
		pay.append(UInt16(seq).bigEndianData)
		pay.append(UInt16(enc.count).bigEndianData)
		pay.append(enc)
		pay.append(mac)

		// update state
		st.seqOut = (seq + 1) & 0xFFFF
		mtls = st

		// Top-level framing must match dongle: [0xB3][LEN_le16][payload]
		var out = Data([0xB3])
		out.append(UInt16(pay.count).littleEndianData)
		out.append(pay)
		return out

    }

    private func unwrapB3(_ payload: Data) -> Data? {
		// payload: seq_be16 | encLen_be16 | enc | mac16
		guard let st = mtls,
			  let kEnc = st.kEnc,
			  let kMac = st.kMac,
			  let kIv  = st.kIv
		else { return nil }

		guard payload.count >= 2+2+16 else { return nil }

		let seq = Int(UInt16(bigEndian: payload.subdata(in: 0..<2).withUnsafeBytes { $0.load(as: UInt16.self) }))
		let encLen = Int(UInt16(bigEndian: payload.subdata(in: 2..<4).withUnsafeBytes { $0.load(as: UInt16.self) }))

		guard payload.count >= 4 + encLen + 16 else { return nil }
		let enc = payload.subdata(in: 4..<(4+encLen))
		let macIn = payload.subdata(in: (4+encLen)..<(4+encLen+16))

		let dir: UInt8 = 0x53 // 'S'

		let macData =
			Data("ENCM".utf8)
			+ UInt32(st.sid).bigEndianData
			+ Data([dir])
			+ UInt16(seq).bigEndianData
			+ enc

		let macExp = BKCrypto.hmac16(kMac, macData)
		guard macExp == macIn else { return nil }

		let iv = mtlsIv(key: kIv, sid: st.sid, dir: dir, seq: seq)
		let plain = BKCrypto.aesCtr(key: kEnc, iv: iv, data: enc) ?? Data()
		return plain
    }

	func mtlsIv(key: Data, sid: Int, dir: UInt8, seq: Int) -> Data {
		// Dongle: HMAC(kIv, "IV1" || sidBE32 || dir || seqBE16)[0..15]
		let msg = Data("IV1".utf8)
			+ UInt32(sid).bigEndianData
			+ Data([dir])
			+ UInt16(seq).bigEndianData
		return BKCrypto.hmacSha256(key, msg).prefix(16)
	}
	
	private func requestProvisionPasswordIfAllowed(
		allowProvisioning: Bool,
		reason: String,
		_ cb: @escaping (String?) -> Void
	) {
		guard allowProvisioning, let handler = passwordPromptHandler else {
			cb(nil)
			return
		}
		handler(reason, cb)
	}
	
	// new streaming function
	// Centralized handler for ALL incoming BLE data
	private func onChunkReceived(_ chunk: Data) {
		guard !chunk.isEmpty else { return }

		// Binary framing path
		let frames = framer.push(chunk)

		for f in frames {
			if let (predicate, callback) = rxWaiter, predicate(f) {
				rxWaiter = nil
				callback(f)
				continue
			}
			rxQueue.append(f)
		}
	}
	
	func addToHistory(_ msg: String) {
		let newMessage = SentMessage(text: msg)
		self.sentHistory.append(newMessage)
		if self.sentHistory.count > 2000 { self.sentHistory.removeFirst() }
	}	
}

// External send (Share Extension)
extension BleHub {
	func sendTextFromShareExtension(_ text: String) {
		let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
		guard !trimmed.isEmpty else { return }

		// Do NOT allow provisioning UI here
		connectSelectedDevice { ok, err in
			if !ok {
				self.showError(err ?? "Connect failed")
				return
			}

			self.sendStringAwaitHash(trimmed) { ok2, err2 in
				if !ok2 {
					self.showError(err2 ?? "Send failed")
				}
			}
		}
	}
}

private extension FixedWidthInteger {
    var littleEndianData: Data {
        withUnsafeBytes(of: self.littleEndian) { Data($0) }
    }
    var bigEndianData: Data {
        withUnsafeBytes(of: self.bigEndian) { Data($0) }
    }
}

private extension UInt8 {
    init(ascii: Character) {
        self = UInt8(String(ascii).utf8.first!)
    }
}
