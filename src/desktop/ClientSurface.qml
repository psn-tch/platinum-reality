import QtQuick
import QtWayland.Compositor
import Frankenstein 1.0

// The policy and chrome use logical surface units. Qt's default Wayland item
// uses output pixels divided by the outer window DPR for its implicit size.
// Normalize the entire surface subtree so its popups/subsurfaces keep Qt's
// matching coordinate mapping while one surface unit occupies one shell unit.
NativeClientSurfaceItem {
    id: clientSurface
    required property real hostScale
    readonly property real surfaceScale: hostScale / (output ? output.scaleFactor : 1)
    transformOrigin: Item.TopLeft
    scale: surfaceScale
    // Explicit dimensions avoid the integer rounding of Qt's implicit QSize.
    width: surface ? surface.destinationSize.width / surfaceScale : 0
    height: surface ? surface.destinationSize.height / surfaceScale : 0
}
