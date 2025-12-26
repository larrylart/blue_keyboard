import Foundation

struct BKFrame {
    let op: UInt8
    let payload: Data
}

final class BKFramer {
    private var buffer = Data()

    // Match sanity behavior (prevents ASCII/banner junk from looking like a huge "valid" LEN)
    private let maxLen = 1024

    private func rdU16le(_ d: Data, _ o: Int) -> Int {
        let b0 = Int(d[o])
        let b1 = Int(d[o+1])
        return b0 | (b1 << 8)
    }

    private func plausibleFrameAt(_ pos: Int) -> Bool {
        if pos + 3 > buffer.count { return false }
        let len = rdU16le(buffer, pos + 1)
        if len < 0 || len > maxLen { return false }
        return (pos + 3 + len) <= buffer.count
    }

	func push(_ chunk: Data) -> [BKFrame] {
		buffer.append(chunk)
		var out: [BKFrame] = []

		while true {
			if buffer.count < 3 { break }

			let op = buffer[0]
			let len = rdU16le(buffer, 1)

			// If LEN is insane, bulk-resync instead of removeFirst() loops
			if len < 0 || len > maxLen {
				// Look for next plausible frame start within a small window
				var found: Int? = nil
				let scanLimit = min(buffer.count - 2, 128) // don't scan forever

				if scanLimit >= 1 {
					for pos in 1..<scanLimit {
						// Optional: also require "binary-looking opcode"
						// ops are >= 0xA0 (A0/B0/B1/B2/B3/FF/E0/etc)
						if buffer[pos] >= 0x80, plausibleFrameAt(pos) {
							found = pos
							break
						}
					}
				}

				if let pos = found {
					buffer.removeSubrange(0..<pos)
				} else {
					// It's just garbage (e.g., plaintext banner). Drop it.
					buffer.removeAll(keepingCapacity: true)
					break
				}
				continue
			}

			let need = 3 + len
			if buffer.count < need { break }

			let payload = buffer.subdata(in: 3..<need)
			out.append(BKFrame(op: op, payload: payload))
			buffer.removeSubrange(0..<need)
		}

		return out
	}

    func reset() { buffer.removeAll(keepingCapacity: true) }
}
