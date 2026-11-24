import QtQuick
import "Util.js" as U
import com.acme.ui
QtObject {
    property Backend b: Backend {}
    property QtObject inner: Inner { }
    readonly property int viaCpp: b.twice(b.n)
    readonly property string viaSingleton: Style.accent
    readonly property int viaJs: U.add(40, 2)
    readonly property int viaNested: inner.value
}
