import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import Acme.Gadget

Item {
    // Layouts: a plugin Qt opens, and a library only that plugin links.
    property Item row: RowLayout {
        Rectangle { implicitWidth: 30; implicitHeight: 10 }
        Rectangle { implicitWidth: 12; implicitHeight: 10 }
    }
    readonly property int layoutWidth: row.implicitWidth
    // Controls with the Material style, chosen by the environment.
    property Item button: Button { text: "ok"; Material.accent: Material.Red }
    // Created from text: no import statement shows QtQuick.Shapes to the closure.
    property Item shape: Qt.createQmlObject("import QtQuick.Shapes; Shape { }", this, "shape")
    readonly property int gadget: Gadget.answer
}
