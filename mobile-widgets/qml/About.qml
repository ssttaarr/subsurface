// SPDX-License-Identifier: GPL-2.0
import QtQuick 2.6
import QtQuick.Layouts 1.2
import org.kde.kirigami 2.2 as Kirigami
import org.subsurfacedivelog.mobile 1.0

Kirigami.ScrollablePage {
	id: aboutPage
	property int pageWidth: aboutPage.width - aboutPage.leftPadding - aboutPage.rightPadding
	title: qsTr("About Subsurface-mobile")
	background: Rectangle { color: subsurfaceTheme.backgroundColor }

	ColumnLayout {
		spacing: Kirigami.Units.largeSpacing
		width: aboutPage.width
		Layout.margins: Kirigami.Units.gridUnit / 2


		Kirigami.Heading {
			text: qsTr("About Subsurface-mobile")
			Layout.topMargin: Kirigami.Units.gridUnit
			Layout.alignment: Qt.AlignHCenter
			Layout.maximumWidth: pageWidth
			wrapMode: TextEdit.NoWrap
			fontSizeMode: Text.Fit
		}
		Image {
			id: image
			source: "qrc:/qml/subsurface-mobile-icon.png"
			fillMode: Image.PreserveAspectCrop
			Layout.alignment: Qt.AlignHCenter + Qt.AlignVCenter
			Layout.maximumWidth: pageWidth / 2
			Layout.maximumHeight: Layout.maximumWidth
		}

		Kirigami.Heading {
			text: qsTr("A mobile version of the free Subsurface divelog software.\n") +
				qsTr("View your dive logs while on the go.")
			level: 4
			Layout.alignment: Qt.AlignHCenter
			Layout.topMargin: Kirigami.Units.largeSpacing * 3
			Layout.maximumWidth: pageWidth
			wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
			anchors.horizontalCenter: parent.Center
			horizontalAlignment: Text.AlignHCenter
		}

		Kirigami.Heading {
			text: qsTr("Version: %1\n\n© Subsurface developer team\n2011-2018").arg(manager.getVersion())
			level: 5
			font.pointSize: subsurfaceTheme.smallPointSize + 1
			Layout.alignment: Qt.AlignHCenter
			Layout.topMargin: Kirigami.Units.largeSpacing
			Layout.maximumWidth: pageWidth
			wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
			anchors.horizontalCenter: parent.Center
			horizontalAlignment: Text.AlignHCenter
		}
		SsrfButton {
			id: copyAppLogToClipboard
			Layout.alignment: Qt.AlignHCenter
			text: qsTr("Copy logs to clipboard")
			onClicked: {
				manager.copyAppLogToClipboard()
				rootItem.returnTopPage()
				}
		}
	}
}
