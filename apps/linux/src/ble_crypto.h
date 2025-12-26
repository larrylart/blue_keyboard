
#pragma once
#include <vector>
#include <string>
#include <cstdint>

std::vector<uint8_t> hmac_sha256(const std::vector<uint8_t>& key,
                                 const std::vector<uint8_t>& data);

std::vector<uint8_t> pbkdf2_sha256(const std::vector<uint8_t>& pass,
                                   const std::vector<uint8_t>& salt,
                                   int iterations,
                                   size_t dk_len = 32);

std::vector<uint8_t> aes_ctr_encrypt(const std::vector<uint8_t>& key,
                                     const std::vector<uint8_t>& iv,
                                     const std::vector<uint8_t>& plaintext);

std::vector<uint8_t> hkdf_sha256(const std::vector<uint8_t>& salt,
                                 const std::vector<uint8_t>& ikm,
                                 const std::vector<uint8_t>& info);

std::vector<uint8_t> md5_bytes(const std::vector<uint8_t>& data);

std::string hex_encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> hex_decode(const std::string& hex);
