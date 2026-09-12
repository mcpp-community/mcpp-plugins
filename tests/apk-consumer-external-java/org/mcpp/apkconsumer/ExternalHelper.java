// Fixture: a Java root OUTSIDE `tests/apk-consumer`, a sibling directory
// reached by an absolute path `build.mcpp` computes from
// `mcpp::manifest_dir()` -- standing in for a path dependency's own Java
// tree the way design record §3.3 describes. `options::java_sources`
// compiles this root alongside the project's own, one `javac`, one `d8`;
// `root_in_project` (`dist/apk.cppm`) reads it as OUTSIDE the package root,
// so this class's file is not declared with `rerun_if_changed_glob` -- only
// as an input of the `javac` action.
package org.mcpp.apkconsumer;

public class ExternalHelper {
    public static String marker() { return "external"; }
}
