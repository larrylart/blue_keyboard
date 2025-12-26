#pragma once

#include "ble_transport.h"
#include "ini_store.h"
#include "ble_crypto.h"
#include <string>
#include <vector>
#include <cstdint>

// Simple frame representation: [op][len_le][payload]
struct Frame {
    uint8_t op;
    std::vector<uint8_t> payload;
};

class Framer {
public:
    // Feed raw bytes; decoded frames are appended to 'out'.
    void push(const std::vector<uint8_t>& chunk,
              std::vector<Frame>& out);

private:
    std::vector<uint8_t> m_buf;
};

// High-level BluKeyborg protocol wrapper.
class BluKeySession {
public:
    explicit BluKeySession(const std::string& ini_path);

    // --list
    std::vector<BleDeviceInfo> list_devices(int timeout_ms = 4000);

    // --prov=<mac>
    bool provision(const std::string& mac);

    // --sendstr=... --to=...  (+ optional newline flag)
    bool send_string(const std::string& mac,
                     const std::string& text,
                     bool add_newline);

    // --sendkey=code --to=...
    bool send_key(const std::string& mac,
                  uint8_t usage,
                  uint8_t mods = 0,
                  uint8_t repeat = 1);

private:
    // INI handling
    IniFile m_ini;

    // BLE transport
    BleTransport m_ble;

    // MTLS state
    bool m_mtls_ready = false;
    int  m_sid        = 0;
    std::vector<uint8_t> m_sess_key;
    uint16_t m_seq_out = 0;
	std::vector<uint8_t> m_k_enc;
	std::vector<uint8_t> m_k_mac;
	std::vector<uint8_t> m_k_iv;

	// Simplified connect signal
	// as the only "ready" indicator.
	bool connect_and_wait_b0(const std::string& mac,
							 std::vector<uint8_t>* b0_payload_out,
							 bool ensure_paired);

	bool ensure_appkey(const std::string& mac);


    bool run_appkey_flow(const std::string& mac,
                         const std::string& password);

    bool do_mtls_handshake_from_b0(const std::string& mac,
                                   const std::vector<uint8_t>& b0_payload);

    bool send_raw_frame(uint8_t op,
                        const std::vector<uint8_t>& payload);

    bool await_next_frame(int timeout_ms,
                          uint8_t op1,
                          uint8_t op2,
                          Frame& out);

    std::vector<uint8_t> wrap_b3(const std::vector<uint8_t>& inner_frame);

    bool send_app_frame(uint8_t op,
                        const std::vector<uint8_t>& payload);

    bool await_app_reply(int timeout_ms,
                         uint8_t expect_op,
                         std::vector<uint8_t>& payload_out);

    bool send_get_info_layout(std::string& layout_out);

    bool enable_fast_keys();

    bool send_string_impl(const std::string& text,
                          bool add_newline);

    bool send_key_impl(uint8_t usage,
                       uint8_t mods,
                       uint8_t repeat);

    // INI helpers
    bool get_appkey_for_mac(const std::string& mac,
                            std::vector<uint8_t>& out_key);

    void store_appkey_for_mac(const std::string& mac,
                              const std::vector<uint8_t>& key);
};
