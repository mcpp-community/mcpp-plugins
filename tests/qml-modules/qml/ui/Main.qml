import QtQuick
import app.core
import Plain

QtObject {
    property Derived d: Derived {}
    property QtObject c: Card { size: 4 }
    property QtObject chip: Chip {}
    readonly property int viaBase: d.v
    readonly property int sum: d.v + d.w + c.size
    readonly property string label: chip.label
}
