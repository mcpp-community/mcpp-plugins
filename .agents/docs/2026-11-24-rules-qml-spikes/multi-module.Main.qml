import QtQuick
import app.core
QtObject {
    property Derived d: Derived {}
    property QtObject c: Card { size: 4 }
    readonly property int sum: d.v + d.w + c.size
}
