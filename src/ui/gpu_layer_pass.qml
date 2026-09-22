import QtQuick

Item {
    id: root

    property var sourceItem: null
    property var backdropSource: null
    property var maskItem: null
    property int blendMode: 1
    property real layerOpacity: 1.0
    property bool hasMask: false
    property real maskDefault: 1.0
    property real maskDensity: 1.0
    property rect maskRect: Qt.rect(0, 0, 0, 0)
    property bool outputVisible: false

    Item {
        id: transparentItem
        width: root.width
        height: root.height
        visible: true
    }

    ShaderEffectSource {
        id: sourceTexture
        anchors.fill: parent
        visible: false
        sourceItem: root.sourceItem
        live: true
        hideSource: true
        textureMirroring: ShaderEffectSource.NoMirroring
        textureSize: Qt.size(root.width, root.height)
    }

    ShaderEffectSource {
        id: backdropTexture
        anchors.fill: parent
        visible: false
        sourceItem: root.backdropSource ? root.backdropSource : transparentItem
        live: true
        hideSource: true
        textureMirroring: ShaderEffectSource.NoMirroring
        textureSize: Qt.size(root.width, root.height)
    }

    ShaderEffectSource {
        id: maskTextureSource
        anchors.fill: parent
        visible: false
        sourceItem: root.maskItem ? root.maskItem : transparentItem
        live: true
        hideSource: true
        textureMirroring: ShaderEffectSource.NoMirroring
        textureSize: Qt.size(root.width, root.height)
    }

    ShaderEffect {
        id: effect
        anchors.fill: parent
        visible: true
        supportsAtlasTextures: true
        blending: false
        property var source: sourceTexture
        property var backdrop: backdropTexture
        property var maskTexture: maskTextureSource
        property real layerOpacity: root.layerOpacity
        property real blendMode: root.blendMode
        property real hasMask: root.hasMask ? 1.0 : 0.0
        property real maskDefault: root.maskDefault
        property real maskDensity: root.maskDensity
        property rect maskRect: root.maskRect
        vertexShader: "qrc:/patchy/shaders/gpu_layer_pass.vert.qsb"
        fragmentShader: "qrc:/patchy/shaders/gpu_layer_pass.frag.qsb"
    }
}
