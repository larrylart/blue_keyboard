package com.blu.blukeyborg

import android.content.Context
import android.util.AttributeSet
import android.widget.ArrayAdapter
import androidx.preference.Preference
import androidx.preference.PreferenceViewHolder
import com.google.android.material.textfield.MaterialAutoCompleteTextView
import com.google.android.material.textfield.TextInputLayout
import com.blu.blukeyborg.R

class SimpleDropdownPreference @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : Preference(context, attrs) {

	init {
		if (layoutResource == 0) {
			layoutResource = R.layout.pref_row_dropdown
		}
	}

    private var dropdown: MaterialAutoCompleteTextView? = null
    private var til: TextInputLayout? = null

    private var entries: List<String> = emptyList()
    private var values: List<String> = emptyList()
    private var selectedValue: String? = null

    var onSelected: ((value: String, label: String) -> Unit)? = null

    override fun onBindViewHolder(holder: PreferenceViewHolder) {
        super.onBindViewHolder(holder)
        til = holder.findViewById(R.id.dropdownTil) as? TextInputLayout
        dropdown = holder.findViewById(R.id.dropdownView) as? MaterialAutoCompleteTextView

        // used to put the preference title into the TextInputLayout hint (top-border label)
		til?.isHintEnabled = !title.isNullOrEmpty()
		til?.hint = if (title.isNullOrEmpty()) null else title

        dropdown?.apply {
            setAdapter(
                ArrayAdapter(
                    context,
                    com.google.android.material.R.layout.mtrl_auto_complete_simple_item,
                    entries
                )
            )
            isEnabled = isEnabled

            // Preselect by value if provided
            selectedValue?.let { sel ->
                val idx = values.indexOf(sel)
                if (idx >= 0) setText(entries[idx], /* filter= */ false)
            }

            setOnItemClickListener { _, _, position, _ ->
                val v = values.getOrNull(position).orEmpty()
                val l = entries.getOrNull(position).orEmpty()
                selectedValue = v
                onSelected?.invoke(v, l)
            }
        }
    }

    fun setData(entries: List<String>, values: List<String>, preselectValue: String?) {
        this.entries = entries
        this.values = values
        this.selectedValue = preselectValue
        notifyChanged()
    }

    fun setSelectedValue(value: String?) {
        selectedValue = value
        notifyChanged()
    }
}
