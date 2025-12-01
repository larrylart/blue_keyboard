package com.blu.blukeyborg

import android.content.Context
import android.util.AttributeSet
import android.view.View
import android.widget.ArrayAdapter
import androidx.preference.Preference
import androidx.preference.PreferenceViewHolder
import com.google.android.material.materialswitch.MaterialSwitch
import com.google.android.material.textfield.MaterialAutoCompleteTextView
import com.blu.blukeyborg.R

class OutputDeviceRowPreference @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : Preference(context, attrs) {

    init { layoutResource = R.layout.pref_output_device_row }

    private var dropdown: MaterialAutoCompleteTextView? = null
    private var enable: MaterialSwitch? = null

    // Data model for the dropdown
    private var entries: List<String> = emptyList()
    private var values: List<String> = emptyList()
    private var selectedAddress: String? = null

    // Callbacks to be set by the Fragment
    var onToggleChanged: ((Boolean) -> Unit)? = null
    var onDeviceSelected: ((address: String, label: String) -> Unit)? = null

	override fun onBindViewHolder(holder: PreferenceViewHolder) {
		super.onBindViewHolder(holder)
		dropdown = holder.findViewById(R.id.outputDeviceDropdown) as MaterialAutoCompleteTextView
		enable = holder.findViewById(R.id.switchEnable) as MaterialSwitch

		// Defensive: avoid SwitchCompat NPE when textOn/textOff are null
		enable?.apply {
			if (textOn == null)  textOn  = ""
			if (textOff == null) textOff = ""
			// optional: no labels at all
			showText = false
		}

		// Bind current enable state
		enable?.setOnCheckedChangeListener(null)
		enable?.isChecked = isDropdownEnabled
		enable?.setOnCheckedChangeListener { _, isChecked ->
			setDropdownEnabled(isChecked)
			onToggleChanged?.invoke(isChecked)
		}

		// Bind dropdown
		dropdown?.apply {
			setAdapter(
				ArrayAdapter(
					context,
					com.google.android.material.R.layout.mtrl_auto_complete_simple_item,
					entries
				)
			)

			isEnabled = isDropdownEnabled

			// Preselect by address if we have one
			if (!selectedAddress.isNullOrBlank()) {
				val idx = values.indexOf(selectedAddress)
				if (idx >= 0) setText(entries[idx], /*filter=*/ false)
			}

			setOnItemClickListener { _, _, position, _ ->
				val addr  = values.getOrNull(position).orEmpty()
				val label = entries.getOrNull(position).orEmpty()
				selectedAddress = addr
				onDeviceSelected?.invoke(addr, label)
			}
		}
	}


    // Public API for the fragment
    private var isDropdownEnabled: Boolean = true

    fun setDropdownEnabled(enabled: Boolean) {
        isDropdownEnabled = enabled
        dropdown?.isEnabled = enabled
    }

    fun setSwitchChecked(checked: Boolean) {
        enable?.isChecked = checked
        setDropdownEnabled(checked)
    }

    fun setData(entries: List<String>, values: List<String>, preselectAddress: String?) {
        this.entries = entries
        this.values = values
        this.selectedAddress = preselectAddress
        notifyChanged()
    }
}
