package com.blu.blukeyborg

import android.content.Context
import android.util.AttributeSet
import android.widget.TextView
import androidx.preference.Preference
import androidx.preference.PreferenceViewHolder
import com.google.android.material.materialswitch.MaterialSwitch

class SendNewlinePreference @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : Preference(context, attrs) {

    private var switchView: MaterialSwitch? = null

    init {
        layoutResource = R.layout.pref_send_newline_row
    }

    override fun onBindViewHolder(holder: PreferenceViewHolder) {
        super.onBindViewHolder(holder)

        val titleView = holder.findViewById(R.id.switchTitle) as? TextView
        switchView = holder.findViewById(R.id.sendNewlineSwitch) as? MaterialSwitch

        // Use Preference title if set, otherwise fallback to layout text
        if (!title.isNullOrEmpty()) {
            titleView?.text = title
        }

        // Load persisted state
        val checked = getPersistedBoolean(false)
        switchView?.apply {
            isChecked = checked
            setOnCheckedChangeListener { _, isChecked ->
                if (callChangeListener(isChecked)) {
                    persistBoolean(isChecked)
                } else {
                    // Revert if changeListener rejected it
                    this.isChecked = !isChecked
                }
            }
        }
    }
}
