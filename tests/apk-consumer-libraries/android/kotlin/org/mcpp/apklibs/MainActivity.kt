// Level 1 in Kotlin: the activity reads the application's R, calls the
// library's Java, and the Java helper beside it.
package org.mcpp.apklibs

import android.app.Activity
import android.os.Bundle
import org.mcpp.apklibs.lib.LibGreeter

class MainActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        System.loadLibrary("apk-consumer-libraries")
        title = getString(R.string.app_greeting) + " " + LibGreeter.greet(this) + " " + JavaHelper.describe()
    }
}
