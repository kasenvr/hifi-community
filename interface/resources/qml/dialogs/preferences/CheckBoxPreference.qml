//
//  CheckBoxPreference.qml
//
//  Created by Bradley Austin Davis on 18 Jan 2016
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import QtQuick 2.5
import TabletScriptingInterface 1.0

import controlsUit 1.0

Preference {
    id: root
    height: spacer.height + Math.max(hifi.dimensions.controlLineHeight, checkBox.implicitHeight)
    property bool value: false
    Component.onCompleted: {
        //checkBox.enabled = preference.enabled;
        checkBox.checked = preference.value;
        checkBox.isFeatureDisabled = preference.functionalityDisabledTooltip;
        checkBox.featureDisabledToolTip = preference.tooltip;
        value = checkBox.checked;
        preference.value = Qt.binding(function(){ return checkBox.checked; });
        value = checkBox.checked;	// Why is this here twice?
    }

    function save() {
        preference.value = checkBox.checked;
        preference.save();
    }

    Item {
        id: spacer
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: isFirstCheckBox && !preference.indented ? 16 : 2
    }

    CheckBox {
        id: checkBox
        onHoveredChanged: {
            if (hovered) {
                Tablet.playSound(TabletEnums.ButtonHover);
            }
        }

        onClicked: {
            Tablet.playSound(TabletEnums.ButtonClick);
            value = checked;
        }

        anchors {
            top: spacer.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            leftMargin: preference.indented ? 20 : 0
        }
        text: root.label
        colorScheme: hifi.colorSchemes.dark
    }
}
