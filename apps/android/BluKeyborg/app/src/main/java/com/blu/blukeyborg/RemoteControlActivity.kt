package com.blu.blukeyborg

import android.os.Build
import android.os.Bundle
import android.view.Gravity
import android.view.KeyEvent
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import android.widget.ImageButton
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.core.view.ViewCompat
import kotlin.math.max

class RemoteControlActivity : AppCompatActivity() {

    // Local session flag for this activity; we don't share MainActivity's
    // boolean, but BleHub.enableFastKeys is idempotent at the dongle side.
    private var fastKeysEnabled: Boolean = false

    // keep a reference so we can update text dynamically
    private lateinit var bodyText: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

		WindowCompat.setDecorFitsSystemWindows(window, false)
		
        // Transparent full-screen root
        val root = FrameLayout(this).apply {
            setBackgroundColor(0x00000000) // fully transparent
        }

        // Centered "card" UI
        val card = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(48, 48, 48, 48)
            setBackgroundColor(0xFF222222.toInt())
        }

        // --- Top bar inside the card: Title + Close X ---
        val topBar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }

        val titleView = TextView(this).apply {
            text = "BluKeyborg Remote"
            textSize = 18f
            setTextColor(0xFFFFFFFF.toInt())
        }

        val titleLp = LinearLayout.LayoutParams(
            0,
            LinearLayout.LayoutParams.WRAP_CONTENT,
            1f
        )

        val closeButton = ImageButton(this).apply {
            setImageResource(R.drawable.baseline_close_24_white)
            background = null
            setPadding(8, 8, 8, 8)
            setOnClickListener { finish() }
        }

        topBar.addView(titleView, titleLp)
        topBar.addView(closeButton)

        // --- Body text / instructions ---
        bodyText = TextView(this).apply {
            textSize = 16f
            setTextColor(0xFFFFFFFF.toInt())
            setPadding(0, 32, 0, 0)
        }

        card.addView(topBar)
        card.addView(bodyText)

        // After bodyText is created, set the initial mapping text
        updateBodyText()

        val cardParams = FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT,
            FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.CENTER
        )
        root.addView(card, cardParams)

        setContentView(root)

        // Set status bar icons to be dark (since the bar will be transparent by default)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            isAppearanceLightStatusBars = true
        }

		 // handle top bar issue in immersive view
			ViewCompat.setOnApplyWindowInsetsListener(root) { v, insets ->
				val imeInsets = insets.getInsets(WindowInsetsCompat.Type.ime())
				val systemBarsInsets = insets.getInsets(WindowInsetsCompat.Type.systemBars())

				val bottomPadding = maxOf(systemBarsInsets.bottom, imeInsets.bottom)

				v.setPadding(
					systemBarsInsets.left,
					systemBarsInsets.top,   // keeps content below status bar
					systemBarsInsets.right,
					bottomPadding           // keeps content above nav bar / IME
				)

				insets
			}

		// hide only bottom nav bar
		hideBottomNavBar()

        // Make window fully transparent and non-dimming
        window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND)
        window.setBackgroundDrawableResource(android.R.color.transparent)
        window.setLayout(
            WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.MATCH_PARENT
        )

        // Show over lock screen & turn screen on
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
            setShowWhenLocked(true)
            setTurnScreenOn(true)
        } else {
            @Suppress("DEPRECATION")
            window.addFlags(
                WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED or
                        WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON
            )
        }

        // Keep screen on while in remote mode
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        // Enable immersive full-screen (hide status + nav bars)
        //enableFullscreenImmersive()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
			hideBottomNavBar()
            //enableFullscreenImmersive()
        }
    }

    override fun onResume() {
        super.onResume()
        updateBodyText()
    }

/*
    private fun enableFullscreenImmersive() {
        WindowCompat.setDecorFitsSystemWindows(window, false)

        val controller = WindowInsetsControllerCompat(window, window.decorView)
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE

        controller.hide(
            WindowInsetsCompat.Type.statusBars() or
                    WindowInsetsCompat.Type.navigationBars()
        )
    }
*/

	private fun hideBottomNavBar() {
		val controller = WindowInsetsControllerCompat(window, window.decorView)
		controller.systemBarsBehavior =
			WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE

		// Hide ONLY navigation bar (same as settings activity)
		controller.hide(WindowInsetsCompat.Type.navigationBars())
	}

    private fun updateBodyText() {
        val upLabel = VolumeKeyActions.getActionDisplayLabel(this, isVolumeUp = true)
        val downLabel = VolumeKeyActions.getActionDisplayLabel(this, isVolumeUp = false)

        bodyText.text = "\nVolume Up  → $upLabel\n" +
                        "Volume Down → $downLabel\n\n"
    }

    private fun handleVolumeKeyPress(isVolumeUp: Boolean) {
        if (fastKeysEnabled) {
            VolumeKeyActions.handleVolumeKey(this, isVolumeUp)
            return
        }

        BleHub.enableFastKeys { ok, err ->
            runOnUiThread {
                if (ok) {
                    fastKeysEnabled = true
                    VolumeKeyActions.handleVolumeKey(this, isVolumeUp)
                } else {
                    Toast.makeText(
                        this,
                        err ?: getString(R.string.msg_failed_enable_fast_keys),
                        Toast.LENGTH_SHORT
                    ).show()
                }
            }
        }
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        return when (keyCode) {
            KeyEvent.KEYCODE_VOLUME_UP -> {
                handleVolumeKeyPress(isVolumeUp = true)
                true
            }
            KeyEvent.KEYCODE_VOLUME_DOWN -> {
                handleVolumeKeyPress(isVolumeUp = false)
                true
            }
            else -> super.onKeyDown(keyCode, event)
        }
    }
}
