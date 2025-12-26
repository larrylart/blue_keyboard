///////////////////////////////////////////////////////////////////////
// BleAppSec
//
// Secure storage for per-device 32-byte APPKEYs.
//
// Purpose:
//   - APPKEY is required for MTLS handshake with the dongle.
//   - We must NOT store APPKEY plaintext in SharedPreferences.
//   - Instead we encrypt the APPKEY with a per-device RSA keypair
//     stored inside AndroidKeyStore (private key is non-exportable).
//
// Compatibility:
//   - Supports Android 4.4 (minSdk 19) to Android 13+.
//   - Uses RSA/ECB/PKCS1Padding because this mode is the ONLY one
//     guaranteed to exist on API 19–22.
//   - APPKEY = 32 bytes - fits easily in RSA-2048 limit.
//
// Design:
//   - One RSA keypair per deviceId (alias derived from hashed deviceId).
//   - Ciphertext is stored Base64-encoded in SharedPreferences.
//   - We keep RSA private keys even after clearKey(), so reconnecting
//     the same dongle does NOT require regenerating a keypair.
//
// Notes:
//   - Public key encrypts, private key decrypts.
//   - This class has no BLE knowledge -  BleHub uses it as secure storage.
///////////////////////////////////////////////////////////////////////
package com.blu.blukeyborg

import android.content.Context
import android.content.SharedPreferences
import android.util.Base64
import android.os.Build
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Log
import java.math.BigInteger
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.MessageDigest
import java.util.Calendar
import javax.crypto.Cipher
import javax.security.auth.x500.X500Principal


object BleAppSec 
{
    private const val PREFS = "ble_appsec_keystore"

	private const val TAG = "BleAppSec"
	
    /////////////////////////////////////////////////////////////////
    // Access SharedPreferences container used to store ciphertext.
    /////////////////////////////////////////////////////////////////	
    private fun sp(ctx: Context): SharedPreferences =
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)


    /////////////////////////////////////////////////////////////////
    // slotId(deviceId):
    //   Normalize & hash the deviceId to derive a stable 128-bit code.
    //   Avoids exposing raw MAC addresses in the keystore or prefs.
    /////////////////////////////////////////////////////////////////
    private fun slotId(deviceId: String): String {
        val d = MessageDigest.getInstance("SHA-256")
            .digest(deviceId.trim().lowercase().toByteArray())
        return d.copyOf(16).joinToString("") { "%02x".format(it) } // 128-bit id
    }
	
    /////////////////////////////////////////////////////////////////
    // prefKey():
    //   SharedPreferences key used to store the Base64 ciphertext.
    /////////////////////////////////////////////////////////////////	
    private fun prefKey(deviceId: String) = "k_${slotId(deviceId)}"
	
    /////////////////////////////////////////////////////////////////
    // ksAlias():
    //   Alias inside AndroidKeyStore for RSA keypair for this device.
    /////////////////////////////////////////////////////////////////	
    private fun ksAlias(deviceId: String) = "ble_appsec_rsa_${slotId(deviceId)}"

    /////////////////////////////////////////////////////////////////
    // ensureRsaKeyPair():
    //   Creates an RSA keypair in AndroidKeyStore if it does not
    //   already exist for this alias.
    //
    //   Uses KeyPairGeneratorSpec because it works from API 19–22
    //   and remains supported on later versions.
    //
    //   RSA private key never leaves secure hardware storage.
    /////////////////////////////////////////////////////////////////
    private fun ensureRsaKeyPair(context: Context, alias: String) {
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        if (ks.containsAlias(alias)) return

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                // Modern path: use KeyGenParameterSpec with explicit paddings/digests.
                val spec = KeyGenParameterSpec.Builder(
                    alias,
                    KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT
                )
                    .setKeySize(2048)
                    .setDigests(
                        KeyProperties.DIGEST_SHA256,
                        KeyProperties.DIGEST_SHA1
                    )
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_RSA_PKCS1)
                    // No user-auth, no attestation – just a simple key for local encryption
                    .build()

                val kpg = KeyPairGenerator.getInstance(
                    KeyProperties.KEY_ALGORITHM_RSA,
                    "AndroidKeyStore"
                )
                kpg.initialize(spec)
                kpg.generateKeyPair()
                Log.d(TAG, "ensureRsaKeyPair: generated RSA keypair with KeyGenParameterSpec for alias=$alias")
            } else {
                // Legacy path for API < 23 – keep your existing KeyPairGeneratorSpec
                val start = Calendar.getInstance()
                val end = Calendar.getInstance().apply { add(Calendar.YEAR, 30) }
                val spec = android.security.KeyPairGeneratorSpec.Builder(context)
                    .setAlias(alias)
                    .setSubject(X500Principal("CN=$alias"))
                    .setSerialNumber(BigInteger.ONE)
                    .setStartDate(start.time)
                    .setEndDate(end.time)
                    .build()
                KeyPairGenerator.getInstance("RSA", "AndroidKeyStore").apply {
                    initialize(spec); generateKeyPair()
                }
                Log.d(TAG, "ensureRsaKeyPair: generated RSA keypair with KeyPairGeneratorSpec for alias=$alias")
            }
        } catch (t: Throwable) {
            Log.e(TAG, "ensureRsaKeyPair: failed to generate keypair for alias=$alias", t)
            throw t
        }
    }

    /////////////////////////////////////////////////////////////////
    // putKey():
    //   Encrypt and store a 32-byte APPKEY for a device.
    //
    //   Steps:
    //     1. Ensure RSA keypair exists.
    //     2. Encrypt APPKEY using RSA/PKCS1 with the public key.
    //     3. Base64-encode ciphertext and save to prefs.
    //
    //   Only ciphertext goes to SharedPreferences – no plaintext ever.
    /////////////////////////////////////////////////////////////////
    fun putKey(context: Context, deviceId: String, key32: ByteArray) {
        require(key32.size == 32) { "APPKEY must be 32 bytes" }
        val alias = ksAlias(deviceId)
        ensureRsaKeyPair(context, alias)

        try {
            val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
            val cert = ks.getCertificate(alias) ?: error("No public key")
            val publicKey = cert.publicKey
            val cipher = Cipher.getInstance("RSA/ECB/PKCS1Padding")
            cipher.init(Cipher.ENCRYPT_MODE, publicKey)
            val ct = cipher.doFinal(key32)
            sp(context).edit()
                .putString(prefKey(deviceId), Base64.encodeToString(ct, Base64.NO_WRAP))
                .apply()
            Log.d(TAG, "putKey: stored APPKEY for $deviceId (ct.len=${ct.size})")
        } catch (t: Throwable) {
            Log.e(TAG, "putKey: failed to store APPKEY for $deviceId", t)
            // If this fails, we prefer to fail loudly rather than pretend success.
            throw t
        }
    }

    /////////////////////////////////////////////////////////////////
    // getKey():
    //   Retrieve and decrypt the stored APPKEY.
    //
    //   Steps:
    //     1. Get ciphertext from prefs.
    //     2. Load RSA private key from AndroidKeyStore.
    //     3. RSA-decrypt to recover original APPKEY bytes.
    //
    //   Returns null if:
    //     - no ciphertext stored
    //     - RSA entry missing
    //     - Base64 corrupted
    //     - padding/decryption error
    /////////////////////////////////////////////////////////////////
    fun getKey(context: Context, deviceId: String): ByteArray? {
        val b64 = sp(context).getString(prefKey(deviceId), null) ?: return null
        return try {
            val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
            val entry = ks.getEntry(ksAlias(deviceId), null) as? KeyStore.PrivateKeyEntry
                ?: run {
                    Log.w(TAG, "getKey: no PrivateKeyEntry for $deviceId (alias=${ksAlias(deviceId)})")
                    return null
                }

            val cipher = Cipher.getInstance("RSA/ECB/PKCS1Padding")
            cipher.init(Cipher.DECRYPT_MODE, entry.privateKey)
            val ct = try {
                Base64.decode(b64, Base64.NO_WRAP)
            } catch (e: Throwable) {
                Log.e(TAG, "getKey: Base64 decode failed for $deviceId", e)
                return null
            }

            val plain = try {
                cipher.doFinal(ct)
            } catch (e: Throwable) {
                Log.e(TAG, "getKey: RSA decrypt failed for $deviceId", e)
                return null
            }

            if (plain.size != 32) {
                Log.e(TAG, "getKey: decrypted APPKEY has wrong size=${plain.size} for $deviceId")
                null
            } else {
                plain
            }
        } catch (t: Throwable) {
            Log.e(TAG, "getKey: unexpected failure for $deviceId", t)
            null
        }
    }

    /////////////////////////////////////////////////////////////////
    // clearKey():
    //   Removes only the ciphertext from SharedPreferences.
    //
    //   RSA keypair is intentionally kept so reconnecting the same
    //   device does not require a slow regeneration of the keypair.
    //
    //   If a complete wipe is desired, KeyStore.deleteEntry(alias)
    //   could be added here.
    /////////////////////////////////////////////////////////////////
    fun clearKey(context: Context, deviceId: String) 
	{
        sp(context).edit().remove(prefKey(deviceId)).apply()
        // We keep the RSA key so future re-pairs don't require re-gen; remove if you prefer.
    }
}
