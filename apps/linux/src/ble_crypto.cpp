
#include "ble_crypto.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/aes.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>

std::vector<uint8_t> hmac_sha256(const std::vector<uint8_t>& key,
                                 const std::vector<uint8_t>& data) {
    unsigned int len = 0;
    std::vector<uint8_t> out(EVP_MAX_MD_SIZE);
    HMAC(EVP_sha256(), key.data(), (int)key.size(),
         data.data(), data.size(),
         out.data(), &len);
    out.resize(len);
    return out;
}

std::vector<uint8_t> pbkdf2_sha256(const std::vector<uint8_t>& pass,
                                   const std::vector<uint8_t>& salt,
                                   int iterations,
                                   size_t dk_len) {
    std::vector<uint8_t> out(dk_len);
    if (!PKCS5_PBKDF2_HMAC((const char*)pass.data(), (int)pass.size(),
                            salt.data(), (int)salt.size(),
                            iterations,
                            EVP_sha256(),
                            (int)dk_len,
                            out.data())) {
        throw std::runtime_error("PKCS5_PBKDF2_HMAC failed");
    }
    return out;
}

std::vector<uint8_t> aes_ctr_encrypt(const std::vector<uint8_t>& key,
                                     const std::vector<uint8_t>& iv,
                                     const std::vector<uint8_t>& plaintext) 
{
    if (key.size() != 32 || iv.size() != 16) {
        throw std::runtime_error("aes_ctr_encrypt: invalid key/iv size");
    }

    std::vector<uint8_t> out(plaintext.size());

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    }

    int rc = EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), nullptr,
                                key.data(), iv.data());
    if (rc != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptInit_ex(EVP_aes_256_ctr) failed");
    }

    int out_len1 = 0;
    rc = EVP_EncryptUpdate(ctx,
                           out.data(), &out_len1,
                           plaintext.data(), static_cast<int>(plaintext.size()));
    if (rc != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptUpdate failed");
    }

    int out_len2 = 0;
    rc = EVP_EncryptFinal_ex(ctx,
                             out.data() + out_len1, &out_len2);
    if (rc != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptFinal_ex failed");
    }

    EVP_CIPHER_CTX_free(ctx);

    out.resize(out_len1 + out_len2); // Should equal plaintext.size() for CTR
    return out;
}


std::vector<uint8_t> hkdf_sha256(const std::vector<uint8_t>& salt,
                                 const std::vector<uint8_t>& ikm,
                                 const std::vector<uint8_t>& info) {
    // HKDF-Extract
    auto prk = hmac_sha256(salt, ikm);
    // HKDF-Expand (1 block)
    HMAC_CTX* ctx = HMAC_CTX_new();
    if (!ctx) throw std::runtime_error("HMAC_CTX_new failed");
    std::vector<uint8_t> okm(32);
    unsigned int len = 0;
    uint8_t counter = 1;
    HMAC_Init_ex(ctx, prk.data(), (int)prk.size(), EVP_sha256(), nullptr);
    HMAC_Update(ctx, info.data(), info.size());
    HMAC_Update(ctx, &counter, 1);
    HMAC_Final(ctx, okm.data(), &len);
    HMAC_CTX_free(ctx);
    okm.resize(len);
    return okm;
}

std::vector<uint8_t> md5_bytes(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out(MD5_DIGEST_LENGTH);
    MD5(data.data(), data.size(), out.data());
    return out;
}

std::string hex_encode(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t b : data) {
        oss << std::setw(2) << (int)b;
    }
    return oss.str();
}

std::vector<uint8_t> hex_decode(const std::string& hex) {
    if (hex.size() % 2 != 0) throw std::runtime_error("hex_decode: odd length");
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        std::string byte_str = hex.substr(i, 2);
        uint8_t val = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
        out.push_back(val);
    }
    return out;
}
