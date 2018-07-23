#ifndef DOWNLOADFROMDCTHREAD_H
#define DOWNLOADFROMDCTHREAD_H

#include <QThread>
#include <QMap>
#include <QHash>
#include <QLoggingCategory>

#include "libdivecomputer.h"
#include "connectionlistmodel.h"
#if BT_SUPPORT
#include "core/btdiscovery.h"
#endif
/* Helper object for access of Device Data in QML */
class DCDeviceData {
public:
	DCDeviceData();
	static DCDeviceData *instance();

	QString vendor() const;
	QString product() const;
	QString devName() const;
	bool bluetoothMode() const;
	bool saveDump() const;
	QString devBluetoothName() const;
	QString descriptor() const;
	bool forceDownload() const;
	bool createNewTrip() const;
	bool saveLog() const;
	int deviceId() const;
	int diveId() const;

	/* this needs to be a pointer to make the C-API happy */
	device_data_t* internalData();

	QStringList getProductListFromVendor(const QString& vendor);
	int getMatchingAddress(const QString &vendor, const QString &product);

	int getDetectedVendorIndex();
	int getDetectedProductIndex(const QString &currentVendorText);

	void setDeviceId(int deviceId);
	void setDiveId(int diveId);
	void setVendor(const QString& vendor);
	void setProduct(const QString& product);
	void setDevName(const QString& devName);
	void setDevBluetoothName(const QString& devBluetoothName);
	void setBluetoothMode(bool mode);
	void setForceDownload(bool force);
	void setCreateNewTrip(bool create);
	void setSaveDump(bool dumpMode);
	void setSaveLog(bool saveLog);
private:
	static DCDeviceData *m_instance;
	device_data_t data;

	// Bluetooth name is managed outside of libdivecomputer
	QString m_devBluetoothName;
};

class DownloadThread : public QThread {
	Q_OBJECT

public:
	DownloadThread();
	void run() override;

	DCDeviceData *data();
	QString error;

private:
	DCDeviceData *m_data;
};

//TODO: C++ify descriptor?
struct mydescriptor {
	const char *vendor;
	const char *product;
	dc_family_t type;
	unsigned int model;
};

/* This fills the vendor list QStringList and related members.
* this code needs to be reworked to be less ugly, but it will
* stay like this for now.
*/
void fill_computer_list();
void show_computer_list();
extern QStringList vendorList;
extern QHash<QString, QStringList> productList;
extern QMap<QString, dc_descriptor_t *> descriptorLookup;
extern ConnectionListModel connectionListModel;
#endif
