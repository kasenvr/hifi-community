//
//  CheckBox.qml
//
//  Created by David Rowe on 26 Feb 2016
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import QtQuick 2.2
import QtQuick.Controls 2.2 as Original
import QtGraphicalEffects 1.0

import "../stylesUit"

import TabletScriptingInterface 1.0

Original.CheckBox {
    id: checkBox

    property int colorScheme: hifi.colorSchemes.light
    property string color: hifi.colors.lightGrayText
    readonly property bool isLightColorScheme: colorScheme == hifi.colorSchemes.light
    property bool isRedCheck: false
    property int boxSize: 14
    property int boxRadius: 3
    property bool wrap: true;
    readonly property int checkSize: Math.max(boxSize - 8, 10)
    readonly property int checkRadius: 2
    property string labelFontFamily: "Raleway"
    property int labelFontSize: 14;
    property int labelFontWeight: Font.DemiBold;
    property bool isFeatureDisabled: false
    property string featureDisabledToolTip;
    focusPolicy: Qt.ClickFocus
    hoverEnabled: true

    onClicked: {
        Tablet.playSound(TabletEnums.ButtonClick);
    }

    onHoveredChanged: {
        if (hovered) {
            Tablet.playSound(TabletEnums.ButtonHover);
        }
    }

    indicator: Item {
        width: isFeatureDisabled ? featureDisabledWarning.width : box.width
        anchors.left: parent.left

        Image {
            id: featureDisabledWarning
            visible: isFeatureDisabled
            source: "/icons/warning-svgrepo-com.svg"

            anchors.bottom: box.top
            anchors.left: parent.left
            anchors.bottomMargin: 3
            height: isFeatureDisabled ? checkBox.height / 2.5 : 0
            width: height

            ToolTip {
                id: toolTip
                toolTip: checkBox.featureDisabledToolTip
                clip: false	// Doesn't seem useful.
            }

            ColorOverlay {
                anchors.fill: parent
                source: parent
                color: "red"
            }
        }

        Rectangle {
            id: box
                implicitWidth: boxSize
                implicitHeight: boxSize
            anchors.leftMargin: featureDisabledWarning.width / 2
            radius: boxRadius
            y: checkBox.height / 2 - height / 2
            border.width: 1
            border.color: pressed || hovered
                          ? hifi.colors.checkboxCheckedBorder
                          : (checkBox.isLightColorScheme ? hifi.colors.checkboxLightFinish : hifi.colors.checkboxDarkFinish)

            gradient: Gradient {
                GradientStop {
                    position: 0.2
                    color: pressed || hovered
                           ? (checkBox.isLightColorScheme ? hifi.colors.checkboxChecked : hifi.colors.checkboxLightStart)
                           : (checkBox.isLightColorScheme ? hifi.colors.checkboxLightStart : hifi.colors.checkboxDarkStart)
                }
                GradientStop {
                    position: 1.0
                    color: pressed || hovered
                           ? (checkBox.isLightColorScheme ? hifi.colors.checkboxChecked : hifi.colors.checkboxLightFinish)
                           : (checkBox.isLightColorScheme ? hifi.colors.checkboxLightFinish : hifi.colors.checkboxDarkFinish)
                }
            }

            Rectangle {
                id: innerBox
                width: checkSize - 4
                height: width
                radius: checkRadius
                anchors.centerIn: parent
                color: hifi.colors.checkboxCheckedBorder
                visible: pressed || hovered
            }

            Rectangle {
                id: check
                width: checkSize
                height: checkSize
                radius: checkRadius
                anchors.centerIn: parent
                color: isRedCheck ? hifi.colors.checkboxCheckedRed : hifi.colors.checkboxChecked
                border.width: 2
                border.color: isRedCheck? hifi.colors.checkboxCheckedBorderRed : hifi.colors.checkboxCheckedBorder
                visible: checked && !pressed || !checked && pressed
            }

            Rectangle {
                id: disabledOverlay
                visible: !enabled
                width: boxSize
                height: boxSize
                radius: boxRadius
                border.width: 1
                border.color: hifi.colors.baseGrayHighlight
                color: hifi.colors.baseGrayHighlight
                opacity: 0.5
            }
        }
    }

    contentItem: Label {
        text: checkBox.text
        color: checkBox.color
        font.family: checkBox.labelFontFamily;
        font.pixelSize: checkBox.labelFontSize;
        font.weight: checkBox.labelFontWeight;
        x: 2
        verticalAlignment: Text.AlignVCenter
        wrapMode: checkBox.wrap ? Text.Wrap : Text.NoWrap
        elide: checkBox.wrap ? Text.ElideNone : Text.ElideRight
        enabled: checkBox.enabled
        leftPadding: checkBox.indicator.width + checkBox.spacing
    }
}
