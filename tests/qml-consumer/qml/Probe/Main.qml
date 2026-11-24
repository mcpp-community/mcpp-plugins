import QtQuick
import QtQuick.Controls
import "Util.js" as U
import Probe

QtObject {
    property Backend b: Backend {}
    property QtObject inner: Inner { }
    // A Qt Quick Controls type: the module `QtQuick.Controls` is Qt's, found at run time.
    property Item controls: Item { Button { text: "ok" } }
    readonly property int viaCpp: b.twice(b.n)
    readonly property string viaSingleton: Style.accent
    readonly property int viaJs: U.add(40, 2)
    readonly property int viaNested: inner.value
    readonly property int viaTheme: Theme.radius
    readonly property string viaTr: qsTr("Hello")
}
