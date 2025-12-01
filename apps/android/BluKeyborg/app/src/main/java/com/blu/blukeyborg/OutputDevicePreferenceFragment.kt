////////////////////////////////////////////////////////////////////
// OutputDevicePreferenceFragment
//
// Settings screen responsible for:
//
//  - Listing nearby BLE devices (via BluetoothDeviceManager)
//  - Showing current selection + pairing state
//  - Letting the user toggle "Use external keyboard device" on/off
//  - Handling pairing/unpairing
//  - Initiating BleHub "connect + handshake" from the Settings screen
//  - Providing a password prompt UI for APPKEY provisioning
//  - Updating the keyboard layout on the dongle (binary MTLS C0/C1/C2 ops)
//
// This fragment is the **UI layer**; all BLE logic happens in:
//     BluetoothDeviceManager  – GATT + scanning
//     BleHub                  – provisioning + MTLS + command API
//
// NOTE: A large portion of the logic in this file is tightly coupled
//       to BlueKeyboard firmware. Some of it may eventually belong
//       in a dedicated UI-helper class.
//
// IMPORTANT: Settings-based connects **allowPrompt = true**, while
//            app-start auto-connects do *not* show password dialogs.
////////////////////////////////////////////////////////////////////
package com.blu.blukeyborg

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Bundle
import androidx.preference.PreferenceFragmentCompat
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.preference.Preference
import com.blu.blukeyborg.R
import com.blu.blukeyborg.BluetoothDeviceManager
import com.blu.blukeyborg.BtDevice
import android.bluetooth.BluetoothDevice
import com.blu.blukeyborg.OutputDeviceRowPreference
import com.google.android.material.button.MaterialButton

import androidx.preference.PreferenceViewHolder
import android.util.AttributeSet
import com.blu.blukeyborg.BleHub
import com.blu.blukeyborg.SimpleDropdownPreference

import android.os.Handler
import android.os.Looper

import android.util.Log

class OutputDevicePreferenceFragment : PreferenceFragmentCompat() {

    private lateinit var manager: BluetoothDeviceManager

    private lateinit var rowPref: OutputDeviceRowPreference
    private lateinit var actionPref: ActionButtonStartPreference

    private val KEY_ACTION = "pref_output_dongle_connect"

    // Made nullable so the screen can work even if simple XML omits them
    private var deviceTypePref: SimpleDropdownPreference? = null
    private var layoutPref: SimpleDropdownPreference? = null

    // volume action dropdowns
    private var volumeUpPref: SimpleDropdownPreference? = null
    private var volumeDownPref: SimpleDropdownPreference? = null

    private val reqPerms = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { _ -> refreshStart() }

    ////////////////////////////////////////////////////////////////////
    // Receiver for system bond-state changes.
    ////////////////////////////////////////////////////////////////////
    private val bondReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (BluetoothDevice.ACTION_BOND_STATE_CHANGED != intent.action) return

            val dev = intent.getParcelableExtra<BluetoothDevice>(BluetoothDevice.EXTRA_DEVICE) ?: return
            val was = intent.getIntExtra(BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE, BluetoothDevice.BOND_NONE)
            val now = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)

            try { manager.refreshBonded() } catch (_: SecurityException) {}
            updateActionRow(PreferencesUtil.getOutputDeviceId(requireContext()))

            val selected = PreferencesUtil.getOutputDeviceId(requireContext())
            if (dev.address == selected && was != BluetoothDevice.BOND_BONDED && now == BluetoothDevice.BOND_BONDED) {
                BleHub.connectFromSettings { ok, err ->
                    if (!ok) {
                        Toast.makeText(requireContext(), err ?: "Failed to connect", Toast.LENGTH_SHORT).show()
                    }
                }
            }
        }
    }

    private fun postUI(block: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) block()
        else Handler(Looper.getMainLooper()).post(block)
    }

    ////////////////////////////////////////////////////////////////////
    // onCreatePreferences
    ////////////////////////////////////////////////////////////////////
    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        // ensure BleHub holds app context + has a password prompt EARLY
        BleHub.init(requireContext())

        // Register a UI callback for BleHub that can show a password dialog.
        BleHub.setPasswordPrompt { _, reply ->
            postUI {
                val activity = requireActivity()
                val edit = android.widget.EditText(activity).apply {
                    inputType = android.text.InputType.TYPE_CLASS_TEXT or
                            android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD
                    hint = "App password"
                }
                androidx.appcompat.app.AlertDialog.Builder(activity)
                    .setTitle("Secure the dongle")
                    .setMessage("Enter the dongle password")
                    .setView(edit)
                    .setPositiveButton("OK") { _, _ -> reply(edit.text.toString().toCharArray()) }
                    .setNegativeButton("Cancel") { _, _ -> reply(null) }
                    .show()
            }
        }

        setPreferencesFromResource(R.xml.preferences_output_device, rootKey)
        manager = BluetoothDeviceManager(requireContext())

        // --- Optional device type selector (can be absent in simple XML) ---
        deviceTypePref = findPreference(getString(R.string.pref_device_type_key))
        deviceTypePref?.let { pref ->
            val deviceTypeEntries = listOf(getString(R.string.device_type_blue_kb))
            val deviceTypeValues  = listOf("BLUE_KB")
            val savedDeviceType   = PreferencesUtil.getDeviceType(requireContext()) ?: "BLUE_KB"
            pref.setData(deviceTypeEntries, deviceTypeValues, savedDeviceType)
            pref.onSelected = { value, _ ->
                PreferencesUtil.setDeviceType(requireContext(), value)
            }
        }

        // rowPref: dropdown of discovered devices + on/off switch (required)
        // actionPref: left-side "Pair / Unpair / Connect" button (required)
        rowPref = requireNotNull(findPreference("pref_output_dongle_row")) {
            "pref_output_dongle_row missing from preferences_output_device.xml"
        }
        actionPref = requireNotNull(findPreference<ActionButtonStartPreference>(KEY_ACTION)) {
            "$KEY_ACTION missing from preferences_output_device.xml"
        }

        //////////////////////////
        // Keyboard Layout (optional in simple build)
        layoutPref = findPreference(getString(R.string.pref_keyboard_layout_key))
        layoutPref?.let { lp ->
            val (layoutEntries, layoutValues) = keyboardLayoutOptions()
            val savedLayout = PreferencesUtil.getKeyboardLayout(requireContext())
            lp.setData(layoutEntries, layoutValues, savedLayout)

            // :: Keyboard layout selector (string code)
            lp.onSelected = { value, _ ->
                val ctx = requireContext()
                val prev = PreferencesUtil.getKeyboardLayout(ctx)
                val address = PreferencesUtil.getOutputDeviceId(ctx)

                if (address.isNullOrBlank()) {
                    Toast.makeText(ctx, R.string.msg_no_device_selected, Toast.LENGTH_SHORT).show()
                    lp.setSelectedValue(prev)
                } else {
                    BleHub.setLayoutString(value) { ok, err ->
                        postUI {
                            if (ok) {
                                PreferencesUtil.setKeyboardLayout(ctx, value)
                                Toast.makeText(ctx, R.string.msg_layout_set_ok, Toast.LENGTH_SHORT).show()
                            } else {
                                lp.setSelectedValue(prev)
                                val msg = if (err.isNullOrBlank()) getString(R.string.msg_layout_set_failed)
                                else getString(R.string.msg_layout_set_failed) + ": " + err
                                Toast.makeText(ctx, msg, Toast.LENGTH_LONG).show()
                            }
                        }
                    }
                }
            }
        }

        //////////////////////////
        // Volume key actions
        //////////////////////////

        val volumeEntries = resources.getStringArray(R.array.volume_action_entries).toList()
        val volumeValues  = resources.getStringArray(R.array.volume_action_values).toList()

        volumeUpPref = findPreference(getString(R.string.pref_volume_up_action_key))
        volumeUpPref?.let { up ->
            val saved = PreferencesUtil.getVolumeUpAction(requireContext())
            up.setData(volumeEntries, volumeValues, saved)
            up.onSelected = { value, _ ->
                PreferencesUtil.setVolumeUpAction(requireContext(), value)
            }
        }

        volumeDownPref = findPreference(getString(R.string.pref_volume_down_action_key))
        volumeDownPref?.let { down ->
            val saved = PreferencesUtil.getVolumeDownAction(requireContext())
            down.setData(volumeEntries, volumeValues, saved)
            down.onSelected = { value, _ ->
                PreferencesUtil.setVolumeDownAction(requireContext(), value)
            }
        }


        // Initial enable state from global setting
        val enabled = PreferencesUtil.useExternalKeyboardDevice(requireContext())
        rowPref.setSwitchChecked(enabled)
        actionPref.isEnabled = enabled
        deviceTypePref?.isEnabled = enabled
        layoutPref?.isEnabled = enabled
        rowPref.setDropdownEnabled(enabled)

        volumeUpPref?.isEnabled = enabled
        volumeDownPref?.isEnabled = enabled


        // :: Master toggle: "Use External Keyboard Device"
        rowPref.onToggleChanged = { isChecked ->

            rowPref.setDropdownEnabled(isChecked)
            actionPref.isEnabled = isChecked
            deviceTypePref?.isEnabled = isChecked
            layoutPref?.isEnabled = isChecked

			volumeUpPref?.isEnabled = isChecked
			volumeDownPref?.isEnabled = isChecked

            if (!isChecked) {
                PreferencesUtil.setUseExternalKeyboardDevice(requireContext(), false)
                PreferencesUtil.setOutputDeviceDisabledByError(requireContext(), false)
                BleHub.disconnect()
            } else {
                val addr = PreferencesUtil.getOutputDeviceId(requireContext())
                if (addr.isNullOrBlank()) {
                    Toast.makeText(requireContext(), R.string.msg_no_device_selected, Toast.LENGTH_SHORT).show()
                    rowPref.setSwitchChecked(false)
                    rowPref.setDropdownEnabled(false)
                    actionPref.isEnabled = false
                    deviceTypePref?.isEnabled = false
                    layoutPref?.isEnabled = false
					
                    volumeUpPref?.isEnabled = false
                    volumeDownPref?.isEnabled = false					
					
                } else {
                    BleHub.connectFromSettings { ok, _ ->
                        postUI {
                            if (!ok) {
                                Toast.makeText(requireContext(), R.string.msg_failed_connect_device, Toast.LENGTH_SHORT).show()
                                rowPref.setSwitchChecked(false)
                                actionPref.isEnabled = false
                                deviceTypePref?.isEnabled = false
                                layoutPref?.isEnabled = false
                            } else {
                                PreferencesUtil.setUseExternalKeyboardDevice(requireContext(), true)
                                PreferencesUtil.setOutputDeviceDisabledByError(requireContext(), false)

                                BleHub.getLayout { okL, layoutId, _ ->
                                    postUI {
                                        if (okL && layoutId != null) {
                                            val saved = PreferencesUtil.getKeyboardLayout(requireContext())
                                            if (!saved.isNullOrBlank()) {
                                                layoutPref?.setSelectedValue(saved)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // :: onDeviceSelected
        rowPref.onDeviceSelected = { address, _label ->
            val name = manager.devices.value.orEmpty()
                .firstOrNull { it.address == address }?.name.orEmpty()

            PreferencesUtil.setOutputDeviceId(requireContext(), address)
            PreferencesUtil.setOutputDeviceName(requireContext(), name)
            updateActionRow(address)

            if (PreferencesUtil.useExternalKeyboardDevice(requireContext())) {
                BleHub.disconnect()
                BleHub.connectFromSettings { ok, _ ->
                    postUI {
                        if (!ok) {
                            Toast.makeText(requireContext(), R.string.msg_failed_connect_device, Toast.LENGTH_SHORT).show()
                            rowPref.setSwitchChecked(false)
                            actionPref.isEnabled = false
                            deviceTypePref?.isEnabled = false
                            layoutPref?.isEnabled = false
                        } else {
                            PreferencesUtil.setUseExternalKeyboardDevice(requireContext(), true)
                            PreferencesUtil.setOutputDeviceDisabledByError(requireContext(), false)

                            BleHub.getLayout { okL, layoutId, _ ->
                                postUI {
                                    if (okL && layoutId != null) {
                                        val saved = PreferencesUtil.getKeyboardLayout(requireContext())
                                        if (!saved.isNullOrBlank()) {
                                            layoutPref?.setSelectedValue(saved)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Preload saved selection into the row/action visibility
        val saved = PreferencesUtil.getOutputDeviceId(requireContext())
        updateActionRow(saved)

        // Pair/unpair click (row tap)
        actionPref.setOnPreferenceClickListener {
            handlePairUnpairClick()
            true
        }

        // Pair/unpair click (button on the left)
        actionPref.onButtonClick = {
            handlePairUnpairClick()
        }
    }

    ////////////////////////////////////////////////////////////////////
    // Handles pair/unpair from both row + button.
    ////////////////////////////////////////////////////////////////////
    private fun handlePairUnpairClick() {
        val addr = PreferencesUtil.getOutputDeviceId(requireContext()).orEmpty()
        if (addr.isBlank()) {
            Toast.makeText(requireContext(), R.string.msg_select_first, Toast.LENGTH_SHORT).show()
            return
        }

        val bonded = currentIsBonded(addr)
        val ok = if (bonded) manager.unpair(addr) else manager.pair(addr)

        if (!ok) {
            Toast.makeText(
                requireContext(),
                if (bonded) R.string.msg_unpair_failed else R.string.msg_pair_failed,
                Toast.LENGTH_SHORT
            ).show()
        } else {
            Toast.makeText(
                requireContext(),
                if (bonded) R.string.msg_unpairing else R.string.msg_pairing,
                Toast.LENGTH_SHORT
            ).show()
        }
    }

    ////////////////////////////////////////////////////////////////////
    // Lifecycle
    ////////////////////////////////////////////////////////////////////
    override fun onStart() {
        super.onStart()
        val perms = neededPermissions()
        if (perms.isEmpty()) refreshStart() else reqPerms.launch(perms)
        requireContext().registerReceiver(
            bondReceiver,
            IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
        )

        // refresh layout dropdown from prefs on (re)entry
        val current = PreferencesUtil.getKeyboardLayout(requireContext())
        layoutPref?.let { lp ->
            if (!current.isNullOrBlank()) {
                postUI { lp.setSelectedValue(current) }
            }
        }

        val enabled = PreferencesUtil.useExternalKeyboardDevice(requireContext())
        rowPref.setSwitchChecked(enabled)
        actionPref.isEnabled = enabled
        deviceTypePref?.isEnabled = enabled
        layoutPref?.isEnabled = enabled
    }

    override fun onStop() {
        super.onStop()
        try { requireContext().unregisterReceiver(bondReceiver) } catch (_: Exception) {}
        manager.stop()
        BleHub.clearPasswordPrompt()
    }

    private fun refreshStart() {
        if (!manager.isBluetoothReady()) {
            Toast.makeText(requireContext(), R.string.msg_bluetooth_disabled, Toast.LENGTH_SHORT).show()
            return
        }
        manager.devices.observe(viewLifecycleOwner) { list -> populateRow(list) }
        manager.start()
    }

    ////////////////////////////////////////////////////////////////////
    // Populate dropdown entries from manager.devices LiveData.
    ////////////////////////////////////////////////////////////////////
    private fun populateRow(list: List<BtDevice>) {
        val entries = list.map { labelFor(it) }
        val values  = list.map { it.address }
        val saved   = PreferencesUtil.getOutputDeviceId(requireContext())
        rowPref.setData(entries, values, saved)
        updateActionRow(saved)
    }

    private fun labelFor(d: BtDevice): String {
        val name = d.name.ifBlank { getString(R.string.label_unknown_device) }
        val star = if (d.bonded) " -paired" else ""
        return "$name (${d.address})$star"
    }

    private fun updateActionRow(address: String?) {
        val hasSel = !address.isNullOrBlank()
        actionPref.isVisible = hasSel
        if (!hasSel) return

        val bonded = currentIsBonded(address!!)
        actionPref.title = getString(if (bonded) R.string.btn_unpair else R.string.btn_connect)
        actionPref.summary = getString(
            if (bonded) R.string.btn_unpair_summary else R.string.btn_connect_summary
        )
        actionPref.paired = bonded
    }

    private fun currentIsBonded(address: String): Boolean {
        return manager.devices.value.orEmpty()
            .any { it.address == address && it.bonded }
    }

    private fun neededPermissions(): Array<String> {
        return if (android.os.Build.VERSION.SDK_INT >= 31) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_COARSE_LOCATION, Manifest.permission.ACCESS_FINE_LOCATION)
        }
    }

    ////////////////////////////////////////////////////////////////////
    // Keyboard layout options (labels, values)
    ////////////////////////////////////////////////////////////////////
    private fun keyboardLayoutOptions(): Pair<List<String>, List<String>> {
        val pairs = listOf(
            "UK_WINLIN" to "Layout UK Windows/Linux",
            "UK_MAC"    to "Layout UK Mac",
            "IE_WINLIN" to "Layout IE Windows/Linux",
            "IE_MAC"    to "Layout IE Mac",
            "US_WINLIN" to "Layout US Windows/Linux",
            "US_MAC"    to "Layout US Mac",

            "DE_WINLIN" to "Layout DE Windows/Linux",
            "DE_MAC"    to "Layout DE Mac",
            "FR_WINLIN" to "Layout FR Windows/Linux",
            "FR_MAC"    to "Layout FR Mac",
            "ES_WINLIN" to "Layout ES Windows/Linux",
            "ES_MAC"    to "Layout ES Mac",
            "IT_WINLIN" to "Layout IT Windows/Linux",
            "IT_MAC"    to "Layout IT Mac",

            "PT_PT_WINLIN" to "Layout PT-PT Windows/Linux",
            "PT_PT_MAC"    to "Layout PT-PT Mac",
            "PT_BR_WINLIN" to "Layout PT-BR Windows/Linux",
            "PT_BR_MAC"    to "Layout PT-BR Mac",

            "SE_WINLIN" to "Layout SE Windows/Linux",
            "NO_WINLIN" to "Layout NO Windows/Linux",
            "DK_WINLIN" to "Layout DK Windows/Linux",
            "FI_WINLIN" to "Layout FI Windows/Linux",

            "CH_DE_WINLIN" to "Layout CH-DE Windows/Linux",
            "CH_FR_WINLIN" to "Layout CH-FR Windows/Linux",

            "TR_WINLIN" to "Layout TR Windows/Linux",
            "TR_MAC"    to "Layout TR Mac"
        )

        val values  = pairs.map { it.first }
        val entries = pairs.map { it.second }
        return entries to values
    }
}

////////////////////////////////////////////////////////////////////
// ActionButtonStartPreference
////////////////////////////////////////////////////////////////////
class ActionButtonStartPreference @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : Preference(context, attrs) {

    var paired: Boolean = false
        set(value) {
            field = value
            notifyChanged()
        }

    var onButtonClick: (() -> Unit)? = null

    override fun onBindViewHolder(holder: PreferenceViewHolder) {
        super.onBindViewHolder(holder)

        val btn = holder.findViewById(R.id.actionButton) as? MaterialButton
        btn?.apply {
            text = if (paired) context.getString(R.string.btn_unpair)
            else context.getString(R.string.btn_pair)
            setOnClickListener { onButtonClick?.invoke() }
        }
    }
}
