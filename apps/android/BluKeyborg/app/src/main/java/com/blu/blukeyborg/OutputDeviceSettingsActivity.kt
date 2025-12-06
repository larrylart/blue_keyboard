package com.blu.blukeyborg

import android.os.Bundle
import android.widget.ImageButton
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import kotlin.math.max

// Removed unused imports for clarity
import android.view.View

class OutputDeviceSettingsActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        WindowCompat.setDecorFitsSystemWindows(window, false)

        setContentView(R.layout.activity_output_device_settings)

        // Set status bar icons to be dark (since the bar will be transparent by default)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            isAppearanceLightStatusBars = true
        }

        val rootView = findViewById<View>(R.id.settingsRoot) // Explicitly type the view
        ViewCompat.setOnApplyWindowInsetsListener(rootView) { v, insets ->
            val imeInsets = insets.getInsets(WindowInsetsCompat.Type.ime())
            val systemBarsInsets = insets.getInsets(WindowInsetsCompat.Type.systemBars())

            // Your logic here is correct: Use the larger of the nav bar height or IME height for bottom padding.
            val bottomPadding = max(systemBarsInsets.bottom, imeInsets.bottom)

            // Apply padding to your root view to prevent content from being obscured
            // by the status bar (top) or the IME (bottom).
            v.setPadding(
                systemBarsInsets.left,
                systemBarsInsets.top,   // Pushes header below the status bar
                systemBarsInsets.right,
                bottomPadding           // Pushes content above the IME or hidden nav bar area
            )

            insets
        }

        hideBottomNavBar()

        // Back arrow
        findViewById<ImageButton>(R.id.btnBack).setOnClickListener {
            onBackPressedDispatcher.onBackPressed()
        }

        // Optional: set title text from resources
        findViewById<TextView>(R.id.settingsTitle).text =
            getString(R.string.title_settings)

        // Load the preference fragment
        if (savedInstanceState == null) { // Prevents adding fragment on configuration change
            supportFragmentManager.beginTransaction()
                .replace(R.id.settingsContainer, OutputDevicePreferenceFragment())
                .commit()
        }
    }

    // Calling this in onWindowFocusChanged is a good fallback to re-apply the state.
    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideBottomNavBar()
        }
    }

    private fun hideBottomNavBar() {
        val controller = WindowInsetsControllerCompat(window, window.decorView)
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        // HIDE ONLY THE NAVIGATION BAR
        controller.hide(WindowInsetsCompat.Type.navigationBars())
    }
}
