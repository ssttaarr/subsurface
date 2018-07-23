// SPDX-License-Identifier: GPL-2.0
import QtQuick 2.6
import QtQuick.Window 2.2
import QtQuick.Dialogs 1.2
import QtQuick.Layouts 1.2
import QtQuick.Window 2.2
import QtQuick.Controls 2.2
import org.subsurfacedivelog.mobile 1.0
import org.kde.kirigami 2.2 as Kirigami

Kirigami.ScrollablePage {
	id: logWindow
	width: subsurfaceTheme.columnWidth
	objectName: "Log"
	title: qsTr("Application Log")
	background: Rectangle { color: subsurfaceTheme.backgroundColor }

	ListView {
		anchors.fill: parent
		model: logModel
		currentIndex: -1
		boundsBehavior: Flickable.StopAtBounds
		maximumFlickVelocity: parent.height * 5
		cacheBuffer: Math.max(5000, parent.height * 5)
		focus: true
		clip: true
		delegate : Text {
			width: logWindow.width
			wrapMode: Text.WrapAtWordBoundaryOrAnywhere
			color: Kirigami.Theme.textColor
			text : message
			font.pointSize: subsurfaceTheme.smallPointSize
			leftPadding: Kirigami.Units.gridUnit / 2
		}
	}
}
