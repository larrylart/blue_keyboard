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

        // Navigation / keyboard-like
        MOVE_RIGHT("move_right"),
        MOVE_LEFT("move_left"),
        MOVE_UP("move_up"),
        MOVE_DOWN("move_down"),
        PAGE_UP("page_up"),
        PAGE_DOWN("page_down"),
        ENTER("enter"),

        // Media / consumer-like actions
        PLAY("play"),                 // Play/Pause toggle
        STOP("stop"),
        NEXT_TRACK("next_track"),
        PREV_TRACK("prev_track"),
        FAST_FORWARD("fast_forward"),
        REWIND("rewind"),
        VOL_UP("vol_up"),
        VOL_DOWN("vol_down"),
        MUTE("mute");

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

    fun getActionDisplayLabel(context: Context, isVolumeUp: Boolean): String {
        val action = resolveAction(context, isVolumeUp)
        return when (action) {
            Action.NONE        -> "Not mapped"

            Action.MOVE_RIGHT  -> "Right arrow"
            Action.MOVE_LEFT   -> "Left arrow"
            Action.MOVE_UP     -> "Up arrow"
            Action.MOVE_DOWN   -> "Down arrow"
            Action.PAGE_UP     -> "Page up"
            Action.PAGE_DOWN   -> "Page down"
            Action.ENTER       -> "Enter"

            Action.PLAY        -> "Play / Pause"
            Action.STOP        -> "Stop"
            Action.NEXT_TRACK  -> "Next track"
            Action.PREV_TRACK  -> "Previous track"
            Action.FAST_FORWARD-> "Fast forward"
            Action.REWIND      -> "Rewind"
            Action.VOL_UP      -> "Volume up"
            Action.VOL_DOWN    -> "Volume down"
            Action.MUTE        -> "Mute"
        }
    }

    /**
     * Called from any Activity that wants to handle volume keys.
     * Returns true if we handled it (and consumed the volume event).
     */
    fun handleVolumeKey(context: Context, isVolumeUp: Boolean): Boolean {
        val action = resolveAction(context, isVolumeUp)
        if (action == Action.NONE) return false  // let system handle

        val mods = 0x00

        // Single "usage" value; firmware decides how to interpret it.
        val usage: Int? = when (action) {
            // Keyboard-ish navigation (HID Keyboard page 0x07)
            Action.MOVE_RIGHT -> 0x4F  // Right arrow
            Action.MOVE_LEFT  -> 0x50  // Left arrow
            Action.MOVE_UP    -> 0x52  // Up arrow
            Action.MOVE_DOWN  -> 0x51  // Down arrow
            Action.PAGE_UP    -> 0x4B  // Page Up
            Action.PAGE_DOWN  -> 0x4E  // Page Down
            Action.ENTER      -> 0x28  // Enter

            // Media / consumer – standard HID Consumer usages (page 0x0C)
            // Your dongle firmware should map these to a consumer control report.
            Action.PLAY         -> 0x00CD  // Play/Pause
            Action.STOP         -> 0x00B7  // Stop
            Action.NEXT_TRACK   -> 0x00B5  // Next Track
            Action.PREV_TRACK   -> 0x00B6  // Previous Track
            Action.FAST_FORWARD -> 0x00B3  // Fast Forward
            Action.REWIND       -> 0x00B4  // Rewind
            Action.VOL_UP       -> 0x00E9  // Volume Up
            Action.VOL_DOWN     -> 0x00EA  // Volume Down
            Action.MUTE         -> 0x00E2  // Mute

            Action.NONE -> null
        }

        if (usage == null) {
            // No mapping – let Android handle the volume key normally
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
