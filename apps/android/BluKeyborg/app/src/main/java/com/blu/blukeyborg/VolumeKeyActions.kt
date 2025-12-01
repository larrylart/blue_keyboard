package com.blu.blukeyborg

import android.content.Context
import android.widget.Toast

/**
 * Shared logic to map volume up/down to HID key taps,
 * based on user preferences.
 */
object VolumeKeyActions {

    enum class Action(val prefValue: String) {
        NONE("none"),
        MOVE_RIGHT("move_right"),
        MOVE_LEFT("move_left"),
        MOVE_UP("move_up"),
        MOVE_DOWN("move_down"),
        PAGE_UP("page_up"),
        PAGE_DOWN("page_down"),
        ENTER("enter"),
        PLAY("play"),
        STOP("stop");

        companion object {
            fun fromPref(value: String?): Action =
                values().firstOrNull { it.prefValue == value } ?: NONE
        }
    }

    private fun resolveAction(context: Context, isVolumeUp: Boolean): Action {
        val pref = if (isVolumeUp) {
            PreferencesUtil.getVolumeUpAction(context)
        } else {
            PreferencesUtil.getVolumeDownAction(context)
        }
        return Action.fromPref(pref)
    }

    /**
     * Called from any Activity that wants to handle volume keys.
     * Returns true if we handled it (and consumed the volume event).
     */
    fun handleVolumeKey(context: Context, isVolumeUp: Boolean): Boolean {
        val action = resolveAction(context, isVolumeUp)
        if (action == Action.NONE) return false  // let system handle

        // Map to (mods, usage). Standard HID codes; make sure your dongle
        // understands these values in raw key mode.
        val mods = 0x00
        val usage: Int? = when (action) {
            Action.MOVE_RIGHT -> 0x4F  // Right arrow
            Action.MOVE_LEFT  -> 0x50  // Left arrow
            Action.MOVE_UP    -> 0x52  // Up arrow
            Action.MOVE_DOWN  -> 0x51  // Down arrow
            Action.PAGE_UP    -> 0x4B  // Page Up
            Action.PAGE_DOWN  -> 0x4E  // Page Down
            Action.ENTER      -> 0x28  // Enter

            // TODO: For Play/Stop you probably want consumer-page media keys
            // and a different path in firmware; for now we no-op them or you
            // can plug in your own usage codes if your dongle expects them.
            Action.PLAY       -> null
            Action.STOP       -> null
            Action.NONE       -> null
        }

        if (usage == null) {
            return false
        }

        // At this level we assume fast keys are already enabled.
        BleHub.sendRawKeyTap(mods, usage) { ok, err ->
            if (!ok) {
                Toast.makeText(
                    context,
                    err ?: context.getString(R.string.msg_send_failed),
                    Toast.LENGTH_SHORT
                ).show()
            }
        }

        return true
    }
}
