// SPDX-License-Identifier: GPL-2.0
import QtQuick 2.6
import QtQuick.Layouts 1.2
import QtQuick.Controls 2.2 as Controls
import org.kde.kirigami 2.2 as Kirigami
import org.subsurfacedivelog.mobile 1.0


Kirigami.ScrollablePage {
	id: startpage
	background: Rectangle { color: subsurfaceTheme.backgroundColor }

	function saveCredentials() { cloudCredentials.saveCredentials() }

	ColumnLayout {
		CloudCredentials {
			id: cloudCredentials
			Layout.fillWidth: true
			Layout.margins: Kirigami.Units.gridUnit
			Layout.topMargin: 0
			property int headingLevel: 3
		}
		Controls.Label {
			id: messageArea
			Layout.fillWidth: true
			Layout.margins: Kirigami.Units.gridUnit
			Layout.topMargin: 0
			text: manager.startPageText
			wrapMode: Text.WordWrap
		}
		Controls.Label {
			id: explanationTextBasic
			visible: !showPin
			Layout.fillWidth: true
			Layout.margins: Kirigami.Units.gridUnit
			Layout.topMargin: Kirigami.Units.gridUnit * 3
			text: qsTr("To use Subsurface-mobile with Subsurface cloud storage, please enter your cloud credentials.<br/><br/>" +
				"If this is the first time you use Subsurface cloud storage, enter a valid email (all lower case) " +
				"and a password of your choice (letters and numbers).<br/><br/>" +
				"To use Subsurface-mobile only with local data on this device, select " +
				"the no cloud button above.")
			wrapMode: Text.WordWrap
		}
		Controls.Label {
			id: explanationTextPin
			visible: showPin
			Layout.fillWidth: true
			Layout.margins: Kirigami.Units.gridUnit
			Layout.topMargin: Kirigami.Units.gridUnit * 3
			text: qsTr("Thank you for registering with Subsurface. We sent <b>%1</b>" +
				" a PIN code to complete the registration. " +
				"If you do not receive an email from us within 15 minutes, please check " +
				"the correct spelling of your email address and your spam box first.<br/><br/>" +
				"In case of any problems regarding cloud account setup, please contact us " +
				"at our user forum \(https://subsurface-divelog.org/user-forum/\).<br/><br/>").arg(prefs.cloudUserName)
			wrapMode: Text.WordWrap
		}
		Item { width: Kirigami.Units.gridUnit; height: 3 * Kirigami.Units.gridUnit}
	}
}
