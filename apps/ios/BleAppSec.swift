import Foundation
import Security

enum BleAppSec {
    private static let service = "com.example.BluKeyborgiOS.appkey"

    // MUST match Keychain Sharing capability in BOTH targets
    private static let accessGroup = "UT2AJJDNK6.com.blu.keyborg.ShareViewController"

    static func putKey(deviceId: String, key32: Data) -> Bool {
        guard key32.count == 32 else { return false }
        let account = slotId(deviceId)

        // Delete existing
        _ = SecItemDelete([
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: account,
			kSecAttrAccessGroup: accessGroup
        ] as CFDictionary)

        let status = SecItemAdd([
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: account,
			kSecAttrAccessGroup: accessGroup,
            kSecValueData: key32,
            kSecAttrAccessible: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        ] as CFDictionary, nil)

        return status == errSecSuccess
    }

    static func getKey(deviceId: String) -> Data? {
        let account = slotId(deviceId)
        var out: AnyObject?

        let status = SecItemCopyMatching([
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: account,
			kSecAttrAccessGroup: accessGroup,
            kSecReturnData: true,
            kSecMatchLimit: kSecMatchLimitOne
        ] as CFDictionary, &out)

        guard status == errSecSuccess else { return nil }
        return out as? Data
    }

    static func clearKey(deviceId: String) {
        let account = slotId(deviceId)
        _ = SecItemDelete([
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: account,
			kSecAttrAccessGroup: accessGroup
        ] as CFDictionary)
    }

    private static func slotId(_ deviceId: String) -> String {
        let d = SHA256.hash(data: Data(deviceId.trimmingCharacters(in: .whitespacesAndNewlines).lowercased().utf8))
        return d.prefix(16).map { String(format: "%02x", $0) }.joined()
    }
}

import CryptoKit
