#include "ble_proto.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

using namespace std;

static long long t_ms() {
    using namespace std::chrono;
    static auto t0 = steady_clock::now();
    auto now = steady_clock::now();
    return duration_cast<milliseconds>(now - t0).count();
}


// --- Framer implementation ---
static uint16_t rd_u16le(const uint8_t* b) {
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}
static void wr_u16le(uint8_t* b, uint16_t v) {
    b[0] = static_cast<uint8_t>(v & 0xFF);
    b[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
static uint16_t rd_u16be(const uint8_t* b) {
    return static_cast<uint16_t>((b[0] << 8) | b[1]);
}

void Framer::push(const vector<uint8_t>& chunk, vector<Frame>& out) {
    const size_t MAX_LEN = 1024;
    m_buf.insert(m_buf.end(), chunk.begin(), chunk.end());
    size_t i = 0;

    auto can_header = [&](size_t pos) {
        return pos + 3 <= m_buf.size();
    };
    auto plausible = [&](size_t pos) {
        if (!can_header(pos)) return false;
        uint16_t len = rd_u16le(&m_buf[pos + 1]);
        if (len > MAX_LEN) return false;
        return pos + 3 + len <= m_buf.size();
    };

    while (i < m_buf.size() && !plausible(i)) {
        ++i;
    }

    while (plausible(i)) {
        uint8_t  op  = m_buf[i];
        uint16_t len = rd_u16le(&m_buf[i + 1]);
        vector<uint8_t> payload(m_buf.begin() + i + 3,
                                m_buf.begin() + i + 3 + len);
        out.push_back(Frame{ op, std::move(payload) });
        i += 3 + len;
        while (i < m_buf.size() && !plausible(i)) {
            ++i;
        }
    }

    if (i > 0) {
        vector<uint8_t> rest(m_buf.begin() + i, m_buf.end());
        m_buf.swap(rest);
    }
}

// --- small helpers ---

static vector<uint8_t> be_int(uint32_t v) {
    return {
        static_cast<uint8_t>(v >> 24),
        static_cast<uint8_t>(v >> 16),
        static_cast<uint8_t>(v >> 8),
        static_cast<uint8_t>(v)
    };
}
static vector<uint8_t> le_int(uint32_t v) {
    return {
        static_cast<uint8_t>(v & 0xFF),
        static_cast<uint8_t>((v >> 8) & 0xFF),
        static_cast<uint8_t>((v >> 16) & 0xFF),
        static_cast<uint8_t>((v >> 24) & 0xFF)
    };
}
static vector<uint8_t> be_short(uint16_t v) {
    return {
        static_cast<uint8_t>(v >> 8),
        static_cast<uint8_t>(v & 0xFF)
    };
}
static vector<uint8_t> le_short(uint16_t v) {
    return {
        static_cast<uint8_t>(v & 0xFF),
        static_cast<uint8_t>(v >> 8)
    };
}

// Extract LAYOUT=XXXX from a banner string.
static bool parse_layout_from_banner(const std::string& s,
                                     std::string& out_layout) {
    const char* key = "LAYOUT=";
    size_t pos = s.find(key);
    if (pos == std::string::npos) {
        return false;
    }
    pos += std::strlen(key);

    size_t start = pos;
    while (pos < s.size()) {
        char c = s[pos];
        if (c == ';' || c == ' ' || c == '\r' ||
            c == '\n' || c == '\t') {
            break;
        }
        ++pos;
    }
    if (pos == start) {
        return false;
    }
    out_layout.assign(s.begin() + start, s.begin() + pos);
    return true;
}

// Compute MTLS IV.
static vector<uint8_t> mtls_iv(const vector<uint8_t>& k_iv,
                               int sid,
                               char dir,
                               uint16_t seq) {
    vector<uint8_t> msg;
    // "IV1" || sid_be || dir || seq_be
    msg.insert(msg.end(), {'I','V','1'});

    auto sid_be = be_int(static_cast<uint32_t>(sid));
    msg.insert(msg.end(), sid_be.begin(), sid_be.end());
    msg.push_back(static_cast<uint8_t>(dir));

    auto seq_be = be_short(seq);
    msg.insert(msg.end(), seq_be.begin(), seq_be.end());

    auto h = hmac_sha256(k_iv, msg);
    h.resize(16);
    return h;
}


// --- BluKeySession implementation ---

BluKeySession::BluKeySession(const string& ini_path)
    : m_ini(ini_path) {
    m_ini.load();
}

vector<BleDeviceInfo> BluKeySession::list_devices(int timeout_ms) {
    return m_ble.scan(timeout_ms);
}

// Read APPKEY from INI into out_key.
bool BluKeySession::get_appkey_for_mac(const string& mac,
                                       vector<uint8_t>& out_key) {
    auto v = m_ini.get(mac, "app_key");
    if (!v) {
        return false;
    }
    try {
        out_key = hex_decode(*v);
    } catch (...) {
        return false;
    }
    return true;
}

void BluKeySession::store_appkey_for_mac(const string& mac,
                                         const vector<uint8_t>& key) {
    m_ini.set(mac, "app_key", hex_encode(key));
    m_ini.save();
}

// Connect and read either ASCII banner (LAYOUT=...) or B0.
// Connect and wait for B0 (server hello) only.
// Simplified protocol: text banners are no longer used.
bool BluKeySession::connect_and_wait_b0(const string& mac,
                                        vector<uint8_t>* b0_payload_out,
                                        bool ensure_paired)
{
    // Try cached BlueZ paths from INI first
    std::string dev_hint, tx_hint, rx_hint;
    if (auto v = m_ini.get(mac, "device_path"))  dev_hint = *v;
    if (auto v = m_ini.get(mac, "tx_char_path")) tx_hint  = *v;
    if (auto v = m_ini.get(mac, "rx_char_path")) rx_hint  = *v;

    if (!m_ble.connect(mac, ensure_paired, dev_hint, tx_hint, rx_hint)) {
        return false;
    }

    // After a successful connect, refresh the cached paths from what BlueZ resolved
    std::string dev_path = m_ble.get_device_path();
    std::string tx_path  = m_ble.get_tx_char_path();
    std::string rx_path  = m_ble.get_rx_char_path();
    if (!dev_path.empty() && !tx_path.empty() && !rx_path.empty()) {
        m_ini.set(mac, "device_path",  dev_path);
        m_ini.set(mac, "tx_char_path", tx_path);
        m_ini.set(mac, "rx_char_path", rx_path);
        m_ini.save();
    }

    Framer framer;
    auto start = chrono::steady_clock::now();
    const int TOTAL_TIMEOUT_MS = 5000;

    while (true) {
        auto now = chrono::steady_clock::now();
        int elapsed = static_cast<int>(
            chrono::duration_cast<chrono::milliseconds>(now - start).count());
        if (elapsed >= TOTAL_TIMEOUT_MS) {
            cerr << "Timeout waiting for B0\n";
            return false;
        }

        int remain = TOTAL_TIMEOUT_MS - elapsed;
        auto chunk_opt = m_ble.wait_notification(remain);
        if (!chunk_opt) {
            cerr << "Timeout waiting notification\n";
            return false;
        }
        const auto& chunk = *chunk_opt;

        // Decode binary frames and look for B0.
        vector<Frame> frames;
        framer.push(chunk, frames);
        for (const auto& f : frames) {
            if (f.op == 0xB0) {
                if (b0_payload_out != nullptr) {
                    *b0_payload_out = f.payload;
                }
                return true;
            }
        }
        // Otherwise, keep waiting until timeout and loop again.
    }
}


bool BluKeySession::run_appkey_flow(const string& mac,
                                    const string& password) {
    vector<uint8_t> pass_bytes(password.begin(), password.end());

    // Send A0
    if (!send_raw_frame(0xA0, {})) {
        cerr << "Failed to send A0\n";
        return false;
    }

    // Wait for A2 or 0xFF
    Frame f;
    if (!await_next_frame(6000, 0xA2, 0xFF, f)) {
        cerr << "No CHALLENGE (A2)\n";
        return false;
    }
    if (f.op == 0xFF) {
        string msg(f.payload.begin(), f.payload.end());
        cerr << "Device rejected APPKEY: " << msg << "\n";
        return false;
    }
    if (f.payload.size() != 36) {
        cerr << "Bad A2 payload size\n";
        return false;
    }

    vector<uint8_t> salt(f.payload.begin(), f.payload.begin() + 16);
    uint32_t iters =  static_cast<uint32_t>(f.payload[16])
                    | (static_cast<uint32_t>(f.payload[17]) << 8)
                    | (static_cast<uint32_t>(f.payload[18]) << 16)
                    | (static_cast<uint32_t>(f.payload[19]) << 24);
    vector<uint8_t> chal(f.payload.begin() + 20, f.payload.begin() + 36);

    // verif = PBKDF2(password, salt, iters)
    vector<uint8_t> verif = pbkdf2_sha256(pass_bytes, salt, static_cast<int>(iters), 32);

    // msg = "APPKEY" || chal
    vector<uint8_t> msg_vec;
    const char tag[] = "APPKEY";
    msg_vec.insert(msg_vec.end(), tag, tag + std::strlen(tag));
    msg_vec.insert(msg_vec.end(), chal.begin(), chal.end());

    // mac = HMAC(verif, msg)
    vector<uint8_t> mac_full = hmac_sha256(verif, msg_vec);
    mac_full.resize(32); // device expects full HMAC, will truncate internally

    // Send A3
    if (!send_raw_frame(0xA3, mac_full)) {
        cerr << "Failed to send A3\n";
        return false;
    }

    // Wait for A1 or 0xFF
    Frame f2;
    if (!await_next_frame(6000, 0xA1, 0xFF, f2)) {
        cerr << "No A1 / error\n";
        return false;
    }
    if (f2.op == 0xFF) {
        string msg2(f2.payload.begin(), f2.payload.end());
        cerr << "APPKEY failed: " << msg2 << "\n";
        return false;
    }

    vector<uint8_t> payload = f2.payload;
    if (payload.size() == 32) {
        store_appkey_for_mac(mac, payload);
        return true;
    } else if (payload.size() == 48) {
        // Wrapped APPKEY
        vector<uint8_t> cipher(payload.begin(), payload.begin() + 32);
        vector<uint8_t> mac_in(payload.begin() + 32, payload.end());

        // wrapKey = HMAC(verif, "AKWRAP" || chal)
        vector<uint8_t> wrap_msg;
        const char tag_wrap[] = "AKWRAP";
        wrap_msg.insert(wrap_msg.end(), tag_wrap, tag_wrap + std::strlen(tag_wrap));
        wrap_msg.insert(wrap_msg.end(), chal.begin(), chal.end());
        vector<uint8_t> wrap_key = hmac_sha256(verif, wrap_msg);

        // macExp = HMAC(wrapKey, "AKMAC" || chal || cipher)[0..15]
        vector<uint8_t> mac_msg;
        const char tag_mac[] = "AKMAC";
        mac_msg.insert(mac_msg.end(), tag_mac, tag_mac + std::strlen(tag_mac));
        mac_msg.insert(mac_msg.end(), chal.begin(), chal.end());
        mac_msg.insert(mac_msg.end(), cipher.begin(), cipher.end());
        vector<uint8_t> mac_full2 = hmac_sha256(wrap_key, mac_msg);
        vector<uint8_t> mac_exp(mac_full2.begin(), mac_full2.begin() + 16);
        if (mac_in != mac_exp) {
            cerr << "Wrapped APPKEY MAC mismatch\n";
            return false;
        }

        // iv = HMAC(verif, "AKIV" || chal)[0..15]
        vector<uint8_t> iv_msg;
        const char tag_iv[] = "AKIV";
        iv_msg.insert(iv_msg.end(), tag_iv, tag_iv + std::strlen(tag_iv));
        iv_msg.insert(iv_msg.end(), chal.begin(), chal.end());
        vector<uint8_t> iv_full = hmac_sha256(verif, iv_msg);
        vector<uint8_t> iv(iv_full.begin(), iv_full.begin() + 16);

        vector<uint8_t> plain = aes_ctr_encrypt(wrap_key, iv, cipher);
        if (plain.size() != 32) {
            cerr << "APPKEY decrypted size != 32\n";
            return false;
        }
        store_appkey_for_mac(mac, plain);
        return true;
    } else {
        cerr << "Unexpected A1 payload size\n";
        return false;
    }
}

bool BluKeySession::ensure_appkey(const string& mac) {
    vector<uint8_t> existing;
    if (get_appkey_for_mac(mac, existing)) return true;

    cout << "Dongle requires provisioning (APPKEY not stored locally).\n";
    cout << "Enter provisioning password (setup password used on Wi-Fi portal): ";
    string pw;
    std::getline(cin, pw);
    if (pw.empty()) {
        cerr << "Password empty, aborting\n";
        return false;
    }

    bool ok = run_appkey_flow(mac, pw);
    if (ok) {
        cout << "APPKEY stored for " << mac << "\n";
    }
    return ok;
}

////////////////////////////////////////////////////////////////////
bool BluKeySession::provision(const string& mac) 
{
    // Make sure CLI Agent is registered only for provisioning flows
    m_ble.ensure_cli_agent();

    // STEP 1: connect and wait for B0 (unprovisioned devices still send B0).
    std::vector<uint8_t> b0;
    if (!connect_and_wait_b0(mac, &b0, /*ensure_paired=*/true)) {
        return false;
    }

    // STEP 2: run APPKEY flow using the CLI password prompt (A0/A2/A3/A1).
    if (!ensure_appkey(mac)) {
        return false;
    }

    // STEP 3: reconnect and perform MTLS handshake from fresh B0.
    m_ble.disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    std::vector<uint8_t> b0_2;
    if (!connect_and_wait_b0(mac, &b0_2, /*ensure_paired=*/true)) {
        return false;
    }
    if (b0_2.empty()) {
        std::cerr << "Expected B0 after provisioning\n";
        return false;
    }
    if (!do_mtls_handshake_from_b0(mac, b0_2)) {
        return false;
    }

    // STEP 4: query layout over secure channel and cache it (C1/C2).
    std::string layout_secure;
    if (send_get_info_layout(layout_secure)) {
        m_ini.set(mac, "keyboard_layout", layout_secure);
        m_ini.save();
    }

    return true;
}


bool BluKeySession::send_raw_frame(uint8_t op,
                                   const vector<uint8_t>& payload) {
    vector<uint8_t> frame;
    frame.reserve(3 + payload.size());
    frame.push_back(op);
    uint8_t len_le[2];
    wr_u16le(len_le, static_cast<uint16_t>(payload.size()));
    frame.push_back(len_le[0]);
    frame.push_back(len_le[1]);
    frame.insert(frame.end(), payload.begin(), payload.end());
    return m_ble.write_tx(frame);
}

bool BluKeySession::await_next_frame(int timeout_ms,
                                     uint8_t op1,
                                     uint8_t op2,
                                     Frame& out) {
    Framer framer;
    auto start = chrono::steady_clock::now();

    while (true) {
        auto now = chrono::steady_clock::now();
        int elapsed = static_cast<int>(
            chrono::duration_cast<chrono::milliseconds>(now - start).count());
        if (elapsed >= timeout_ms) {
            return false;
        }
        int remain = timeout_ms - elapsed;
        auto chunk_opt = m_ble.wait_notification(remain);
        if (!chunk_opt) {
            return false;
        }

        vector<Frame> frames;
        framer.push(*chunk_opt, frames);
        for (const auto& f : frames) {
            if (f.op == op1 || (op2 != 0 && f.op == op2)) {
                out = f;
                return true;
            }
        }
    }
}

vector<uint8_t> BluKeySession::wrap_b3(const vector<uint8_t>& inner) {
    if (!m_mtls_ready || m_sess_key.empty()) {
        throw runtime_error("MTLS not ready");
    }

    uint16_t seq = m_seq_out;
	// match dongle: prevent wrap reuse (force re-handshake before 0xFFFF)
	if (m_seq_out == 0xFFFF) {
		m_mtls_ready = false;           // force caller to handshake again
		throw runtime_error("MTLS seq wrap imminent; re-handshake required");
	}

	auto iv  = mtls_iv(m_k_iv, m_sid, 'C', seq);
	auto enc = aes_ctr_encrypt(m_k_enc, iv, inner);

    vector<uint8_t> mac_data;
    const char tag[] = "ENCM";
    mac_data.insert(mac_data.end(), tag, tag + std::strlen(tag));
    auto sid_be = be_int(static_cast<uint32_t>(m_sid));
    mac_data.insert(mac_data.end(), sid_be.begin(), sid_be.end());
	mac_data.push_back('C');
	auto seq_be_mac = be_short(seq);
	mac_data.insert(mac_data.end(), seq_be_mac.begin(), seq_be_mac.end());
	mac_data.insert(mac_data.end(), enc.begin(), enc.end());
	
   auto mac_full = hmac_sha256(m_k_mac, mac_data);
	
    vector<uint8_t> mac16(mac_full.begin(), mac_full.begin() + 16);

	vector<uint8_t> payload;

	auto seq_be_hdr  = be_short(seq);
	auto clen_be_hdr = be_short(static_cast<uint16_t>(enc.size()));
	payload.insert(payload.end(), seq_be_hdr.begin(), seq_be_hdr.end());
	payload.insert(payload.end(), clen_be_hdr.begin(), clen_be_hdr.end());
	
    payload.insert(payload.end(), enc.begin(), enc.end());
    payload.insert(payload.end(), mac16.begin(), mac16.end());

    vector<uint8_t> frame;
    frame.push_back(0xB3);
    uint8_t len_le[2];
    wr_u16le(len_le, static_cast<uint16_t>(payload.size()));
    frame.push_back(len_le[0]);
    frame.push_back(len_le[1]);
    frame.insert(frame.end(), payload.begin(), payload.end());

    m_seq_out = static_cast<uint16_t>((m_seq_out + 1) & 0xFFFF);
    return frame;
}

bool BluKeySession::send_app_frame(uint8_t op,
                                   const vector<uint8_t>& payload) {
    if (!m_mtls_ready) {
        cerr << "MTLS not established\n";
        return false;
    }

    vector<uint8_t> inner;
    inner.push_back(op);
    uint8_t len_le[2];
    wr_u16le(len_le, static_cast<uint16_t>(payload.size()));
    inner.push_back(len_le[0]);
    inner.push_back(len_le[1]);
    inner.insert(inner.end(), payload.begin(), payload.end());

    auto b3 = wrap_b3(inner);
    return m_ble.write_tx(b3);
}

bool BluKeySession::await_app_reply(int timeout_ms,
                                    uint8_t expect_op,
                                    vector<uint8_t>& payload_out) {
    if (!m_mtls_ready || m_sess_key.empty()) {
        return false;
    }

    Framer framer;
    auto start = chrono::steady_clock::now();

    while (true) {
        auto now = chrono::steady_clock::now();
        int elapsed = static_cast<int>(
            chrono::duration_cast<chrono::milliseconds>(now - start).count());
        if (elapsed >= timeout_ms) {
            return false;
        }
        int remain = timeout_ms - elapsed;
        auto chunk_opt = m_ble.wait_notification(remain);
        if (!chunk_opt) {
            return false;
        }

        vector<Frame> frames;
        framer.push(*chunk_opt, frames);
        for (const auto& f : frames) {
            if (f.op != 0xB3) {
                continue;
            }
            const auto& p = f.payload;
            if (p.size() < 2 + 2 + 16) {
                continue;
            }
			uint16_t seq  = rd_u16be(&p[0]);
			uint16_t clen = rd_u16be(&p[2]);
            if (p.size() != 2 + 2 + clen + 16) {
                continue;
            }

            vector<uint8_t> cipher(p.begin() + 4, p.begin() + 4 + clen);
            vector<uint8_t> mac_in(p.begin() + 4 + clen, p.end());

            vector<uint8_t> mac_data;
            const char tag[] = "ENCM";
            mac_data.insert(mac_data.end(), tag, tag + std::strlen(tag));
            auto sid_be = be_int(static_cast<uint32_t>(m_sid));
            mac_data.insert(mac_data.end(), sid_be.begin(), sid_be.end());
            mac_data.push_back('S');
            auto seq_be = be_short(seq);
            mac_data.insert(mac_data.end(), seq_be.begin(), seq_be.end());
            mac_data.insert(mac_data.end(), cipher.begin(), cipher.end());
            auto mac_full = hmac_sha256(m_k_mac, mac_data);
            vector<uint8_t> mac_exp(mac_full.begin(), mac_full.begin() + 16);
            if (mac_exp != mac_in) {
                continue;
            }

            auto iv = mtls_iv(m_k_iv, m_sid, 'S', seq);
            auto plain = aes_ctr_encrypt(m_k_enc, iv, cipher);
            if (plain.size() < 3) {
                continue;
            }
            uint8_t  op_in = plain[0];
            uint16_t L     = rd_u16le(&plain[1]);
            if (plain.size() != 3 + L) {
                continue;
            }
            if (op_in != expect_op) {
                continue;
            }
            payload_out.assign(plain.begin() + 3, plain.end());
            return true;
        }
    }
}

bool BluKeySession::do_mtls_handshake_from_b0(const string& mac,
                                              const vector<uint8_t>& b0_payload) {
    if (b0_payload.size() != 69) {
        cerr << "Bad B0 payload size\n";
        return false;
    }

    vector<uint8_t> srv_pub(b0_payload.begin(), b0_payload.begin() + 65);
	uint32_t sid =  (static_cast<uint32_t>(b0_payload[65]) << 24)
				  | (static_cast<uint32_t>(b0_payload[66]) << 16)
				  | (static_cast<uint32_t>(b0_payload[67]) << 8)
				  |  static_cast<uint32_t>(b0_payload[68]);

    vector<uint8_t> appkey;
    if (!get_appkey_for_mac(mac, appkey)) {
        cerr << "APPKEY missing for " << mac << "\n";
        return false;
    }

    // Generate P-256 keypair
    EC_KEY* eckey = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (!eckey || EC_KEY_generate_key(eckey) != 1) {
        cerr << "EC keygen failed\n";
        if (eckey) {
            EC_KEY_free(eckey);
        }
        return false;
    }
    const EC_GROUP* group     = EC_KEY_get0_group(eckey);
    const EC_POINT* pub_point = EC_KEY_get0_public_key(eckey);

    vector<uint8_t> cli_pub(65);
    cli_pub[0] = 0x04;
    if (EC_POINT_point2oct(group,
                           pub_point,
                           POINT_CONVERSION_UNCOMPRESSED,
                           cli_pub.data(),
                           cli_pub.size(),
                           nullptr) != 65) {
        cerr << "EC_POINT_point2oct failed\n";
        EC_KEY_free(eckey);
        return false;
    }

    vector<uint8_t> keyx_msg;
    const char tag[] = "KEYX";
    keyx_msg.insert(keyx_msg.end(), tag, tag + std::strlen(tag));

	auto sid_be_keyx = be_int(sid);
	keyx_msg.insert(keyx_msg.end(), sid_be_keyx.begin(), sid_be_keyx.end());

    keyx_msg.insert(keyx_msg.end(), srv_pub.begin(), srv_pub.end());
    keyx_msg.insert(keyx_msg.end(), cli_pub.begin(), cli_pub.end());
    auto mac_full = hmac_sha256(appkey, keyx_msg);
    vector<uint8_t> mac16(mac_full.begin(), mac_full.begin() + 16);

    vector<uint8_t> b1_payload;
    b1_payload.insert(b1_payload.end(), cli_pub.begin(), cli_pub.end());
    b1_payload.insert(b1_payload.end(), mac16.begin(), mac16.end());

    if (!send_raw_frame(0xB1, b1_payload)) {
        cerr << "Failed to send B1\n";
        EC_KEY_free(eckey);
        return false;
    }

    Frame resp;
    if (!await_next_frame(4000, 0xB2, 0xFF, resp)) {
        cerr << "No B2 / error\n";
        EC_KEY_free(eckey);
        return false;
    }
    if (resp.op == 0xFF) {
        string msg(resp.payload.begin(), resp.payload.end());
        cerr << "Handshake failed: " << msg << "\n";
        EC_KEY_free(eckey);
        return false;
    }

    // ECDH shared secret
    EC_POINT* srv_point = EC_POINT_new(group);
    if (!srv_point) {
        EC_KEY_free(eckey);
        return false;
    }
    if (!EC_POINT_oct2point(group,
                            srv_point,
                            srv_pub.data(),
                            srv_pub.size(),
                            nullptr)) {
        cerr << "EC_POINT_oct2point failed\n";
        EC_POINT_free(srv_point);
        EC_KEY_free(eckey);
        return false;
    }

    int field_size = EC_GROUP_get_degree(group);
    int secret_len = (field_size + 7) / 8;
    vector<uint8_t> shared(secret_len);
    int rc = ECDH_compute_key(shared.data(),
                              shared.size(),
                              srv_point,
                              eckey,
                              nullptr);
    EC_POINT_free(srv_point);
    EC_KEY_free(eckey);
    if (rc <= 0) {
        cerr << "ECDH_compute_key failed\n";
        return false;
    }
    shared.resize(rc);

    vector<uint8_t> info;
    const char tag_mt1[] = "MT1";
    info.insert(info.end(), tag_mt1, tag_mt1 + std::strlen(tag_mt1));

	auto sid_be_info = be_int(sid);
	info.insert(info.end(), sid_be_info.begin(), sid_be_info.end());

    info.insert(info.end(), srv_pub.begin(), srv_pub.end());
    info.insert(info.end(), cli_pub.begin(), cli_pub.end());
    auto sess = hkdf_sha256(appkey, shared, info);

	auto kEnc = hmac_sha256(sess, std::vector<uint8_t>{'E','N','C'});
	auto kMac = hmac_sha256(sess, std::vector<uint8_t>{'M','A','C'});
	auto kIv  = hmac_sha256(sess, std::vector<uint8_t>{'I','V','K'});

	// keep them 32 bytes (they already are)
	kEnc.resize(32);
	kMac.resize(32);
	kIv.resize(32);

    vector<uint8_t> sfin_msg;
    const char tag_sfin[] = "SFIN";
    sfin_msg.insert(sfin_msg.end(), tag_sfin, tag_sfin + std::strlen(tag_sfin));
	
	auto sid_be4 = be_int(sid);
	sfin_msg.insert(sfin_msg.end(), sid_be4.begin(), sid_be4.end());

    sfin_msg.insert(sfin_msg.end(), srv_pub.begin(), srv_pub.end());
    sfin_msg.insert(sfin_msg.end(), cli_pub.begin(), cli_pub.end());

	auto expect_full = hmac_sha256(kMac, sfin_msg);
	
    vector<uint8_t> expect(expect_full.begin(), expect_full.begin() + 16);
    if (resp.payload != expect) {
        cerr << "SFIN mismatch\n";
        return false;
    }

    m_sid       = static_cast<int>(sid);
    m_sess_key  = std::move(sess);
	m_k_enc    = std::move(kEnc);
	m_k_mac    = std::move(kMac);
	m_k_iv     = std::move(kIv);	
    m_seq_out   = 0;
    m_mtls_ready = true;
    cout << "MTLS session established (sid=" << sid << ")\n";
    return true;
}

bool BluKeySession::send_get_info_layout(string& layout_out) {
    if (!send_app_frame(0xC1, {})) {
        return false;
    }
    vector<uint8_t> pay;
    if (!await_app_reply(4000, 0xC2, pay)) {
        return false;
    }
    string txt(pay.begin(), pay.end());
    string layout;
    if (!parse_layout_from_banner(txt, layout)) {
        return false;
    }
    layout_out = layout;
    return true;
}

bool BluKeySession::enable_fast_keys() {
    vector<uint8_t> body = { 0x01 };
    if (!send_app_frame(0xC8, body)) {
        return false;
    }
    vector<uint8_t> pay;
    if (!await_app_reply(4000, 0x00, pay)) {
        return false;
    }
    if (!pay.empty()) {
        return false;
    }
    return true;
}

bool BluKeySession::send_string_impl(const string& text,
                                     bool add_newline) {
    string value = text;
    if (add_newline) {
        value.push_back('\n');
    }

    vector<uint8_t> bytes(value.begin(), value.end());
    auto expected_md5 = md5_bytes(bytes);

    if (!send_app_frame(0xD0, bytes)) {
        return false;
    }

    vector<uint8_t> pay;
    if (!await_app_reply(6000, 0xD1, pay)) {
        return false;
    }
    if (pay.size() != 17) {
        return false;
    }

    uint8_t status = pay[0];
    vector<uint8_t> md5_recv(pay.begin() + 1, pay.end());
    if (status != 0) {
        cerr << "Non-zero D1 status\n";
        return false;
    }
    if (md5_recv != expected_md5) {
        cerr << "MD5 mismatch\n";
        return false;
    }
    return true;
}

bool BluKeySession::send_key_impl(uint8_t usage,
                                  uint8_t mods,
                                  uint8_t repeat) {
    if (repeat == 0) {
        repeat = 1;
    }
    vector<uint8_t> payload;
    payload.push_back(mods);
    payload.push_back(usage);
    if (repeat > 1) {
        payload.push_back(repeat);
    }
    // E0 is raw (non-B3) in firmware, but requires MTLS session active.
    return send_raw_frame(0xE0, payload);
}

bool BluKeySession::send_string(const string& mac,
                                const string& text,
                                bool add_newline) 
{
    // Fast path: if we don’t have an APPKEY, there’s no point trying to send
    vector<uint8_t> dummy_key;
    if (!get_appkey_for_mac(mac, dummy_key)) {
        cerr << "No APPKEY stored for " << mac
             << " – provision the dongle first with --prov.\n";
        return false;
    }

    std::vector<uint8_t> b0;

    // Already provisioned: skip pairing, just connect and expect B0
    if (!connect_and_wait_b0(mac, &b0, /*ensure_paired=*/false)) {
        return false;
    }
    if (b0.empty()) {
        cerr << "Expected B0 for send_string()\n";
        return false;
    }
    if (!do_mtls_handshake_from_b0(mac, b0)) {
        return false;
    }
    return send_string_impl(text, add_newline);
}


bool BluKeySession::send_key(const string& mac,
                             uint8_t usage,
                             uint8_t mods,
                             uint8_t repeat) 
{
    // APPKEY ⇒ already paired
    vector<uint8_t> dummy_key;
    if (!get_appkey_for_mac(mac, dummy_key)) {
        cerr << "No APPKEY stored for " << mac
             << " – provision the dongle first with --prov.\n";
        return false;
    }

    std::vector<uint8_t> b0;
    if (!connect_and_wait_b0(mac, &b0, /*ensure_paired=*/false)) {
        return false;
    }
    if (b0.empty()) {
        cerr << "Expected B0 for send_key()\n";
        return false;
    }
    if (!do_mtls_handshake_from_b0(mac, b0)) {
        return false;
    }
    if (!enable_fast_keys()) {
        cerr << "Failed to enable fast keys\n";
        return false;
    }
    return send_key_impl(usage, mods, repeat);
}


