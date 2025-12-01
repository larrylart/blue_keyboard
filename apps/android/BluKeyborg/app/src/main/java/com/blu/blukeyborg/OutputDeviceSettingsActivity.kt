package com.blu.blukeyborg

import android.os.Bundle
import android.widget.ImageButton
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.blu.blukeyborg.R
import com.blu.blukeyborg.OutputDevicePreferenceFragment

class OutputDeviceSettingsActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_output_device_settings)

        // Back arrow
        findViewById<ImageButton>(R.id.btnBack).setOnClickListener {
            onBackPressedDispatcher.onBackPressed()
        }

        // Optional: set title text from resources
        findViewById<TextView>(R.id.settingsTitle).text =
            getString(R.string.title_settings)  // or hardcode "Settings"

        // Load the existing preference fragment into our container
        supportFragmentManager.beginTransaction()
            .replace(R.id.settingsContainer, OutputDevicePreferenceFragment())
            .commit()
    }
}

