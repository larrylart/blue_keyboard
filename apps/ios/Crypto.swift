import Foundation
import CryptoKit
import CommonCrypto

enum BKCrypto {
    static func md5(_ data: Data) -> Data {
        var digest = [UInt8](repeating: 0, count: Int(CC_MD5_DIGEST_LENGTH))
        data.withUnsafeBytes { ptr in
            _ = CC_MD5(ptr.baseAddress, CC_LONG(data.count), &digest)
        }
        return Data(digest)
    }

    static func hmacSha256(_ key: Data, _ msg: Data) -> Data {
        var mac = [UInt8](repeating: 0, count: Int(CC_SHA256_DIGEST_LENGTH))
        key.withUnsafeBytes { k in
            msg.withUnsafeBytes { m in
                CCHmac(CCHmacAlgorithm(kCCHmacAlgSHA256), k.baseAddress, key.count, m.baseAddress, msg.count, &mac)
            }
        }
        return Data(mac)
    }

    static func hmac16(_ key: Data, _ msg: Data) -> Data {
        return hmacSha256(key, msg).prefix(16)
    }

    static func pbkdf2Sha256(password: Data, salt: Data, iterations: Int, keyLen: Int) -> Data? {
        var out = [UInt8](repeating: 0, count: keyLen)
        let rc = out.withUnsafeMutableBytes { outPtr in
            password.withUnsafeBytes { pwPtr in
                salt.withUnsafeBytes { saltPtr in
                    CCKeyDerivationPBKDF(CCPBKDFAlgorithm(kCCPBKDF2),
                                         pwPtr.baseAddress!.assumingMemoryBound(to: Int8.self),
                                         password.count,
                                         saltPtr.baseAddress!.assumingMemoryBound(to: UInt8.self),
                                         salt.count,
                                         CCPseudoRandomAlgorithm(kCCPRFHmacAlgSHA256),
                                         UInt32(iterations),
                                         outPtr.baseAddress!.assumingMemoryBound(to: UInt8.self),
                                         keyLen)
                }
            }
        }
        return rc == kCCSuccess ? Data(out) : nil
    }

    /// AES-CTR with 256-bit key (sessKey). IV must be 16 bytes.
    static func aesCtr(key: Data, iv: Data, data: Data) -> Data? {
        guard key.count == 32, iv.count == 16 else { return nil }
        var out = [UInt8](repeating: 0, count: data.count)
        var numBytes: size_t = 0
        var cryptor: CCCryptorRef?

        let rc = key.withUnsafeBytes { k in
            iv.withUnsafeBytes { ivp in
                CCCryptorCreateWithMode(CCOperation(kCCEncrypt),
                                        CCMode(kCCModeCTR),
                                        CCAlgorithm(kCCAlgorithmAES),
                                        CCPadding(ccNoPadding),
                                        ivp.baseAddress,
                                        k.baseAddress,
                                        key.count,
                                        nil, 0, 0,
                                        CCModeOptions(kCCModeOptionCTR_BE),
                                        &cryptor)
            }
        }
        guard rc == kCCSuccess, let c = cryptor else { return nil }
        defer { CCCryptorRelease(c) }

        let outLen = out.count  // capture before exclusive access

        let rc2 = out.withUnsafeMutableBytes { outPtr in
            data.withUnsafeBytes { inPtr in
                CCCryptorUpdate(c,
                                inPtr.baseAddress, data.count,
                                outPtr.baseAddress, outLen,
                                &numBytes)
            }
        }

        guard rc2 == kCCSuccess else { return nil }
        return Data(out.prefix(numBytes))
    }

    static func hkdfSha256(salt: Data, ikm: Data, info: Data, outLen: Int = 32) -> Data {
        let key = SymmetricKey(data: ikm)
        // CryptoKit HKDF uses IKM as "inputKeyMaterial" and salt separately
        let derived = HKDF<SHA256>.deriveKey(inputKeyMaterial: key,
                                             salt: salt,
                                             info: info,
                                             outputByteCount: outLen)
        return Data(derived.withUnsafeBytes { Data($0) })
    }

    static func p256KeyAgreement() -> (priv: P256.KeyAgreement.PrivateKey, pubUncompressed65: Data) {
        let priv = P256.KeyAgreement.PrivateKey()
        let pub = priv.publicKey
        // ANSI X9.63 uncompressed: 0x04 || X || Y (32+32)
        let x963 = pub.x963Representation
        return (priv, x963)
    }

    static func ecdhSharedSecret(priv: P256.KeyAgreement.PrivateKey, srvPubUncompressed65: Data) -> Data? {
        guard let pub = try? P256.KeyAgreement.PublicKey(x963Representation: srvPubUncompressed65) else { return nil }
        guard let ss = try? priv.sharedSecretFromKeyAgreement(with: pub) else { return nil }
        // Use raw shared secret bytes (32)
        return ss.withUnsafeBytes { Data($0) }
    }
}
