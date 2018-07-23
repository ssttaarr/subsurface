// SPDX-License-Identifier: GPL-2.0
#ifndef DIVETRIPMODEL_H
#define DIVETRIPMODEL_H

#include "treemodel.h"
#include "core/dive.h"
#include <string>

struct DiveItem : public TreeItem {
	Q_DECLARE_TR_FUNCTIONS(TripItem)
public:
	enum Column {
		NR,
		DATE,
		RATING,
		DEPTH,
		DURATION,
		TEMPERATURE,
		TOTALWEIGHT,
		SUIT,
		CYLINDER,
		GAS,
		SAC,
		OTU,
		MAXCNS,
		TAGS,
		PHOTOS,
		COUNTRY,
		LOCATION,
		COLUMNS
	};

	virtual QVariant data(int column, int role) const;
	int diveId;
	virtual bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole);
	virtual Qt::ItemFlags flags(const QModelIndex &index) const;
	QString displayDate() const;
	QString displayDuration() const;
	QString displayDepth() const;
	QString displayDepthWithUnit() const;
	QString displayTemperature() const;
	QString displayTemperatureWithUnit() const;
	QString displayWeight() const;
	QString displayWeightWithUnit() const;
	QString displaySac() const;
	QString displaySacWithUnit() const;
	QString displayTags() const;
	int countPhotos(dive *dive) const;
	int weight() const;
	QString icon_names[4];
};

struct TripItem : public TreeItem {
	Q_DECLARE_TR_FUNCTIONS(TripItem)
public:
	virtual QVariant data(int column, int role) const;
	dive_trip_t *trip;
};

class DiveTripModel : public TreeModel {
	Q_OBJECT
public:
	enum Column {
		NR,
		DATE,
		RATING,
		DEPTH,
		DURATION,
		TEMPERATURE,
		TOTALWEIGHT,
		SUIT,
		CYLINDER,
		GAS,
		SAC,
		OTU,
		MAXCNS,
		TAGS,
		PHOTOS,
		COUNTRY,
		LOCATION,
		COLUMNS
	};

	enum ExtraRoles {
		STAR_ROLE = Qt::UserRole + 1,
		DIVE_ROLE,
		TRIP_ROLE,
		SORT_ROLE,
		DIVE_IDX
	};
	enum Layout {
		TREE,
		LIST,
		CURRENT
	};

	Qt::ItemFlags flags(const QModelIndex &index) const;
	virtual QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
	virtual bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole);
	DiveTripModel(QObject *parent = 0);
	Layout layout() const;
	void setLayout(Layout layout);
	int columnWidth(int column);
	void setColumnWidth(int column, int width);

private:
	void setupModelData();
	QMap<dive_trip_t *, TripItem *> trips;
	QVector<int> columnWidthMap;
	Layout currentLayout;
};

#endif
