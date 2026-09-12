// Fixture: the project-local Java root of `tests/apk-consumer`, used when
// `APK_CONSUMER_LEVEL1` selects level 1 (see `build.mcpp`). One class,
// naming the activity `options::manifest_template` renders through
// `{{activity}}` and `assets/mcpp-run.json` also carries.
package org.mcpp.apkconsumer;

import android.app.Activity;

public class MainActivity extends Activity {
}
