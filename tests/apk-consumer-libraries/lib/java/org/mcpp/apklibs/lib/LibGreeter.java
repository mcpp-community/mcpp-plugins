// The library's Java: its own R, and a call into the library's Kotlin.
package org.mcpp.apklibs.lib;

import android.content.Context;

public final class LibGreeter {
    private LibGreeter() {}

    public static String greet(Context context) {
        return LibKotlin.INSTANCE.shout(context.getString(R.string.lib_greeting));
    }
}
