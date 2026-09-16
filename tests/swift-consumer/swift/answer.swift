// Exported to C under a fixed symbol name, and calling back into C through the
// bridging header.
@_cdecl("swift_consumer_answer")
public func swiftConsumerAnswer(_ base: Int32) -> Int32 {
    let fromC = swift_consumer_c_marker()
    // String interpolation and `print` reach the Swift standard library, so a
    // program that prints this line has linked and loaded the Swift runtime.
    print("swift side: base \(base), from C \(fromC)")
    return base + fromC
}
