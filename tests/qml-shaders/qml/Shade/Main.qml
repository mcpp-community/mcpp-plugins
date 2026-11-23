import QtQuick

Item {
    width: 64
    height: 64
    // The compiled shader is a resource of this module, beside this file.
    property Item effect: ShaderEffect {
        width: 64
        height: 64
        fragmentShader: "shaders/wave.frag.qsb"
    }
    readonly property url shader: effect.fragmentShader
}
