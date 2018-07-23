// SPDX-License-Identifier: GPL-2.0
#include "qt-models/divelistmodel.h"
#include "core/qthelper.h"
#include <QDateTime>

DiveListSortModel::DiveListSortModel(QObject *parent) : QSortFilterProxyModel(parent)
{

}

int DiveListSortModel::getDiveId(int idx)
{
	DiveListModel *mySourceModel = qobject_cast<DiveListModel *>(sourceModel());
	return mySourceModel->getDiveId(mapToSource(index(idx,0)).row());
}

int DiveListSortModel::getIdxForId(int id)
{
	for (int i = 0; i < rowCount(); i++) {
		QVariant v = data(index(i, 0), DiveListModel::DiveRole);
		DiveObjectHelper *d = v.value<DiveObjectHelper *>();
		if (d->id() == id)
			return i;
	}
	return -1;
}

void DiveListSortModel::clear()
{
	DiveListModel *mySourceModel = qobject_cast<DiveListModel *>(sourceModel());
	mySourceModel->clear();
}

void DiveListSortModel::addAllDives()
{
	DiveListModel *mySourceModel = qobject_cast<DiveListModel *>(sourceModel());
	mySourceModel->addAllDives();
}

DiveListModel *DiveListModel::m_instance = NULL;

DiveListModel::DiveListModel(QObject *parent) : QAbstractListModel(parent)
{
	m_instance = this;
}

void DiveListModel::addDive(QList<dive *>listOfDives)
{
	if (listOfDives.isEmpty())
		return;
	beginInsertRows(QModelIndex(), rowCount(), rowCount() + listOfDives.count() - 1);
	foreach (dive *d, listOfDives) {
		m_dives.append(new DiveObjectHelper(d));
	}
	endInsertRows();
}

void DiveListModel::addAllDives()
{
	QList<dive *>listOfDives;
	int i;
	struct dive *d;
	for_each_dive (i, d)
		listOfDives.append(d);
	addDive(listOfDives);

}

void DiveListModel::insertDive(int i, DiveObjectHelper *newDive)
{
	beginInsertRows(QModelIndex(), i, i);
	m_dives.insert(i, newDive);
	endInsertRows();
}

void DiveListModel::removeDive(int i)
{
	beginRemoveRows(QModelIndex(), i, i);
	delete m_dives.at(i);
	m_dives.removeAt(i);
	endRemoveRows();
}

void DiveListModel::removeDiveById(int id)
{
	for (int i = 0; i < rowCount(); i++) {
		if (m_dives.at(i)->id() == id) {
			removeDive(i);
			return;
		}
	}
}

void DiveListModel::updateDive(int i, dive *d)
{
	DiveObjectHelper *newDive = new DiveObjectHelper(d);
	// we need to make sure that QML knows that this dive has changed -
	// the only reliable way I've found is to remove and re-insert it
	removeDive(i);
	insertDive(i, newDive);
}

void DiveListModel::clear()
{
	if (m_dives.count()) {
		beginRemoveRows(QModelIndex(), 0, m_dives.count() - 1);
		qDeleteAll(m_dives);
		m_dives.clear();
		endRemoveRows();
	}
}

void DiveListModel::resetInternalData()
{
	// this is a hack. There is a long standing issue, that seems related to a
	// sync problem between QML engine and underlying model data. It causes delete
	// from divelist (on mobile) to crash. But not always. This function is part of
	// an attempt to fix this. See commit.
	beginResetModel();
	endResetModel();
}

int DiveListModel::rowCount(const QModelIndex &) const
{
	return m_dives.count();
}

int DiveListModel::getDiveId(int idx) const
{
	if (idx < 0 || idx >= m_dives.count())
		return -1;
	return m_dives[idx]->id();
}

int DiveListModel::getDiveIdx(int id) const
{
	int i;
	for (i = 0; i < m_dives.count(); i++) {
		if (m_dives.at(i)->id() == id)
			return i;
	}
	return -1;
}

QVariant DiveListModel::data(const QModelIndex &index, int role) const
{
	if(index.row() < 0 || index.row() > m_dives.count())
		return QVariant();

	DiveObjectHelper *curr_dive = m_dives[index.row()];
	switch(role) {
	case DiveRole: return QVariant::fromValue<QObject*>(curr_dive);
	case DiveDateRole: return (qlonglong)curr_dive->timestamp();
	}
	return QVariant();

}

QHash<int, QByteArray> DiveListModel::roleNames() const
{
	QHash<int, QByteArray> roles;
	roles[DiveRole] = "dive";
	roles[DiveDateRole] = "date";
	return roles;
}

// create a new dive. set the current time and add it to the end of the dive list
QString DiveListModel::startAddDive()
{
	struct dive *d;
	d = alloc_dive();
	d->when = QDateTime::currentMSecsSinceEpoch() / 1000L + gettimezoneoffset();

	// find the highest dive nr we have and pick the next one
	struct dive *pd;
	int i, nr = 0;
	for_each_dive(i, pd) {
		if (pd->number > nr)
			nr = pd->number;
	}
	nr++;
	d->number = nr;
	d->dc.model = strdup("manually added dive");
	add_single_dive(-1, d);
	insertDive(get_idx_by_uniq_id(d->id), new DiveObjectHelper(d));
	return QString::number(d->id);
}

DiveListModel *DiveListModel::instance()
{
	return m_instance;
}

DiveObjectHelper* DiveListModel::at(int i)
{
	return m_dives.at(i);
}
