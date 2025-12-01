package com.blu.blukeyborg

import android.app.Application
import com.blu.blukeyborg.BleHub

class BluKeyborgApp : Application() {
    override fun onCreate() {
        super.onCreate()
        BleHub.init(this)
    }
}
