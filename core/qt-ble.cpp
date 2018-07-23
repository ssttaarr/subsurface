// SPDX-License-Identifier: GPL-2.0
#include <errno.h>

#include <QtBluetooth/QBluetoothAddress>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <QLoggingCategory>

#include <libdivecomputer/version.h>

#include "libdivecomputer.h"
#include "core/qt-ble.h"
#include "core/btdiscovery.h"
#include "core/subsurface-string.h"

#define BLE_TIMEOUT 12000 // 12 seconds seems like a very long time to wait
#define DEBUG_THRESHOLD 50
static int debugCounter;

#define IS_HW(_d) same_string((_d)->vendor, "Heinrichs Weikamp")
#define IS_SHEARWATER(_d) same_string((_d)->vendor, "Shearwater")

#define MAXIMAL_HW_CREDIT	255
#define MINIMAL_HW_CREDIT	32

#define WAITFOR(expression, ms) do {					\
	Q_ASSERT(QCoreApplication::instance());				\
	Q_ASSERT(QThread::currentThread());				\
									\
	if (expression)							\
		break;							\
	QElapsedTimer timer;						\
	timer.start();							\
									\
	do {								\
		QCoreApplication::processEvents(QEventLoop::AllEvents, ms); \
		if (expression)						\
			break;						\
		QThread::msleep(10);					\
	} while (timer.elapsed() < (ms));				\
} while (0)

static void waitFor(int ms)
{
	WAITFOR(false, ms);
}

extern "C" {

void BLEObject::serviceStateChanged(QLowEnergyService::ServiceState)
{
	QList<QLowEnergyCharacteristic> list;

	auto service = qobject_cast<QLowEnergyService*>(sender());
	if (service)
		list = service->characteristics();

	Q_FOREACH(QLowEnergyCharacteristic c, list) {
		qDebug() << "   " << c.uuid().toString();
	}
}

void BLEObject::characteristcStateChanged(const QLowEnergyCharacteristic &c, const QByteArray &value)
{
	if (IS_HW(device)) {
		if (c.uuid() == hwAllCharacteristics[HW_OSTC_BLE_DATA_TX]) {
			hw_credit--;
			receivedPackets.append(value);
			if (hw_credit == MINIMAL_HW_CREDIT)
				setHwCredit(MAXIMAL_HW_CREDIT - MINIMAL_HW_CREDIT);
		} else {
			qDebug() << "ignore packet from" << c.uuid() << value.toHex();
		}
	} else {
		receivedPackets.append(value);
	}
}

void BLEObject::characteristicWritten(const QLowEnergyCharacteristic &c, const QByteArray &value)
{
	if (IS_HW(device)) {
		if (c.uuid() == hwAllCharacteristics[HW_OSTC_BLE_CREDITS_RX]) {
			bool ok;
			hw_credit += value.toHex().toInt(&ok, 16);
			isCharacteristicWritten = true;
		}
	} else {
		if (debugCounter < DEBUG_THRESHOLD)
			qDebug() << "BLEObject::characteristicWritten";
	}
}

void BLEObject::writeCompleted(const QLowEnergyDescriptor&, const QByteArray&)
{
	qDebug() << "BLE write completed";
}

void BLEObject::addService(const QBluetoothUuid &newService)
{
	qDebug() << "Found service" << newService;
	bool isStandardUuid = false;
	newService.toUInt16(&isStandardUuid);
	if (IS_HW(device)) {
		/* The HW BT/BLE piece or hardware uses, what we
		 * call here, "a Standard UUID. It is standard because the Telit/Stollmann
		 * manufacturer applied for an own UUID for its product, and this was granted
		 * by the Bluetooth SIG.
		 */
		if (newService != QUuid("{0000fefb-0000-1000-8000-00805f9b34fb}"))
			return; // skip all services except the right one
	} else if (isStandardUuid) {
		qDebug () << " .. ignoring standard service";
		return;
	}

	auto service = controller->createServiceObject(newService, this);
	qDebug() << " .. created service object" << service;
	if (service) {
		services.append(service);
		connect(service, &QLowEnergyService::stateChanged, this, &BLEObject::serviceStateChanged);
		connect(service, &QLowEnergyService::characteristicChanged, this, &BLEObject::characteristcStateChanged);
		connect(service, &QLowEnergyService::characteristicWritten, this, &BLEObject::characteristicWritten);
		connect(service, &QLowEnergyService::descriptorWritten, this, &BLEObject::writeCompleted);
		service->discoverDetails();
	}
}

BLEObject::BLEObject(QLowEnergyController *c, dc_user_device_t *d)
{
	controller = c;
	device = d;
	debugCounter = 0;
	isCharacteristicWritten = false;
}

BLEObject::~BLEObject()
{
	qDebug() << "Deleting BLE object";

	foreach (QLowEnergyService *service, services)
		delete service;

	delete controller;
}

dc_status_t BLEObject::write(const void *data, size_t size, size_t *actual)
{
	if (actual) *actual = 0;

	if (!receivedPackets.isEmpty()) {
		qDebug() << ".. write HIT with still incoming packets in queue";
		do {
			receivedPackets.takeFirst();
		} while (!receivedPackets.isEmpty());
	}

	QList<QLowEnergyCharacteristic> list = preferredService()->characteristics();

	if (list.isEmpty())
		return DC_STATUS_IO;

	QByteArray bytes((const char *)data, (int) size);

	const QLowEnergyCharacteristic &c = list.constFirst();
	QLowEnergyService::WriteMode mode;

	mode = (c.properties() & QLowEnergyCharacteristic::WriteNoResponse) ?
			QLowEnergyService::WriteWithoutResponse :
			QLowEnergyService::WriteWithResponse;

	preferredService()->writeCharacteristic(c, bytes, mode);
	if (actual) *actual = size;
	return DC_STATUS_SUCCESS;
}

dc_status_t BLEObject::read(void *data, size_t size, size_t *actual)
{
	if (actual)
		*actual = 0;
	if (receivedPackets.isEmpty()) {
		QList<QLowEnergyCharacteristic> list = preferredService()->characteristics();
		if (list.isEmpty())
			return DC_STATUS_IO;

		WAITFOR(!receivedPackets.isEmpty(), BLE_TIMEOUT);
		if (receivedPackets.isEmpty())
			return DC_STATUS_IO;
	}

	QByteArray packet = receivedPackets.takeFirst();

	if ((size_t)packet.size() > size)
		return DC_STATUS_NOMEMORY;

	memcpy((char *)data, packet.data(), packet.size());
	if (actual)
		*actual += packet.size();

	return DC_STATUS_SUCCESS;
}

dc_status_t BLEObject::setHwCredit(unsigned int c)
{
	/* The Terminal I/O client transmits initial UART credits to the server (see 6.5).
	 *
	 * Notice that we have to write to the characteristic here, and not to its
	 * descriptor as for the enabeling of notifications or indications.
	 *
	 * Futher notice that this function has the implicit effect of processing the
	 * event loop (due to waiting for the confirmation of the credit request).
	 * So, as characteristcStateChanged will be triggered, while receiving
	 * data from the OSTC, these are processed too.
	 */

	QList<QLowEnergyCharacteristic> list = preferredService()->characteristics();
	isCharacteristicWritten = false;
	preferredService()->writeCharacteristic(list[HW_OSTC_BLE_CREDITS_RX],
						QByteArray(1, c),
						QLowEnergyService::WriteWithResponse);

	/* And wait for the answer*/
	int msec = BLE_TIMEOUT;
	while (msec > 0 && !isCharacteristicWritten) {
		waitFor(100);
		msec -= 100;
	}
	if (!isCharacteristicWritten)
		return DC_STATUS_TIMEOUT;
	return DC_STATUS_SUCCESS;
}

dc_status_t BLEObject::setupHwTerminalIo(QList<QLowEnergyCharacteristic> allC)
{	/* This initalizes the Terminal I/O client as described in
	 * http://www.telit.com/fileadmin/user_upload/products/Downloads/sr-rf/BlueMod/TIO_Implementation_Guide_r04.pdf
	 * Referenced section numbers below are from that document.
	 *
	 * This is for all HW computers, that use referenced BT/BLE hardware module from Telit
	 * (formerly Stollmann). The 16 bit UUID 0xFEFB (or a derived 128 bit UUID starting with
	 * 0x0000FEFB is a clear indication that the OSTC is equipped with this BT/BLE hardware.
	 */

	if (allC.length() != 4) {
		qDebug() << "This should not happen. HW/OSTC BT/BLE device without 4 Characteristics";
		return DC_STATUS_IO;
	}

	/* The Terminal I/O client subscribes to indications of the UART credits TX
	 * characteristic (see 6.4).
	 *
	 * Notice that indications are subscribed to by writing 0x0200 to its descriptor. This
	 * can be understood by looking for Client Characteristic Configuration, Assigned
	 * Number: 0x2902. Enabling/Disabeling is setting the proper bit, and they
	 * differ for indications and notifications.
	 */
	QLowEnergyDescriptor d = allC[HW_OSTC_BLE_CREDITS_TX].descriptors().first();
	preferredService()->writeDescriptor(d, QByteArray::fromHex("0200"));

	/* The Terminal I/O client subscribes to notifications of the UART data TX
	 * characteristic (see 6.2).
	 */
	d = allC[HW_OSTC_BLE_DATA_TX].descriptors().first();
	preferredService()->writeDescriptor(d, QByteArray::fromHex("0100"));

	/* The Terminal I/O client transmits initial UART credits to the server (see 6.5). */
	return setHwCredit(MAXIMAL_HW_CREDIT);
}

dc_status_t qt_ble_open(void **io, dc_context_t *, const char *devaddr, dc_user_device_t *user_device)
{
	debugCounter = 0;
	QLoggingCategory::setFilterRules(QStringLiteral("qt.bluetooth* = true"));

	/*
	 * LE-only devices get the "LE:" prepended by the scanning
	 * code, so that the rfcomm code can see they only do LE.
	 *
	 * We just skip that prefix (and it doesn't always exist,
	 * since the device may support both legacy BT and LE).
	 */
	if (!strncmp(devaddr, "LE:", 3))
		devaddr += 3;

	// HACK ALERT! Qt 5.9 needs this for proper Bluez operation
	qputenv("QT_DEFAULT_CENTRAL_SERVICES", "1");

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
	QBluetoothDeviceInfo remoteDevice = getBtDeviceInfo(QString(devaddr));
	QLowEnergyController *controller = QLowEnergyController::createCentral(remoteDevice);
#else
	// this is deprecated but given that we don't use Qt to scan for
	// devices on Android, we don't have QBluetoothDeviceInfo for the
	// paired devices and therefore cannot use the newer interfaces
	// that are preferred starting with Qt 5.7
	QBluetoothAddress remoteDeviceAddress(devaddr);
	QLowEnergyController *controller = new QLowEnergyController(remoteDeviceAddress);
#endif
	qDebug() << "qt_ble_open(" << devaddr << ")";

	if (IS_SHEARWATER(user_device))
		controller->setRemoteAddressType(QLowEnergyController::RandomAddress);

	// Try to connect to the device
	controller->connectToDevice();

	// Create a timer. If the connection doesn't succeed after five seconds or no error occurs then stop the opening step
	int msec = BLE_TIMEOUT;
	while (msec > 0 && controller->state() == QLowEnergyController::ConnectingState) {
		waitFor(100);
		msec -= 100;
	}

	switch (controller->state()) {
	case QLowEnergyController::ConnectedState:
		qDebug() << "connected to the controller for device" << devaddr;
		break;
	case QLowEnergyController::ConnectingState:
		qDebug() << "timeout while trying to connect to the controller " << devaddr;
		report_error("Timeout while trying to connect to %s", devaddr);
		delete controller;
		return DC_STATUS_IO;
	default:
		qDebug() << "failed to connect to the controller " << devaddr << "with error" << controller->errorString();
		report_error("Failed to connect to %s: '%s'", devaddr, qPrintable(controller->errorString()));
		delete controller;
		return DC_STATUS_IO;
	}

	// We need to discover services etc here!
	// Note that ble takes ownership of controller and henceforth deleting ble will
	// take care of deleting controller.
	BLEObject *ble = new BLEObject(controller, user_device);
	ble->connect(controller, SIGNAL(serviceDiscovered(QBluetoothUuid)), SLOT(addService(QBluetoothUuid)));

	qDebug() << "  .. discovering services";

	controller->discoverServices();

	msec = BLE_TIMEOUT;
	while (msec > 0 && controller->state() == QLowEnergyController::DiscoveringState) {
		waitFor(100);
		msec -= 100;
	}

	qDebug() << " .. done discovering services";
	if (ble->preferredService() == nullptr) {
		qDebug() << "failed to find suitable service on" << devaddr;
		report_error("Failed to find suitable service on '%s'", devaddr);
		delete ble;
		return DC_STATUS_IO;
	}

	qDebug() << " .. discovering details";
	msec = BLE_TIMEOUT;
	while (msec > 0 && ble->preferredService()->state() == QLowEnergyService::DiscoveringServices) {
		waitFor(100);
		msec -= 100;
	}

	if (ble->preferredService()->state() != QLowEnergyService::ServiceDiscovered) {
		qDebug() << "failed to find suitable service on" << devaddr;
		report_error("Failed to find suitable service on '%s'", devaddr);
		delete ble;
		return DC_STATUS_IO;
	}


	qDebug() << " .. enabling notifications";

	/* Enable notifications */
	QList<QLowEnergyCharacteristic> list = ble->preferredService()->characteristics();

	if (!list.isEmpty()) {
		const QLowEnergyCharacteristic &c = list.constLast();

		if (IS_HW(user_device)) {
			dc_status_t r = ble->setupHwTerminalIo(list);
			if (r != DC_STATUS_SUCCESS) {
				delete ble;
				return r;
			}
		} else {
			QList<QLowEnergyDescriptor> l = c.descriptors();

			qDebug() << "Descriptor list with" << l.length() << "elements";

			QLowEnergyDescriptor d;
			foreach(d, l)
				qDebug() << "Descriptor:" << d.name() << "uuid:" << d.uuid().toString();

			if (!l.isEmpty()) {
				bool foundCCC = false;
				foreach (d, l) {
					if (d.type() == QBluetoothUuid::ClientCharacteristicConfiguration) {
						// pick the correct characteristic
						foundCCC = true;
						break;
					}
				}
				if (!foundCCC)
					// if we didn't find a ClientCharacteristicConfiguration, try the first one
					d = l.first();

				qDebug() << "now writing \"0x0100\" to the descriptor" << d.uuid().toString();

				ble->preferredService()->writeDescriptor(d, QByteArray::fromHex("0100"));
			}
		}
	}

	// Fill in info
	*io = (void *)ble;
	return DC_STATUS_SUCCESS;
}

dc_status_t qt_ble_close(void *io)
{
	BLEObject *ble = (BLEObject *) io;

	delete ble;

	return DC_STATUS_SUCCESS;
}
static void checkThreshold()
{
	if (++debugCounter == DEBUG_THRESHOLD) {
		QLoggingCategory::setFilterRules(QStringLiteral("qt.bluetooth* = false"));
		qDebug() << "turning off further BT debug output";
	}
}

dc_status_t qt_ble_read(void *io, void* data, size_t size, size_t *actual)
{
	checkThreshold();
	BLEObject *ble = (BLEObject *) io;
	return ble->read(data, size, actual);
}

dc_status_t qt_ble_write(void *io, const void* data, size_t size, size_t *actual)
{
	checkThreshold();
	BLEObject *ble = (BLEObject *) io;
	return ble->write(data, size, actual);
}

} /* extern "C" */
