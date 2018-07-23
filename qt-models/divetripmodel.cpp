// SPDX-License-Identifier: GPL-2.0
#include "qt-models/divetripmodel.h"
#include "core/gettextfromc.h"
#include "core/metrics.h"
#include "core/divelist.h"
#include "core/qthelper.h"
#include "core/subsurface-string.h"
#include <QIcon>
#include <QDebug>

static int nitrox_sort_value(struct dive *dive)
{
	int o2, he, o2max;
	get_dive_gas(dive, &o2, &he, &o2max);
	return he * 1000 + o2;
}

static QVariant dive_table_alignment(int column)
{
	QVariant retVal;
	switch (column) {
	case DiveTripModel::DEPTH:
	case DiveTripModel::DURATION:
	case DiveTripModel::TEMPERATURE:
	case DiveTripModel::TOTALWEIGHT:
	case DiveTripModel::SAC:
	case DiveTripModel::OTU:
	case DiveTripModel::MAXCNS:
		// Right align numeric columns
		retVal = int(Qt::AlignRight | Qt::AlignVCenter);
		break;
	// NR needs to be left aligned becase its the indent marker for trips too
	case DiveTripModel::NR:
	case DiveTripModel::DATE:
	case DiveTripModel::RATING:
	case DiveTripModel::SUIT:
	case DiveTripModel::CYLINDER:
	case DiveTripModel::GAS:
	case DiveTripModel::TAGS:
	case DiveTripModel::PHOTOS:
	case DiveTripModel::COUNTRY:
	case DiveTripModel::LOCATION:
		retVal = int(Qt::AlignLeft | Qt::AlignVCenter);
		break;
	}
	return retVal;
}

QVariant TripItem::data(int column, int role) const
{
	QVariant ret;
	bool oneDayTrip=true;

	if (role == DiveTripModel::TRIP_ROLE)
		return QVariant::fromValue<void *>(trip);

	if (role == DiveTripModel::SORT_ROLE)
		return (qulonglong)trip->when;

	if (role == Qt::DisplayRole) {
		switch (column) {
		case DiveTripModel::NR:
			QString shownText;
			struct dive *d = trip->dives;
			int countShown = 0;
			while (d) {
				if (!d->hidden_by_filter)
					countShown++;
				oneDayTrip &= is_same_day (trip->when,  d->when);
				d = d->next;
			}
			if (countShown < trip->nrdives)
				shownText = tr("(%1 shown)").arg(countShown);
			if (!empty_string(trip->location))
				ret = QString(trip->location) + ", " + get_trip_date_string(trip->when, trip->nrdives, oneDayTrip) + " "+ shownText;
			else
				ret = get_trip_date_string(trip->when, trip->nrdives, oneDayTrip) + shownText;
			break;
		}
	}

	return ret;
}


QVariant DiveItem::data(int column, int role) const
{
	QVariant retVal;
	QString icon_names[4] = {":zero",":photo-in-icon", ":photo-out-icon", ":photo-in-out-icon" };
	struct dive *dive = get_dive_by_uniq_id(diveId);
	if (!dive)
		return QVariant();

	switch (role) {
	case Qt::TextAlignmentRole:
		retVal = dive_table_alignment(column);
		break;
	case DiveTripModel::SORT_ROLE:
		Q_ASSERT(dive != NULL);
		switch (column) {
		case NR:
			retVal = (qlonglong)dive->when;
			break;
		case DATE:
			retVal = (qlonglong)dive->when;
			break;
		case RATING:
			retVal = dive->rating;
			break;
		case DEPTH:
			retVal = dive->maxdepth.mm;
			break;
		case DURATION:
			retVal = dive->duration.seconds;
			break;
		case TEMPERATURE:
			retVal = dive->watertemp.mkelvin;
			break;
		case TOTALWEIGHT:
			retVal = total_weight(dive);
			break;
		case SUIT:
			retVal = QString(dive->suit);
			break;
		case CYLINDER:
			retVal = QString(dive->cylinder[0].type.description);
			break;
		case GAS:
			retVal = nitrox_sort_value(dive);
			break;
		case SAC:
			retVal = dive->sac;
			break;
		case OTU:
			retVal = dive->otu;
			break;
		case MAXCNS:
			retVal = dive->maxcns;
			break;
		case TAGS:
			retVal = displayTags();
			break;
		case PHOTOS:
			retVal = countPhotos(dive);
			break;
		case COUNTRY:
			retVal = QString(get_dive_country(dive));
			break;
		case LOCATION:
			retVal = QString(get_dive_location(dive));
			break;
		}
		break;
	case Qt::DisplayRole:
		Q_ASSERT(dive != NULL);
		switch (column) {
		case NR:
			retVal = dive->number;
			break;
		case DATE:
			retVal = displayDate();
			break;
		case DEPTH:
			retVal = prefs.units.show_units_table ? displayDepthWithUnit() : displayDepth();
			break;
		case DURATION:
			retVal = displayDuration();
			break;
		case TEMPERATURE:
			retVal = prefs.units.show_units_table ? retVal = displayTemperatureWithUnit() : displayTemperature();
			break;
		case TOTALWEIGHT:
			retVal = prefs.units.show_units_table ? retVal = displayWeightWithUnit() : displayWeight();
			break;
		case SUIT:
			retVal = QString(dive->suit);
			break;
		case CYLINDER:
			retVal = QString(dive->cylinder[0].type.description);
			break;
		case SAC:
			retVal = prefs.units.show_units_table ? retVal = displaySacWithUnit() : displaySac();
			break;
		case OTU:
			retVal = dive->otu;
			break;
		case MAXCNS:
			if (prefs.units.show_units_table)
				retVal = QString("%1%").arg(dive->maxcns);
			else
				retVal = dive->maxcns;
			break;
		case TAGS:
			retVal = displayTags();
			break;
		case PHOTOS:
			break;
		case COUNTRY:
			retVal = QString(get_dive_country(dive));
			break;
		case LOCATION:
			retVal = QString(get_dive_location(dive));
			break;
		case GAS:
			const char *gas_string = get_dive_gas_string(dive);
			retVal = QString(gas_string);
			free((void*)gas_string);
			break;
		}
		break;
	case Qt::DecorationRole:
		switch (column) {
		//TODO: ADD A FLAG
		case COUNTRY:
			retVal = QVariant();
			break;
		case LOCATION:
			if (dive_has_gps_location(dive)) {
				IconMetrics im = defaultIconMetrics();
				retVal = QIcon(":globe-icon").pixmap(im.sz_small, im.sz_small);
			}
			break;
		case PHOTOS:
			if (dive->picture_list)
			{
				IconMetrics im = defaultIconMetrics();
				retVal = QIcon(icon_names[countPhotos(dive)]).pixmap(im.sz_small, im.sz_small);
			}	 // If there are photos, show one of the three photo icons: fish= photos during dive;
			break;	 // sun=photos before/after dive; sun+fish=photos during dive as well as before/after
		}
		break;
	case Qt::ToolTipRole:
		switch (column) {
		case NR:
			retVal = tr("#");
			break;
		case DATE:
			retVal = tr("Date");
			break;
		case RATING:
			retVal = tr("Rating");
			break;
		case DEPTH:
			retVal = tr("Depth(%1)").arg((get_units()->length == units::METERS) ? tr("m") : tr("ft"));
			break;
		case DURATION:
			retVal = tr("Duration");
			break;
		case TEMPERATURE:
			retVal = tr("Temp.(%1%2)").arg(UTF8_DEGREE).arg((get_units()->temperature == units::CELSIUS) ? "C" : "F");
			break;
		case TOTALWEIGHT:
			retVal = tr("Weight(%1)").arg((get_units()->weight == units::KG) ? tr("kg") : tr("lbs"));
			break;
		case SUIT:
			retVal = tr("Suit");
			break;
		case CYLINDER:
			retVal = tr("Cylinder");
			break;
		case GAS:
			retVal = tr("Gas");
			break;
		case SAC:
			const char *unit;
			get_volume_units(0, NULL, &unit);
			retVal = tr("SAC(%1)").arg(QString(unit).append(tr("/min")));
			break;
		case OTU:
			retVal = tr("OTU");
			break;
		case MAXCNS:
			retVal = tr("Max. CNS");
			break;
		case TAGS:
			retVal = tr("Tags");
			break;
		case PHOTOS:
			retVal = tr("Media before/during/after dive");
			break;
		case COUNTRY:
			retVal = tr("Country");
			break;
		case LOCATION:
			retVal = tr("Location");
			break;
		}
		break;
	}

	if (role == DiveTripModel::STAR_ROLE) {
		Q_ASSERT(dive != NULL);
		retVal = dive->rating;
	}
	if (role == DiveTripModel::DIVE_ROLE) {
		retVal = QVariant::fromValue<void *>(dive);
	}
	if (role == DiveTripModel::DIVE_IDX) {
		Q_ASSERT(dive != NULL);
		retVal = get_divenr(dive);
	}
	return retVal;
}

Qt::ItemFlags DiveItem::flags(const QModelIndex &index) const
{
	if (index.column() == NR) {
		return TreeItem::flags(index) | Qt::ItemIsEditable;
	}
	return TreeItem::flags(index);
}

bool DiveItem::setData(const QModelIndex &index, const QVariant &value, int role)
{
	if (role != Qt::EditRole)
		return false;
	if (index.column() != NR)
		return false;

	int v = value.toInt();
	if (v == 0)
		return false;

	int i;
	struct dive *d;
	for_each_dive (i, d) {
		if (d->number == v)
			return false;
	}
	d = get_dive_by_uniq_id(diveId);
	d->number = value.toInt();
	mark_divelist_changed(true);
	return true;
}

QString DiveItem::displayDate() const
{
	struct dive *dive = get_dive_by_uniq_id(diveId);
	return get_dive_date_string(dive->when);
}

QString DiveItem::displayDepth() const
{
	struct dive *dive = get_dive_by_uniq_id(diveId);
	return get_depth_string(dive->maxdepth);
}

QString DiveItem::displayDepthWithUnit() const
{
	struct dive *dive = get_dive_by_uniq_id(diveId);
	return get_depth_string(dive->maxdepth, true);
}

int DiveItem::countPhotos(dive *dive) const
{	// Determine whether dive has pictures, and whether they were taken during or before/after dive.
	const int bufperiod = 120; // A 2-min buffer period. Photos within 2 min of dive are assumed as
	int diveTotaltime = dive_endtime(dive) - dive->when;	// taken during the dive, not before/after.
	int pic_offset, icon_index = 0;
	FOR_EACH_PICTURE (dive) {		// Step through each of the pictures for this dive:
		pic_offset = picture->offset.seconds;
		if  ((pic_offset < -bufperiod) | (pic_offset > diveTotaltime+bufperiod)) {
			icon_index |= 0x02;	// If picture is before/after the dive
		}				//  then set the appropriate bit ...
		else {
			icon_index |= 0x01;	// else set the bit for picture during the dive
		}
	}
	return icon_index;	// return value: 0=no pictures; 1=pictures during dive;
}				// 2=pictures before/after; 3=pictures during as well as before/after

QString DiveItem::displayDuration() const
{
	struct dive *dive = get_dive_by_uniq_id(diveId);
	if (prefs.units.show_units_table)
		return get_dive_duration_string(dive->duration.seconds, tr("h"), tr("min"), "", ":", dive->dc.divemode == FREEDIVE);
	else
		return get_dive_duration_string(dive->duration.seconds, "", "", "", ":", dive->dc.divemode == FREEDIVE);
}

QString DiveItem::displayTemperature() const
{
	struct dive *dive = get_dive_by_uniq_id(diveId);
	if (!dive->watertemp.mkelvin)
		return QString();
	return get_temperature_string(dive->watertemp, false);
}

QString DiveItem::displayTemperatureWithUnit() const
{
	struct dive *dive = get_dive_by_uniq_id(diveId);
	if (!dive->watertemp.mkelvin)
		return QString();
	return get_temperature_string(dive->watertemp, true);
}

QString DiveItem::displaySac() const
{
	struct dive *dive = get_dive_by_uniq_id(diveId);
	if (!dive->sac)
		return QString();
	return get_volume_string(dive->sac, false);
}

QString DiveItem::displaySacWithUnit() const
{
	struct dive *dive = get_dive_by_uniq_id(diveId);
	if (!dive->sac)
		return QString();
	return get_volume_string(dive->sac, true).append(tr("/min"));
}

QString DiveItem::displayWeight() const
{
	return weight_string(weight());
}

QString DiveItem::displayWeightWithUnit() const
{
	return weight_string(weight()) + ((get_units()->weight == units::KG) ? tr("kg") : tr("lbs"));
}

QString DiveItem::displayTags() const
{
	struct dive *dive = get_dive_by_uniq_id(diveId);
	return get_taglist_string(dive->tag_list);
}

int DiveItem::weight() const
{
	struct dive *dive = get_dive_by_uniq_id(diveId);
	weight_t tw = { total_weight(dive) };
	return tw.grams;
}

DiveTripModel::DiveTripModel(QObject *parent) :
	TreeModel(parent),
	currentLayout(TREE)
{
	columns = COLUMNS;
	// setup the default width of columns (px)
	columnWidthMap = QVector<int>(COLUMNS);
	// pre-fill with 50px; the rest are explicit
	for(int i = 0; i < COLUMNS; i++)
		columnWidthMap[i] = 50;
	columnWidthMap[NR] = 70;
	columnWidthMap[DATE] = 140;
	columnWidthMap[RATING] = 90;
	columnWidthMap[SUIT] = 70;
	columnWidthMap[SAC] = 70;
	columnWidthMap[PHOTOS] = 5;
	columnWidthMap[LOCATION] = 500;
}

Qt::ItemFlags DiveTripModel::flags(const QModelIndex &index) const
{
	if (!index.isValid())
		return 0;

	TripItem *item = static_cast<TripItem *>(index.internalPointer());
	return item->flags(index);
}

QVariant DiveTripModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	QVariant ret;
	if (orientation == Qt::Vertical)
		return ret;

	switch (role) {
	case Qt::TextAlignmentRole:
		ret = dive_table_alignment(section);
		break;
	case Qt::FontRole:
		ret = defaultModelFont();
		break;
	case Qt::DisplayRole:
		switch (section) {
		case NR:
			ret = tr("#");
			break;
		case DATE:
			ret = tr("Date");
			break;
		case RATING:
			ret = tr("Rating");
			break;
		case DEPTH:
			ret = tr("Depth");
			break;
		case DURATION:
			ret = tr("Duration");
			break;
		case TEMPERATURE:
			ret = tr("Temp.");
			break;
		case TOTALWEIGHT:
			ret = tr("Weight");
			break;
		case SUIT:
			ret = tr("Suit");
			break;
		case CYLINDER:
			ret = tr("Cylinder");
			break;
		case GAS:
			ret = tr("Gas");
			break;
		case SAC:
			ret = tr("SAC");
			break;
		case OTU:
			ret = tr("OTU");
			break;
		case MAXCNS:
			ret = tr("Max CNS");
			break;
		case TAGS:
			ret = tr("Tags");
			break;
		case PHOTOS:
			ret = tr("Media");
			break;
		case COUNTRY:
			ret = tr("Country");
			break;
		case LOCATION:
			ret = tr("Location");
			break;
		}
		break;
	case Qt::ToolTipRole:
		switch (section) {
		case NR:
			ret = tr("#");
			break;
		case DATE:
			ret = tr("Date");
			break;
		case RATING:
			ret = tr("Rating");
			break;
		case DEPTH:
			ret = tr("Depth(%1)").arg((get_units()->length == units::METERS) ? tr("m") : tr("ft"));
			break;
		case DURATION:
			ret = tr("Duration");
			break;
		case TEMPERATURE:
			ret = tr("Temp.(%1%2)").arg(UTF8_DEGREE).arg((get_units()->temperature == units::CELSIUS) ? "C" : "F");
			break;
		case TOTALWEIGHT:
			ret = tr("Weight(%1)").arg((get_units()->weight == units::KG) ? tr("kg") : tr("lbs"));
			break;
		case SUIT:
			ret = tr("Suit");
			break;
		case CYLINDER:
			ret = tr("Cylinder");
			break;
		case GAS:
			ret = tr("Gas");
			break;
		case SAC:
			const char *unit;
			get_volume_units(0, NULL, &unit);
			ret = tr("SAC(%1)").arg(QString(unit).append(tr("/min")));
			break;
		case OTU:
			ret = tr("OTU");
			break;
		case MAXCNS:
			ret = tr("Max CNS");
			break;
		case TAGS:
			ret = tr("Tags");
			break;
		case PHOTOS:
			ret = tr("Media before/during/after dive");
			break;
		case LOCATION:
			ret = tr("Location");
			break;
		}
		break;
	}

	return ret;
}

void DiveTripModel::setupModelData()
{
	int i = dive_table.nr;

	if (rowCount()) {
		beginRemoveRows(QModelIndex(), 0, rowCount() - 1);
		endRemoveRows();
	}

	if (autogroup)
		autogroup_dives();
	dive_table.preexisting = dive_table.nr;
	while (--i >= 0) {
		struct dive *dive = get_dive(i);
		update_cylinder_related_info(dive);
		dive_trip_t *trip = dive->divetrip;

		DiveItem *diveItem = new DiveItem();
		diveItem->diveId = dive->id;

		if (!trip || currentLayout == LIST) {
			diveItem->parent = rootItem;
			rootItem->children.push_back(diveItem);
			continue;
		}
		if (currentLayout == LIST)
			continue;

		if (!trips.keys().contains(trip)) {
			TripItem *tripItem = new TripItem();
			tripItem->trip = trip;
			tripItem->parent = rootItem;
			tripItem->children.push_back(diveItem);
			trips[trip] = tripItem;
			rootItem->children.push_back(tripItem);
			continue;
		}
		TripItem *tripItem = trips[trip];
		tripItem->children.push_back(diveItem);
	}

	if (rowCount()) {
		beginInsertRows(QModelIndex(), 0, rowCount() - 1);
		endInsertRows();
	}
}

DiveTripModel::Layout DiveTripModel::layout() const
{
	return currentLayout;
}

void DiveTripModel::setLayout(DiveTripModel::Layout layout)
{
	currentLayout = layout;
	setupModelData();
}

bool DiveTripModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
	TreeItem *item = static_cast<TreeItem *>(index.internalPointer());
	DiveItem *diveItem = dynamic_cast<DiveItem *>(item);
	if (!diveItem)
		return false;
	return diveItem->setData(index, value, role);
}

int DiveTripModel::columnWidth(int column)
{
	if (column > COLUMNS - 1 || column < 0) {
		qWarning() << "DiveTripModel::columnWidth(): not a valid column index -" << column;
		return 50;
	}
	return columnWidthMap[column];
}

void DiveTripModel::setColumnWidth(int column, int width)
{
	if (column > COLUMNS - 1 || column < 0) {
		qWarning() << "DiveTripModel::setColumnWidth(): not a valid column index -" << column;
		return;
	}
	columnWidthMap[column] = width;
}
