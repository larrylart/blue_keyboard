package com.blu.blukeyborg

import android.app.Application
import com.blu.blukeyborg.BleHub
import androidx.appcompat.app.AppCompatDelegate

class BluKeyborgApp : Application() {
    override fun onCreate() {
        super.onCreate()
		AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_NO)
        BleHub.init(this)
    }
}
