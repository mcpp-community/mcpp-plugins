// Java beside the Kotlin activity, reaching the application's R and both archives.
package org.mcpp.apklibs;

public final class JavaHelper {
    private JavaHelper() {}

    public static String describe() {
        return "java:" + R.string.shared_name + ":" + org.mcpp.jar.JarThing.NAME + ":" + org.mcpp.aar.AarThing.name();
    }
}
