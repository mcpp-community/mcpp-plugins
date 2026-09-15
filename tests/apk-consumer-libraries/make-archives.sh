#!/usr/bin/env bash
# make-archives.sh <javac> <jar> <out dir>
#
# Builds the local archives the fixture names: `example.jar`, one class, and
# `example.aar`, an Android library archive with a manifest (a permission of its
# own), a class that reads its resource through R, that resource, native
# libraries for both ABIs and an asset. The AAR's R stub is compiled with its
# class and left out of classes.jar, as a Gradle library build leaves it out:
# the application's link generates the R the class reads.
set -eu
javac="$1"; jar="$2"; out="$3"
work="$out/work"
rm -rf "$work" "$out/example.jar" "$out/example.aar"
mkdir -p "$work/jar-src/org/mcpp/jar" "$work/jar-classes" \
         "$work/aar-src/org/mcpp/aar" "$work/aar-classes" \
         "$work/aar/res/values" "$work/aar/jni/x86_64" "$work/aar/jni/arm64-v8a" "$work/aar/assets"

cat > "$work/jar-src/org/mcpp/jar/JarThing.java" <<'J'
package org.mcpp.jar;
public final class JarThing {
    public static final String NAME = "jar";
    private JarThing() {}
}
J
"$javac" -source 17 -target 17 -d "$work/jar-classes" "$work/jar-src/org/mcpp/jar/JarThing.java"
"$jar" cf "$out/example.jar" -C "$work/jar-classes" .

cat > "$work/aar-src/org/mcpp/aar/R.java" <<'J'
package org.mcpp.aar;
public final class R {
    public static final class string {
        public static int aar_greeting = 0;
    }
}
J
cat > "$work/aar-src/org/mcpp/aar/AarThing.java" <<'J'
package org.mcpp.aar;
public final class AarThing {
    private AarThing() {}
    public static String name() { return "aar:" + R.string.aar_greeting; }
}
J
"$javac" -source 17 -target 17 -d "$work/aar-classes" \
    "$work/aar-src/org/mcpp/aar/R.java" "$work/aar-src/org/mcpp/aar/AarThing.java"
"$jar" cf "$work/aar/classes.jar" -C "$work/aar-classes" org/mcpp/aar/AarThing.class

cat > "$work/aar/AndroidManifest.xml" <<'X'
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="org.mcpp.aar">
    <uses-sdk android:minSdkVersion="19"/>
    <uses-permission android:name="android.permission.WAKE_LOCK"/>
</manifest>
X
cat > "$work/aar/res/values/values.xml" <<'X'
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="aar_greeting">hello from the archive</string>
</resources>
X
printf 'not loaded by this fixture\n' > "$work/aar/jni/x86_64/libaar-native.so"
printf 'not loaded by this fixture\n' > "$work/aar/jni/arm64-v8a/libaar-native.so"
printf 'an asset the archive ships\n' > "$work/aar/assets/aar-asset.txt"
"$jar" cfM "$out/example.aar" -C "$work/aar" .
echo "archives: $out/example.jar $out/example.aar"
