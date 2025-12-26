#include "ble_proto.h"
#include <iostream>
#include <filesystem>
#include <cstdlib>

using namespace std;

static void usage(const char* prog) {
    cerr << "Usage:\n"
         << "  " << prog << " --list\n"
         << "  " << prog << " --prov=<mac>\n"
         << "  " << prog << " --sendstr=<text> --to=<mac> [--newline]\n"
         << "  " << prog << " --sendkey=<usage> --to=<mac> [--mods=<mods>] [--repeat=<n>]\n"
         << "\n"
         << "INI file: ./blukeyborg_ini in current working directory\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    string arg1 = argv[1];

    // INI file 
    string ini_path = "blukeyborg.data";
    BluKeySession session(ini_path);

    if (arg1 == string("--list")) {
        auto devices = session.list_devices();
        for (const auto& d : devices) {
            cout << d.address << "  " << d.name << "\n";
        }
        return 0;
    }

    string prov_mac;
    string send_text;
    string send_to;
    string sendkey_str;
    int   mods        = 0;
    int   repeat      = 1;
    bool  add_newline = false;

    for (int i = 1; i < argc; ++i) {
        string a  = argv[i];
        auto eq   = a.find('=');
        string key = (eq == string::npos) ? a : a.substr(0, eq);
        string val = (eq == string::npos) ? "" : a.substr(eq + 1);

        if (key == "--prov") {
            prov_mac = val;
        } else if (key == "--sendstr") {
            send_text = val;
        } else if (key == "--to") {
            send_to = val;
        } else if (key == "--sendkey") {
            sendkey_str = val;
        } else if (key == "--mods") {
            mods = std::atoi(val.c_str());
        } else if (key == "--repeat") {
            repeat = std::atoi(val.c_str());
            if (repeat <= 0) {
                repeat = 1;
            }
        } else if (key == "--newline") {
            add_newline = true;
        }
    }

    if (!prov_mac.empty()) {
        if (session.provision(prov_mac)) {
            return 0;
        }
        return 1;
    }

    if (!send_text.empty() && !send_to.empty()) {
        if (session.send_string(send_to, send_text, add_newline)) {
            return 0;
        }
        return 1;
    }

    if (!sendkey_str.empty() && !send_to.empty()) {
        int usage = std::atoi(sendkey_str.c_str());
        if (usage <= 0 || usage > 255) {
            cerr << "Invalid usage code\n";
            return 1;
        }
        if (session.send_key(send_to,
                             static_cast<uint8_t>(usage),
                             static_cast<uint8_t>(mods),
                             static_cast<uint8_t>(repeat))) {
            return 0;
        }
        return 1;
    }

    usage(argv[0]);
    return 1;
}
