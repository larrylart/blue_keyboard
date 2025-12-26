#include "ble_transport.h"
#include <gio/gio.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <algorithm>

static long long t_ms() {
    using namespace std::chrono;
    static auto t0 = steady_clock::now();
    auto now = steady_clock::now();
    return duration_cast<milliseconds>(now - t0).count();
}


const char* BleTransport::SERVICE_UUID_STR   = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const char* BleTransport::CHAR_TX_UUID_STR   = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const char* BleTransport::CHAR_RX_UUID_STR   = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

struct BleTransport::Impl {
    GDBusConnection* conn = nullptr;
    std::string adapter_path = "/org/bluez/hci0"; // default adapter

    std::string device_path;
    std::string tx_char_path;
    std::string rx_char_path;

    GMainLoop* loop = nullptr;
    std::thread loop_thread;
    guint rx_signal_sub_id = 0;
	
    // Agent registration
    GDBusNodeInfo* agent_node_info = nullptr;
    guint agent_reg_id = 0;
    bool agent_registered = false;	
};

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

// --- BlueZ Agent1 (for PIN/passkey entry on CLI) ---

static const gchar* AGENT_INTROSPECTION_XML =
    "<node>"
    "  <interface name='org.bluez.Agent1'>"
    "    <method name='Release'/>"
    "    <method name='RequestPinCode'>"
    "      <arg name='device' type='o' direction='in'/>"
    "      <arg name='pincode' type='s' direction='out'/>"
    "    </method>"
    "    <method name='DisplayPinCode'>"
    "      <arg name='device' type='o' direction='in'/>"
    "      <arg name='pincode' type='s' direction='in'/>"
    "    </method>"
    "    <method name='RequestPasskey'>"
    "      <arg name='device' type='o' direction='in'/>"
    "      <arg name='passkey' type='u' direction='out'/>"
    "    </method>"
    "    <method name='DisplayPasskey'>"
    "      <arg name='device' type='o' direction='in'/>"
    "      <arg name='passkey' type='u' direction='in'/>"
    "      <arg name='entered' type='q' direction='in'/>"
    "    </method>"
    "    <method name='RequestConfirmation'>"
    "      <arg name='device' type='o' direction='in'/>"
    "      <arg name='passkey' type='u' direction='in'/>"
    "    </method>"
    "    <method name='RequestAuthorization'>"
    "      <arg name='device' type='o' direction='in'/>"
    "    </method>"
    "    <method name='AuthorizeService'>"
    "      <arg name='device' type='o' direction='in'/>"
    "      <arg name='uuid' type='s' direction='in'/>"
    "    </method>"
    "    <method name='Cancel'/>"
    "  </interface>"
    "</node>";

// Forward declaration for Agent method handler
static void agent_method_call(GDBusConnection* connection,
                              const gchar* sender,
                              const gchar* object_path,
                              const gchar* interface_name,
                              const gchar* method_name,
                              GVariant* parameters,
                              GDBusMethodInvocation* invocation,
                              gpointer user_data);


// Forward declaration for Agent registration helper
static bool bluez_register_cli_agent(void* impl_void);


BleTransport::BleTransport()
    : m_impl(new Impl()) {

    GError* error = nullptr;
	m_impl->conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
	if (!m_impl->conn) {
		std::cerr << "Failed to connect to system D-Bus: "
				  << (error ? error->message : "unknown") << "\n";
		if (error) g_error_free(error);
		return;
	}

    // Register our CLI Agent so BlueZ asks us for PIN/passkey on the terminal
	// this is only needed for pairing - no need to do this here as it will slowdown the normal flow
//    if (!register_cli_agent()) {
//        std::cerr << "Warning: failed to register CLI Agent. "
//                     "Pairing may fall back to system agent or fail.\n";
//    }

	// Basic check that adapter exists...

	m_impl->loop = g_main_loop_new(nullptr, FALSE);
	m_impl->loop_thread = std::thread([this]() {
		g_main_loop_run(m_impl->loop);
	});

}

BleTransport::~BleTransport() {
    disconnect();

    if (m_impl) {
        if (m_impl->conn && m_impl->rx_signal_sub_id != 0) {
            g_dbus_connection_signal_unsubscribe(m_impl->conn,
                                                 m_impl->rx_signal_sub_id);
            m_impl->rx_signal_sub_id = 0;
        }

        if (m_impl->loop) {
            g_main_loop_quit(m_impl->loop);
            if (m_impl->loop_thread.joinable()) {
                m_impl->loop_thread.join();
            }
            g_main_loop_unref(m_impl->loop);
            m_impl->loop = nullptr;
        }

		if (m_impl->conn) {
			// Unregister Agent object if needed
			if (m_impl->agent_reg_id != 0) {
				g_dbus_connection_unregister_object(m_impl->conn, m_impl->agent_reg_id);
				m_impl->agent_reg_id = 0;
			}

			// Optionally tell BlueZ we're gone (best-effort, ignore errors)
			if (m_impl->agent_registered) {
				GError* error = nullptr;
				g_dbus_connection_call_sync(
					m_impl->conn,
					"org.bluez",
					"/org/bluez",
					"org.bluez.AgentManager1",
					"UnregisterAgent",
					g_variant_new("(o)", "/blukeyborg/agent"),
					nullptr,
					G_DBUS_CALL_FLAGS_NONE,
					5000,
					nullptr,
					&error
				);
				if (error) g_error_free(error);
			}

			g_object_unref(m_impl->conn);
			m_impl->conn = nullptr;
		}

		if (m_impl->agent_node_info) {
			g_dbus_node_info_unref(m_impl->agent_node_info);
			m_impl->agent_node_info = nullptr;
		}

		delete m_impl;
		m_impl = nullptr;

    }
}

// Helper: call BlueZ ObjectManager.GetManagedObjects
// Returns a GVariant* of type a{oa{sa{sv}}} (the inner dict), refcount = 1.
static GVariant* bluez_get_managed_objects(GDBusConnection* conn) {
    GError* error = nullptr;
    GVariant* tuple = g_dbus_connection_call_sync(
        conn,
        "org.bluez",
        "/",
        "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects",
        nullptr,
        G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        &error
    );
    if (!tuple) {
        std::cerr << "GetManagedObjects failed: "
                  << (error ? error->message : "unknown") << "\n";
        if (error) g_error_free(error);
        return nullptr;
    }

    // Correct: take child #0 as a new owning reference
    GVariant* dict = g_variant_get_child_value(tuple, 0);
    g_variant_unref(tuple);
    return dict;  // caller must g_variant_unref(dict) when done
}

// Helper: read a boolean property from org.bluez.Device1
static bool bluez_get_device_bool_prop(GDBusConnection* conn,
                                       const std::string& dev_path,
                                       const char* prop_name,
                                       bool& out_value) {
    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(
        conn,
        "org.bluez",
        dev_path.c_str(),
        "org.freedesktop.DBus.Properties",
        "Get",
        g_variant_new("(ss)", "org.bluez.Device1", prop_name),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        5000,      // 5s
        nullptr,
        &error
    );
    if (!result) {
        std::cerr << "Properties.Get(" << prop_name << ") failed on "
                  << dev_path << ": "
                  << (error ? error->message : "unknown") << "\n";
        if (error) g_error_free(error);
        return false;
    }

    GVariant* v = nullptr;
    g_variant_get(result, "(v)", &v);
    gboolean b = FALSE;
    g_variant_get(v, "b", &b);
    g_variant_unref(v);
    g_variant_unref(result);

    out_value = (b != FALSE);
    return true;
}

// Helper: read a string property from org.bluez.Device1
static bool bluez_get_device_str_prop(GDBusConnection* conn,
                                      const std::string& dev_path,
                                      const char* prop_name,
                                      std::string& out_value) {
    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(
        conn,
        "org.bluez",
        dev_path.c_str(),
        "org.freedesktop.DBus.Properties",
        "Get",
        g_variant_new("(ss)", "org.bluez.Device1", prop_name),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        nullptr,
        &error
    );
    if (!result) {
        std::cerr << "Properties.Get(" << prop_name << ") failed on "
                  << dev_path << ": "
                  << (error ? error->message : "unknown") << "\n";
        if (error) g_error_free(error);
        return false;
    }

    GVariant* v = nullptr;
    g_variant_get(result, "(v)", &v);
    const gchar* s = nullptr;
    g_variant_get(v, "s", &s);
    if (s) {
        out_value = s;
    } else {
        out_value.clear();
    }
    g_variant_unref(v);
    g_variant_unref(result);
    return !out_value.empty();
}

// Helper: read UUID from a GattCharacteristic1 object
static bool bluez_get_char_uuid(GDBusConnection* conn,
                                const std::string& char_path,
                                std::string& out_uuid) {
    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(
        conn,
        "org.bluez",
        char_path.c_str(),
        "org.freedesktop.DBus.Properties",
        "Get",
        g_variant_new("(ss)", "org.bluez.GattCharacteristic1", "UUID"),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        nullptr,
        &error
    );
    if (!result) {
        std::cerr << "Properties.Get(UUID) failed on "
                  << char_path << ": "
                  << (error ? error->message : "unknown") << "\n";
        if (error) g_error_free(error);
        return false;
    }

    GVariant* v = nullptr;
    g_variant_get(result, "(v)", &v);
    const gchar* s = nullptr;
    g_variant_get(v, "s", &s);
    if (s) {
        out_uuid = to_lower(s);
    } else {
        out_uuid.clear();
    }
    g_variant_unref(v);
    g_variant_unref(result);
    return !out_uuid.empty();
}

// Validate cached BlueZ paths for a given MAC and NUS UUIDs.
// Returns true if all paths exist and match the expected address/UUIDs.
static bool bluez_validate_cached_paths(GDBusConnection* conn,
                                        const std::string& address,
                                        const std::string& dev_path,
                                        const std::string& tx_char_path,
                                        const std::string& rx_char_path) {
    if (dev_path.empty() || tx_char_path.empty() || rx_char_path.empty()) {
        return false;
    }

    // Check Device1.Address matches
    std::string addr_prop;
    if (!bluez_get_device_str_prop(conn, dev_path, "Address", addr_prop)) {
        return false;
    }
    if (addr_prop != address) {
        std::cerr << "Cached device_path has different Address: "
                  << addr_prop << " != " << address << "\n";
        return false;
    }

    // Check characteristic UUIDs
    std::string tx_uuid_prop;
    if (!bluez_get_char_uuid(conn, tx_char_path, tx_uuid_prop)) {
        return false;
    }
    std::string rx_uuid_prop;
    if (!bluez_get_char_uuid(conn, rx_char_path, rx_uuid_prop)) {
        return false;
    }

    const std::string tx_uuid_expected = to_lower(BleTransport::CHAR_TX_UUID_STR);
    const std::string rx_uuid_expected = to_lower(BleTransport::CHAR_RX_UUID_STR);

    if (tx_uuid_prop != tx_uuid_expected || rx_uuid_prop != rx_uuid_expected) {
        std::cerr << "Cached TX/RX UUIDs mismatch expected NUS UUIDs.\n";
        return false;
    }

    return true;
}


// Helper: ensure the device is paired (call Device1.Pair if needed)
static bool bluez_ensure_paired(GDBusConnection* conn,
                                const std::string& dev_path) {
    bool paired = false;
    if (!bluez_get_device_bool_prop(conn, dev_path, "Paired", paired)) {
        std::cerr << "Could not read Device1.Paired property\n";
        return false;
    }
    if (paired) {
        // Already paired – nothing to do
        return true;
    }

    std::cerr << "Device not paired yet, calling Device1.Pair()…\n";

    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(
        conn,
        "org.bluez",
        dev_path.c_str(),
        "org.bluez.Device1",
        "Pair",
        nullptr,          // no args
        nullptr,          // no return type (expects '()')
        G_DBUS_CALL_FLAGS_NONE,
        60000,            // up to 60s – pairing can take a while
        nullptr,
        &error
    );
    if (!result) {
        std::cerr << "Device.Pair failed: "
                  << (error ? error->message : "unknown") << "\n";
        if (error) g_error_free(error);
        // Even if Pair failed, we’ll check Paired again below.
    } else {
        g_variant_unref(result);
    }

    // Poll Paired until it turns true or we time out
    const int MAX_WAIT_MS = 60000;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        auto now = std::chrono::steady_clock::now();
        int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed >= MAX_WAIT_MS) {
            std::cerr << "Timed out waiting for Paired == true\n";
            return false;
        }

        bool now_paired = false;
        if (!bluez_get_device_bool_prop(conn, dev_path, "Paired", now_paired)) {
            return false;
        }
        if (now_paired) {
            std::cerr << "Device successfully paired.\n";
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

// Register our CLI-based Agent with BlueZ so pairing PIN/passkey is asked on stdin.
bool BleTransport::register_cli_agent() {
    if (!m_impl || !m_impl->conn) return false;

    GError* error = nullptr;

    // Parse Agent1 introspection XML
    m_impl->agent_node_info = g_dbus_node_info_new_for_xml(AGENT_INTROSPECTION_XML, &error);
    if (!m_impl->agent_node_info) {
        std::cerr << "Failed to parse Agent introspection XML: "
                  << (error ? error->message : "unknown") << "\n";
        if (error) g_error_free(error);
        return false;
    }

    const GDBusInterfaceVTable vtable = {
        agent_method_call,
        nullptr,
        nullptr
    };

    // Register object /blukeyborg/agent implementing org.bluez.Agent1
    m_impl->agent_reg_id = g_dbus_connection_register_object(
        m_impl->conn,
        "/blukeyborg/agent",
        m_impl->agent_node_info->interfaces[0],
        &vtable,
        m_impl,        // user_data = Impl*
        nullptr,
        &error
    );
    if (m_impl->agent_reg_id == 0) {
        std::cerr << "Failed to register Agent object: "
                  << (error ? error->message : "unknown") << "\n";
        if (error) g_error_free(error);
        return false;
    }

    // Register Agent with BlueZ (capability: KeyboardOnly – we type PIN from dongle)
    GVariant* result = g_dbus_connection_call_sync(
        m_impl->conn,
        "org.bluez",
        "/org/bluez",
        "org.bluez.AgentManager1",
        "RegisterAgent",
        g_variant_new("(os)", "/blukeyborg/agent", "KeyboardOnly"),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        10000,
        nullptr,
        &error
    );
    if (!result) {
        std::cerr << "RegisterAgent failed: "
                  << (error ? error->message : "unknown") << "\n";
        if (error) g_error_free(error);
        return false;
    }
    g_variant_unref(result);

    // Make it the default Agent
    result = g_dbus_connection_call_sync(
        m_impl->conn,
        "org.bluez",
        "/org/bluez",
        "org.bluez.AgentManager1",
        "RequestDefaultAgent",
        g_variant_new("(o)", "/blukeyborg/agent"),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        10000,
        nullptr,
        &error
    );
    if (!result) {
        std::cerr << "RequestDefaultAgent failed: "
                  << (error ? error->message : "unknown") << "\n";
        if (error) g_error_free(error);
        return false;
    }
    g_variant_unref(result);

    m_impl->agent_registered = true;
    std::cerr << "CLI Agent registered as default Bluetooth agent.\n";
    return true;
}


// Agent1 method dispatcher – called by BlueZ during pairing
static void agent_method_call(GDBusConnection*,
                              const gchar* sender,
                              const gchar* object_path,
                              const gchar* interface_name,
                              const gchar* method_name,
                              GVariant* parameters,
                              GDBusMethodInvocation* invocation,
                              gpointer user_data) {
    (void)sender;
    (void)object_path;
    (void)interface_name;
    //auto* impl = static_cast<BleTransport::Impl*>(user_data);

    // Helper to just ACK a void method
    auto ack = [&]() {
        g_dbus_method_invocation_return_value(invocation, nullptr);
    };

    if (g_strcmp0(method_name, "Release") == 0) {
        std::cerr << "Agent.Release()\n";
        ack();
        return;
    }

    if (g_strcmp0(method_name, "RequestPinCode") == 0) {
        const gchar* dev = nullptr;
        g_variant_get(parameters, "(&o)", &dev);
        std::string device_path = dev ? dev : "";

        std::cout << "\n[PAIRING] Device " << device_path
                  << " requests PIN (shown on dongle screen).\n";
        std::cout << "Enter PIN: " << std::flush;

        std::string pin;
        std::getline(std::cin, pin);
        if (pin.empty()) {
            g_dbus_method_invocation_return_error(
                invocation,
                G_IO_ERROR,
                G_IO_ERROR_CANCELLED,
                "PIN entry cancelled (empty input)");
            return;
        }

        GVariant* reply = g_variant_new("(s)", pin.c_str());
        g_dbus_method_invocation_return_value(invocation, reply);
        return;
    }

    if (g_strcmp0(method_name, "RequestPasskey") == 0) {
        const gchar* dev = nullptr;
        g_variant_get(parameters, "(&o)", &dev);
        std::string device_path = dev ? dev : "";

        std::cout << "\n[PAIRING] Device " << device_path
                  << " requests numeric passkey (shown on dongle screen).\n";
        std::cout << "Enter passkey: " << std::flush;

        std::string pin;
        std::getline(std::cin, pin);
        if (pin.empty()) {
            g_dbus_method_invocation_return_error(
                invocation,
                G_IO_ERROR,
                G_IO_ERROR_CANCELLED,
                "Passkey entry cancelled (empty input)");
            return;
        }

        uint32_t passkey = 0;
        try {
            passkey = static_cast<uint32_t>(std::stoul(pin));
        } catch (...) {
            g_dbus_method_invocation_return_error(
                invocation,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_ARGUMENT,
                "Invalid passkey (not a number)");
            return;
        }

        GVariant* reply = g_variant_new("(u)", passkey);
        g_dbus_method_invocation_return_value(invocation, reply);
        return;
    }

    if (g_strcmp0(method_name, "DisplayPasskey") == 0) {
        const gchar* dev = nullptr;
        guint32 passkey = 0;
        guint16 entered = 0;
        g_variant_get(parameters, "(&ouq)", &dev, &passkey, &entered);
        std::cerr << "[PAIRING] DisplayPasskey dev=" << (dev ? dev : "")
                  << " passkey=" << passkey
                  << " entered=" << entered << "\n";
        ack();
        return;
    }

    if (g_strcmp0(method_name, "DisplayPinCode") == 0) {
        const gchar* dev = nullptr;
        const gchar* pincode = nullptr;
        g_variant_get(parameters, "(&os)", &dev, &pincode);
        std::cerr << "[PAIRING] DisplayPinCode dev=" << (dev ? dev : "")
                  << " pincode=" << (pincode ? pincode : "") << "\n";
        ack();
        return;
    }

    if (g_strcmp0(method_name, "RequestConfirmation") == 0) {
        const gchar* dev = nullptr;
        guint32 passkey = 0;
        g_variant_get(parameters, "(&ou)", &dev, &passkey);
        std::cout << "\n[PAIRING] Confirm passkey " << passkey
                  << " for device " << (dev ? dev : "") << " [auto-accept].\n";
        // Auto-accept
        ack();
        return;
    }

    if (g_strcmp0(method_name, "RequestAuthorization") == 0) {
        const gchar* dev = nullptr;
        g_variant_get(parameters, "(&o)", &dev);
        std::cout << "\n[PAIRING] RequestAuthorization for device "
                  << (dev ? dev : "") << " [auto-accept].\n";
        ack();
        return;
    }

    if (g_strcmp0(method_name, "AuthorizeService") == 0) {
        const gchar* dev = nullptr;
        const gchar* uuid = nullptr;
        g_variant_get(parameters, "(&os)", &dev, &uuid);
        std::cout << "\n[PAIRING] AuthorizeService dev=" << (dev ? dev : "")
                  << " uuid=" << (uuid ? uuid : "") << " [auto-accept].\n";
        ack();
        return;
    }

    if (g_strcmp0(method_name, "Cancel") == 0) {
        std::cerr << "[PAIRING] Agent.Cancel()\n";
        ack();
        return;
    }

    // Unknown / unhandled method
    std::cerr << "Agent: unhandled method " << method_name << "\n";
    g_dbus_method_invocation_return_error(
        invocation,
        G_IO_ERROR,
        G_IO_ERROR_NOT_SUPPORTED,
        "Method not implemented");
}

std::vector<BleDeviceInfo> BleTransport::scan(int timeout_ms) {
    std::vector<BleDeviceInfo> devices;
    if (!m_impl || !m_impl->conn) return devices;

    GError* error = nullptr;

    // Start discovery
    g_dbus_connection_call_sync(
        m_impl->conn,
        "org.bluez",
        m_impl->adapter_path.c_str(),
        "org.bluez.Adapter1",
        "StartDiscovery",
        nullptr,
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        timeout_ms,
        nullptr,
        &error
    );
    if (error) {
        std::cerr << "StartDiscovery failed: " << error->message << "\n";
        g_error_free(error);
        error = nullptr;
    }

    // Give BlueZ some time to discover devices
    if (timeout_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
    }

    // Get known objects and filter Device1s
	GVariant* managed = bluez_get_managed_objects(m_impl->conn);
	if (managed) {
		GVariantIter iter;
		g_variant_iter_init(&iter, managed);

		const gchar* obj_path;
		GVariant* ifaces;

		while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &obj_path, &ifaces)) {
			GVariant* dev_props = g_variant_lookup_value(
				ifaces, "org.bluez.Device1", G_VARIANT_TYPE("a{sv}"));
			if (dev_props) {
				const gchar* addr = nullptr;
				const gchar* name = nullptr;
				g_variant_lookup(dev_props, "Address", "&s", &addr);
				g_variant_lookup(dev_props, "Name", "&s", &name);

				if (addr) {
					BleDeviceInfo info;
					info.address = addr;
					info.name = name ? name : "";
					devices.push_back(std::move(info));
				}
				g_variant_unref(dev_props);
			}
			g_variant_unref(ifaces);
		}

		g_variant_unref(managed);
	}


    // Stop discovery
    g_dbus_connection_call_sync(
        m_impl->conn,
        "org.bluez",
        m_impl->adapter_path.c_str(),
        "org.bluez.Adapter1",
        "StopDiscovery",
        nullptr,
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        &error
    );
    if (error) {
        // Not fatal if StopDiscovery fails (e.g., already stopped)
        g_error_free(error);
        error = nullptr;
    }

    return devices;
}

void BleTransport::disconnect() {
    if (!m_impl || !m_impl->conn) return;

    // Stop notifications if any
    if (!m_impl->rx_char_path.empty()) {
        GError* error = nullptr;
        g_dbus_connection_call_sync(
            m_impl->conn,
            "org.bluez",
            m_impl->rx_char_path.c_str(),
            "org.bluez.GattCharacteristic1",
            "StopNotify",
            nullptr,
            nullptr,
            G_DBUS_CALL_FLAGS_NONE,
            2000,
            nullptr,
            &error
        );
        if (error) {
            // Not fatal
            g_error_free(error);
        }
    }

    // Unsubscribe from PropertiesChanged
    if (m_impl->conn && m_impl->rx_signal_sub_id != 0) {
        g_dbus_connection_signal_unsubscribe(m_impl->conn, m_impl->rx_signal_sub_id);
        m_impl->rx_signal_sub_id = 0;
    }

    // Disconnect Device
    if (!m_impl->device_path.empty()) {
        GError* error = nullptr;
        g_dbus_connection_call_sync(
            m_impl->conn,
            "org.bluez",
            m_impl->device_path.c_str(),
            "org.bluez.Device1",
            "Disconnect",
            nullptr,
            nullptr,
            G_DBUS_CALL_FLAGS_NONE,
            5000,
            nullptr,
            &error
        );
        if (error) {
            // Might already be disconnected; ignore
            g_error_free(error);
        }
    }

    m_impl->device_path.clear();
    m_impl->tx_char_path.clear();
    m_impl->rx_char_path.clear();

    std::lock_guard<std::mutex> lock(m_notif_mutex);
    m_notif_queue.clear();
}

bool BleTransport::connect(const std::string& address,
                           bool ensure_paired,
                           const std::string& dev_hint,
                           const std::string& tx_hint,
                           const std::string& rx_hint) {
    if (!m_impl || !m_impl->conn) return false;

    std::cerr << "[T+" << t_ms() << "ms] connect() start, addr=" << address
              << " ensure_paired=" << ensure_paired << "\n";

    // Clean up any previous connection
    disconnect();

    std::string target = address;

    // First try cached paths if provided
    bool used_cache = false;
    if (!dev_hint.empty() && !tx_hint.empty() && !rx_hint.empty()) {
        if (bluez_validate_cached_paths(m_impl->conn, target,
                                        dev_hint, tx_hint, rx_hint)) {
            std::cerr << "[T+" << t_ms() << "ms] using cached paths\n";
            m_impl->device_path  = dev_hint;
            m_impl->tx_char_path = tx_hint;
            m_impl->rx_char_path = rx_hint;
            used_cache = true;
        } else {
            std::cerr << "[T+" << t_ms() << "ms] cached paths invalid, will discover\n";
        }
    }

    // If cache is not usable, do full discovery via GetManagedObjects
    if (!used_cache) 
	{
		std::cerr << "[T+" << t_ms() << "ms] starting full discovery (GetManagedObjects)\n";
        GVariant* managed = bluez_get_managed_objects(m_impl->conn);
        if (!managed) return false;

        GVariantIter iter;
        g_variant_iter_init(&iter, managed);

        const gchar* obj_path;
        GVariant* ifaces;
        std::string dev_path;

        while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &obj_path, &ifaces)) {
            GVariant* dev_props = g_variant_lookup_value(
                ifaces, "org.bluez.Device1", G_VARIANT_TYPE("a{sv}"));
            if (dev_props) {
                const gchar* addr = nullptr;
                g_variant_lookup(dev_props, "Address", "&s", &addr);
                if (addr && target == addr) {
                    dev_path = obj_path;
                    g_variant_unref(dev_props);
                    g_variant_unref(ifaces);
                    break;
                }
                g_variant_unref(dev_props);
            }
            g_variant_unref(ifaces);
        }

        g_variant_unref(managed);

        if (dev_path.empty()) {
			std::cerr << "[T+" << t_ms() << "ms] device not found in managed objects\n";
            std::cerr << "Device " << address << " not found in BlueZ objects.\n";
            return false;
        }

		std::cerr << "[T+" << t_ms() << "ms] discovered device_path=" << dev_path << "\n";

        m_impl->device_path.clear();
        m_impl->tx_char_path.clear();
        m_impl->rx_char_path.clear();

        m_impl->device_path = dev_path;

        // ensure device is paired before we try to use it
        if (ensure_paired) {
            if (!bluez_ensure_paired(m_impl->conn, m_impl->device_path)) {
                std::cerr << "Pairing failed or was cancelled.\n";
                m_impl->device_path.clear();
                return false;
            }
        }

		std::cerr << "[T+" << t_ms() << "ms] calling Device.Connect (discovery path)\n";
		// Connect
		GError* error = nullptr;
		g_dbus_connection_call_sync(
			m_impl->conn,
			"org.bluez",                 // bus_name
			m_impl->device_path.c_str(), // object_path
			"org.bluez.Device1",         // interface
			"Connect",                   // method
			nullptr,                     // parameters
			nullptr,                     // reply_type
			G_DBUS_CALL_FLAGS_NONE,
			15000,
			nullptr,
			&error
		);

        if (error) {
			std::cerr << "[T+" << t_ms() << "ms] Device.Connect failed\n";
            std::cerr << "Device.Connect failed: " << error->message << "\n";
            g_error_free(error);
            m_impl->device_path.clear();
            return false;
        }
		
		std::cerr << "[T+" << t_ms() << "ms] Device.Connect OK, doing char discovery\n";
		
        // Discover NUS characteristics (TX/RX) under this device
        managed = bluez_get_managed_objects(m_impl->conn);
        if (!managed) {
			std::cerr << "[T+" << t_ms() << "ms] GetManagedObjects (for chars) failed\n";
            disconnect();
            return false;
        }

		std::cerr << "[T+" << t_ms() << "ms] got managed objects for char discovery\n";

        GVariantIter iter2;
        g_variant_iter_init(&iter2, managed);

        const std::string service_uuid = to_lower(SERVICE_UUID_STR);
        const std::string tx_uuid      = to_lower(CHAR_TX_UUID_STR);
        const std::string rx_uuid      = to_lower(CHAR_RX_UUID_STR);

        const gchar* obj_path2;
        GVariant* ifaces2;

        while (g_variant_iter_next(&iter2, "{&o@a{sa{sv}}}", &obj_path2, &ifaces2)) {
            std::string path_str(obj_path2);
            if (path_str.rfind(m_impl->device_path, 0) != 0) {
                g_variant_unref(ifaces2);
                continue;
            }

            GVariant* ch_props = g_variant_lookup_value(
                ifaces2, "org.bluez.GattCharacteristic1", G_VARIANT_TYPE("a{sv}"));
            if (ch_props) {
                const gchar* uuid = nullptr;
                g_variant_lookup(ch_props, "UUID", "&s", &uuid);
                if (uuid) {
                    std::string u = to_lower(uuid);
                    if (u == tx_uuid) {
                        m_impl->tx_char_path = path_str;
                    } else if (u == rx_uuid) {
                        m_impl->rx_char_path = path_str;
                    }
                }
                g_variant_unref(ch_props);
            }

            g_variant_unref(ifaces2);
        }

        g_variant_unref(managed);

        if (m_impl->tx_char_path.empty() || m_impl->rx_char_path.empty()) {
			std::cerr << "[T+" << t_ms() << "ms] failed to find TX/RX chars\n";
            //std::cerr << "Could not find NUS TX/RX characteristics on device.\n";
            disconnect();
            return false;
        }
		
		std::cerr << "[T+" << t_ms() << "ms] found TX=" << m_impl->tx_char_path
                  << " RX=" << m_impl->rx_char_path << "\n";
				  
    } else {
		
		std::cerr << "[T+" << t_ms() << "ms] using cached device_path=" << m_impl->device_path << "\n";
		
        // Cache path: we still need to ensure pairing (if requested) and connect.
        if (ensure_paired) {
            if (!bluez_ensure_paired(m_impl->conn, m_impl->device_path)) {
                std::cerr << "Pairing failed or was cancelled.\n";
                m_impl->device_path.clear();
                m_impl->tx_char_path.clear();
                m_impl->rx_char_path.clear();
                return false;
            }
        }

		std::cerr << "[T+" << t_ms() << "ms] calling Device.Connect (cached path)\n";

        GError* error = nullptr;
        g_dbus_connection_call_sync(
            m_impl->conn,
            "org.bluez",
            m_impl->device_path.c_str(),
            "org.bluez.Device1",
            "Connect",
            nullptr,
            nullptr,
            G_DBUS_CALL_FLAGS_NONE,
            15000,
            nullptr,
            &error
        );
        if (error) 
		{
			std::cerr << "[T+" << t_ms() << "ms] Device.Connect (cached) failed\n";
			
            std::cerr << "Device.Connect (cached) failed: "
                      << error->message << "\n";
            g_error_free(error);
            m_impl->device_path.clear();
            m_impl->tx_char_path.clear();
            m_impl->rx_char_path.clear();
            return false;
        }
		
		std::cerr << "[T+" << t_ms() << "ms] Device.Connect (cached) OK\n";
    }

	// Enable notifications on RX (same for both paths)
	GError* error = nullptr;
	std::cerr << "[T+" << t_ms() << "ms] calling StartNotify\n";
	g_dbus_connection_call_sync(
		m_impl->conn,
		"org.bluez",                 // bus_name
		m_impl->rx_char_path.c_str(),// object_path
		"org.bluez.GattCharacteristic1",
		"StartNotify",
		nullptr,
		nullptr,
		G_DBUS_CALL_FLAGS_NONE,
		5000,
		nullptr,
		&error
	);

    if (error) {
		std::cerr << "[T+" << t_ms() << "ms] StartNotify failed\n";
        //std::cerr << "StartNotify failed: " << error->message << "\n";
        g_error_free(error);
        disconnect();
        return false;
    }

	std::cerr << "[T+" << t_ms() << "ms] StartNotify OK, subscribing to PropertiesChanged\n";

    // Subscribe to PropertiesChanged on RX characteristic (Value updates)
    m_impl->rx_signal_sub_id = g_dbus_connection_signal_subscribe(
        m_impl->conn,
        "org.bluez",
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        m_impl->rx_char_path.c_str(),
        nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        &BleTransport::properties_changed_cb,
        this,
        nullptr
    );

    if (m_impl->rx_signal_sub_id == 0) {
        std::cerr << "Failed to subscribe to PropertiesChanged for RX.\n";
        disconnect();
        return false;
    }

	std::cerr << "[T+" << t_ms() << "ms] connect() done\n";
    return true;
}

bool BleTransport::write_tx(const std::vector<uint8_t>& data) {
    if (!m_impl || !m_impl->conn || m_impl->tx_char_path.empty()) return false;

    // Build the "value" (type: ay)
    GVariant* value = g_variant_new_fixed_array(
        G_VARIANT_TYPE_BYTE,
        data.data(),
        data.size(),
        sizeof(guint8)
    );

    // Build an empty "options" dict (type: a{sv})
    GVariant* options = g_variant_new_array(
        G_VARIANT_TYPE("{sv}"),
        nullptr,
        0
    );

    // Combine into a tuple (ay, a{sv}) for WriteValue
    GVariant* children[2] = { value, options };
    GVariant* params = g_variant_new_tuple(children, 2);

    GError* error = nullptr;
    g_dbus_connection_call_sync(
        m_impl->conn,
        "org.bluez",
        m_impl->tx_char_path.c_str(),
        "org.bluez.GattCharacteristic1",
        "WriteValue",
        params,
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        10000,
        nullptr,
        &error
    );
    if (error) {
        std::cerr << "WriteValue failed: " << error->message << "\n";
        g_error_free(error);
        return false;
    }
    return true;
}

// Called from GLib thread
void BleTransport::handle_notification(const uint8_t* value, size_t value_length) {
    std::lock_guard<std::mutex> lock(m_notif_mutex);
    m_notif_queue.emplace_back(value, value + value_length);
    m_notif_cv.notify_all();
}

// Static D-Bus signal handler
void BleTransport::properties_changed_cb(GDBusConnection*,
                                         const gchar*,
                                         const gchar* /*object_path*/,
                                         const gchar* interface_name,
                                         const gchar*,
                                         GVariant* parameters,
                                         gpointer user_data) {
											 
	(void)interface_name;
    auto* self = static_cast<BleTransport*>(user_data);
    if (!self) return;

    // We only care about org.bluez.GattCharacteristic1 Value changes
	const gchar* iface = nullptr;
	GVariant* changed = nullptr;
	GVariant* invalidated = nullptr;

	// Note the @ signs:
	g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed, &invalidated);
	if (std::string(iface) != "org.bluez.GattCharacteristic1") {
		g_variant_unref(changed);
		g_variant_unref(invalidated);
		return;
	}

    GVariant* val = g_variant_lookup_value(changed, "Value", G_VARIANT_TYPE("ay"));
    if (val) {
        gsize len = 0;
        const guint8* data = static_cast<const guint8*>(
            g_variant_get_fixed_array(val, &len, sizeof(guint8)));
        if (data && len > 0) {
            self->handle_notification(data, len);
        }
        g_variant_unref(val);
    }

    g_variant_unref(changed);
    g_variant_unref(invalidated);
}

std::optional<std::vector<uint8_t>> BleTransport::wait_notification(int timeout_ms) {
    std::unique_lock<std::mutex> lock(m_notif_mutex);
    if (!m_notif_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                             [this]{ return !m_notif_queue.empty(); })) {
        return std::nullopt;
    }
    auto data = m_notif_queue.front();
    m_notif_queue.erase(m_notif_queue.begin());
    return data;
}

bool BleTransport::ensure_cli_agent() 
{
    if (!m_impl || !m_impl->conn) return false;
    if (m_impl->agent_registered) return true;
    return register_cli_agent();
}

std::string BleTransport::get_device_path() const {
    if (!m_impl) return {};
    return m_impl->device_path;
}

std::string BleTransport::get_tx_char_path() const {
    if (!m_impl) return {};
    return m_impl->tx_char_path;
}

std::string BleTransport::get_rx_char_path() const {
    if (!m_impl) return {};
    return m_impl->rx_char_path;
}
