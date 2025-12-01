package com.blu.blukeyborg

import android.content.Context
import android.content.SharedPreferences
import androidx.preference.PreferenceManager
import com.blu.blukeyborg.R

object PreferencesUtil {

    private fun prefs(context: Context): SharedPreferences =
        PreferenceManager.getDefaultSharedPreferences(context)

    // Output device: ID + name

    fun getOutputDeviceId(context: Context): String? =
        prefs(context).getString(
            context.getString(R.string.output_device_id_key),
            null
        )

    fun setOutputDeviceId(context: Context, id: String?) {
        prefs(context).edit()
            .putString(context.getString(R.string.output_device_id_key), id)
            .apply()
    }

    fun getOutputDeviceName(context: Context): String? =
        prefs(context).getString(
            context.getString(R.string.output_device_name_key),
            null
        )

    fun setOutputDeviceName(context: Context, name: String?) {
        prefs(context).edit()
            .putString(context.getString(R.string.output_device_name_key), name)
            .apply()
    }

    // Master toggle: use external keyboard device

    fun useExternalKeyboardDevice(context: Context): Boolean =
        prefs(context).getBoolean(
            context.getString(R.string.settings_output_device_key),
            false
        )

    fun setUseExternalKeyboardDevice(context: Context, enabled: Boolean) {
        prefs(context).edit()
            .putBoolean(context.getString(R.string.settings_output_device_key), enabled)
            .apply()
    }

    // Flag to disable output device when an error occurs
    private const val KEY_DISABLED_BY_ERROR = "output_device_disabled_by_error"

    fun wasOutputDeviceDisabledByError(context: Context): Boolean =
        prefs(context).getBoolean(KEY_DISABLED_BY_ERROR, false)

    fun setOutputDeviceDisabledByError(context: Context, disabled: Boolean) {
        prefs(context).edit()
            .putBoolean(KEY_DISABLED_BY_ERROR, disabled)
            .apply()
    }

    // "Send new line" preference

    fun sendNewLineAfterPassword(context: Context): Boolean =
        prefs(context).getBoolean(
            context.getString(R.string.output_device_send_new_line_key),
            false
        )

    fun setSendNewLineAfterPassword(context: Context, enabled: Boolean) {
        prefs(context).edit()
            .putBoolean(context.getString(R.string.output_device_send_new_line_key), enabled)
            .apply()
    }

    // Device type + keyboard layout

    fun getDeviceType(context: Context): String? =
        prefs(context).getString(
            context.getString(R.string.pref_device_type_key),
            null
        )

    fun setDeviceType(context: Context, type: String?) {
        prefs(context).edit()
            .putString(context.getString(R.string.pref_device_type_key), type)
            .apply()
    }

    fun getKeyboardLayout(context: Context): String? =
        prefs(context).getString(
            context.getString(R.string.pref_keyboard_layout_key),
            null
        )

    fun setKeyboardLayout(context: Context, layout: String?) {
        prefs(context).edit()
            .putString(context.getString(R.string.pref_keyboard_layout_key), layout)
            .apply()
    }
	
    // Volume key actions //////////////////////////////////////////////////////

    fun getVolumeUpAction(context: Context): String {
        return prefs(context).getString(
            context.getString(R.string.pref_volume_up_action_key),
            "none"   // default = None
        ) ?: "none"
    }

    fun setVolumeUpAction(context: Context, value: String) {
        prefs(context).edit()
            .putString(context.getString(R.string.pref_volume_up_action_key), value)
            .apply()
    }

    fun getVolumeDownAction(context: Context): String {
        return prefs(context).getString(
            context.getString(R.string.pref_volume_down_action_key),
            "none"
        ) ?: "none"
    }

    fun setVolumeDownAction(context: Context, value: String) {
        prefs(context).edit()
            .putString(context.getString(R.string.pref_volume_down_action_key), value)
            .apply()
    }
	
	
}
