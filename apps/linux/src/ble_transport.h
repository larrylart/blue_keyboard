#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <gio/gio.h>

struct BleDeviceInfo {
    std::string address;
    std::string name;
};

class BleTransport {
public:
    BleTransport();
    ~BleTransport();

    // Scan for a short period and return list of devices (MAC + name)
    std::vector<BleDeviceInfo> scan(int timeout_ms = 4000);

	// Connect to a device by MAC address (blocking)
	// 'ensure_paired' = true for provisioning, false for fast send when APPKEY exists
	// Optional cached BlueZ paths (device/tx/rx) let us skip slow full-tree discovery.
	bool connect(const std::string& address,
				 bool ensure_paired = true,
				 const std::string& dev_hint = std::string(),
				 const std::string& tx_hint  = std::string(),
				 const std::string& rx_hint  = std::string());

    // Disconnect if connected
    void disconnect();

    // Write raw bytes to the Nordic UART TX characteristic
    bool write_tx(const std::vector<uint8_t>& data);

    // Blocking wait for next notification chunk with timeout (ms)
    std::optional<std::vector<uint8_t>> wait_notification(int timeout_ms);

    // Nordic UART UUIDs
    static const char* SERVICE_UUID_STR;
    static const char* CHAR_TX_UUID_STR;
    static const char* CHAR_RX_UUID_STR;

    // lazily register pairing agent when needed
    bool ensure_cli_agent();

    // Expose resolved paths so callers can cache them in INI
    std::string get_device_path() const;
    std::string get_tx_char_path() const;
    std::string get_rx_char_path() const;

private:
    // D-Bus / BlueZ implementation 
    struct Impl;
    Impl* m_impl;

    // Notification plumbing 
    std::mutex m_notif_mutex;
    std::condition_variable m_notif_cv;
    std::vector<std::vector<uint8_t>> m_notif_queue;

    // Called from D-Bus signal handler
    void handle_notification(const uint8_t* value, size_t value_length);

    // Static trampoline for PropertiesChanged signals
    static void properties_changed_cb(GDBusConnection* connection,
                                      const gchar* sender_name,
                                      const gchar* object_path,
                                      const gchar* interface_name,
                                      const gchar* signal_name,
                                      GVariant* parameters,
                                      gpointer user_data);
									  
    // Helper to register a CLI Agent with BlueZ for PIN/passkey input
    bool register_cli_agent();									  
};
