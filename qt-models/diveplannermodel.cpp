// SPDX-License-Identifier: GPL-2.0
#include "diveplannermodel.h"
#include "core/subsurface-string.h"
#include "qt-models/cylindermodel.h"
#include "core/planner.h"
#include "qt-models/models.h"
#include "core/device.h"
#include "core/qthelper.h"
#include "core/subsurface-qt/SettingsObjectWrapper.h"
#include "core/gettextfromc.h"
#include <QApplication>
#include <QTextDocument>
#include <QtConcurrent>

#define VARIATIONS_IN_BACKGROUND 1

#define UNIT_FACTOR ((prefs.units.length == units::METERS) ? 1000.0 / 60.0 : feet_to_mm(1.0) / 60.0)

/* TODO: Port this to CleanerTableModel to remove a bit of boilerplate and
 * use the signal warningMessage() to communicate errors to the MainWindow.
 */
void DivePlannerPointsModel::removeSelectedPoints(const QVector<int> &rows)
{
	if (!rows.count())
		return;
	int firstRow = rowCount() - rows.count();
	QVector<int> v2 = rows;
	std::sort(v2.begin(), v2.end());

	beginRemoveRows(QModelIndex(), firstRow, rowCount() - 1);
	for (int i = v2.count() - 1; i >= 0; i--) {
		divepoints.remove(v2[i]);
	}
	endRemoveRows();
	CylindersModel::instance()->updateTrashIcon();
}

void DivePlannerPointsModel::createSimpleDive()
{
	// initialize the start time in the plan
	diveplan.when = displayed_dive.when;

	// Use gas from the first cylinder
	int cylinderid = 0;

	// If we're in drop_stone_mode, don't add a first point.
	// It will be added implicit.
	if (!prefs.drop_stone_mode)
		addStop(M_OR_FT(15, 45), 1 * 60, cylinderid, 0, true, UNDEF_COMP_TYPE);

	addStop(M_OR_FT(15, 45), 20 * 60, 0, 0, true, UNDEF_COMP_TYPE);
	if (!isPlanner()) {
		addStop(M_OR_FT(5, 15), 42 * 60, 0, cylinderid, true, UNDEF_COMP_TYPE);
		addStop(M_OR_FT(5, 15), 45 * 60, 0, cylinderid, true, UNDEF_COMP_TYPE);
	}
	updateMaxDepth();
	GasSelectionModel::instance()->repopulate();
	DiveTypeSelectionModel::instance()->repopulate();
}

void DivePlannerPointsModel::setupStartTime()
{
	// if the latest dive is in the future, then start an hour after it ends
	// otherwise start an hour from now
	startTime = QDateTime::currentDateTimeUtc().addSecs(3600 + gettimezoneoffset());
	if (dive_table.nr) {
		struct dive *d = get_dive(dive_table.nr - 1);
		time_t ends = dive_endtime(d);
		time_t diff = ends - startTime.toTime_t();
		if (diff > 0) {
			startTime = startTime.addSecs(diff + 3600);
		}
	}
	emit startTimeChanged(startTime);
}

void DivePlannerPointsModel::loadFromDive(dive *d)
{
	int depthsum = 0;
	int samplecount = 0;
	o2pressure_t last_sp;
	bool oldRec = recalc;
	struct divecomputer *dc = &(d->dc);
	struct event *evd = NULL;
	enum divemode_t current_divemode = UNDEF_COMP_TYPE;
	recalc = false;
	CylindersModel::instance()->updateDive();
	duration_t lasttime = { 0 };
	duration_t lastrecordedtime = {};
	duration_t newtime = {};
	free_dps(&diveplan);
	if (mode != PLAN)
		clear();
	diveplan.when = d->when;
	// is this a "new" dive where we marked manually entered samples?
	// if yes then the first sample should be marked
	// if it is we only add the manually entered samples as waypoints to the diveplan
	// otherwise we have to add all of them

	bool hasMarkedSamples = false;

	if (dc->samples)
		hasMarkedSamples = dc->sample[0].manually_entered;
	else
		fake_dc(dc);

	// if this dive has more than 100 samples (so it is probably a logged dive),
	// average samples so we end up with a total of 100 samples.
	int plansamples = dc->samples <= 100 ? dc->samples : 100;
	int j = 0;
	int cylinderid = 0;
	last_sp.mbar = 0;
	for (int i = 0; i < plansamples - 1; i++) {
		if (dc->last_manual_time.seconds && dc->last_manual_time.seconds > 120 && lasttime.seconds >= dc->last_manual_time.seconds)
			break;
		while (j * plansamples <= i * dc->samples) {
			const sample &s = dc->sample[j];
			const sample &prev = dc->sample[j-1];
			if (s.time.seconds != 0 && (!hasMarkedSamples || s.manually_entered)) {
				depthsum += s.depth.mm;
				last_sp = prev.setpoint;
				++samplecount;
				newtime = s.time;
			}
			j++;
		}
		if (samplecount) {
			cylinderid = get_cylinderid_at_time(d, dc, lasttime);
			if (newtime.seconds - lastrecordedtime.seconds > 10) {
				current_divemode = get_current_divemode(dc, newtime.seconds - 1, &evd, &current_divemode);
				addStop(depthsum / samplecount, newtime.seconds, cylinderid, last_sp.mbar, true, current_divemode);
				lastrecordedtime = newtime;
			}
			lasttime = newtime;
			depthsum = 0;
			samplecount = 0;
		}
	}
	// make sure we get the last point right so the duration is correct
	current_divemode = get_current_divemode(dc, d->dc.duration.seconds, &evd, &current_divemode);
	if (!hasMarkedSamples && !dc->last_manual_time.seconds)
		addStop(0, d->dc.duration.seconds,cylinderid, last_sp.mbar, true, current_divemode);
	recalc = oldRec;
	DiveTypeSelectionModel::instance()->repopulate();
	emitDataChanged();
}

// copy the tanks from the current dive, or the default cylinder
// or an unknown cylinder
// setup the cylinder widget accordingly
void DivePlannerPointsModel::setupCylinders()
{
	int i;
	if (mode == PLAN && current_dive) {
		// take the displayed cylinders from the selected dive as starting point
		CylindersModel::instance()->copyFromDive(current_dive);
		copy_cylinders(current_dive, &displayed_dive, !prefs.display_unused_tanks);
		reset_cylinders(&displayed_dive, true);

		for (i = 0; i < MAX_CYLINDERS; i++)
			if (!cylinder_none(&(displayed_dive.cylinder[i])))
				return;		// We have at least one cylinder
	}
	if (!empty_string(prefs.default_cylinder)) {
		fill_default_cylinder(&displayed_dive.cylinder[0]);
		displayed_dive.cylinder[0].start = displayed_dive.cylinder[0].type.workingpressure;
	}
	if (cylinder_none(&displayed_dive.cylinder[0])) {
		// roughly an AL80
		displayed_dive.cylinder[0].type.description = copy_qstring(tr("unknown"));
		displayed_dive.cylinder[0].type.size.mliter = 11100;
		displayed_dive.cylinder[0].type.workingpressure.mbar = 207000;
	}
	reset_cylinders(&displayed_dive, false);
	CylindersModel::instance()->copyFromDive(&displayed_dive);
}

// Update the dive's maximum depth.  Returns true if max. depth changed
bool DivePlannerPointsModel::updateMaxDepth()
{
	int prevMaxDepth = displayed_dive.maxdepth.mm;
	displayed_dive.maxdepth.mm = 0;
	for (int i = 0; i < rowCount(); i++) {
		divedatapoint p = at(i);
		if (p.depth.mm > displayed_dive.maxdepth.mm)
			displayed_dive.maxdepth.mm = p.depth.mm;
	}
	return displayed_dive.maxdepth.mm != prevMaxDepth;
}

void DivePlannerPointsModel::removeDeco()
{
	bool oldrec = setRecalc(false);
	QVector<int> computedPoints;
	for (int i = 0; i < rowCount(); i++)
		if (!at(i).entered)
			computedPoints.push_back(i);
	removeSelectedPoints(computedPoints);
	setRecalc(oldrec);
}

void DivePlannerPointsModel::addCylinder_clicked()
{
	CylindersModel::instance()->add();
}



void DivePlannerPointsModel::setPlanMode(Mode m)
{
	mode = m;
	// the planner may reset our GF settings that are used to show deco
	// reset them to what's in the preferences
	if (m != PLAN) {
		set_gf(prefs.gflow, prefs.gfhigh);
		set_vpmb_conservatism(prefs.vpmb_conservatism);
	}
}

bool DivePlannerPointsModel::isPlanner()
{
	return mode == PLAN;
}

/* When the planner adds deco stops to the model, adding those should not trigger a new deco calculation.
 * We thus start the planner only when recalc is true. */

bool DivePlannerPointsModel::setRecalc(bool rec)
{
	bool old = recalc;
	recalc = rec;
	return old;
}

bool DivePlannerPointsModel::recalcQ()
{
	return recalc;
}

int DivePlannerPointsModel::columnCount(const QModelIndex&) const
{
	return COLUMNS; // to disable CCSETPOINT subtract one
}

QVariant DivePlannerPointsModel::data(const QModelIndex &index, int role) const
{
	divedatapoint p = divepoints.at(index.row());
	if (role == Qt::DisplayRole || role == Qt::EditRole) {
		switch (index.column()) {
		case CCSETPOINT:
			return (double)p.setpoint / 1000;
		case DEPTH:
			return (int) lrint(get_depth_units(p.depth.mm, NULL, NULL));
		case RUNTIME:
			return p.time / 60;
		case DURATION:
			if (index.row())
				return (p.time - divepoints.at(index.row() - 1).time) / 60;
			else
				return p.time / 60;
		case DIVEMODE:
			return gettextFromC::tr(divemode_text_ui[p.divemode]);
		case GAS:
			/* Check if we have the same gasmix two or more times
			 * If yes return more verbose string */
			int same_gas = same_gasmix_cylinder(&displayed_dive.cylinder[p.cylinderid], p.cylinderid, &displayed_dive, true);
			if (same_gas == -1)
				return get_gas_string(displayed_dive.cylinder[p.cylinderid].gasmix);
			else
				return get_gas_string(displayed_dive.cylinder[p.cylinderid].gasmix) +
					QString(" (%1 %2 ").arg(tr("cyl.")).arg(p.cylinderid + 1) +
						displayed_dive.cylinder[p.cylinderid].type.description + ")";
		}
	} else if (role == Qt::DecorationRole) {
		switch (index.column()) {
		case REMOVE:
			if (rowCount() > 1)
				return p.entered ? trashIcon() : QVariant();
			else
				return trashForbiddenIcon();
		}
	} else if (role == Qt::SizeHintRole) {
		switch (index.column()) {
		case REMOVE:
			if (rowCount() > 1)
				return p.entered ? trashIcon().size() : QVariant();
			else
				return trashForbiddenIcon().size();
		}
	} else if (role == Qt::FontRole) {
		if (divepoints.at(index.row()).entered) {
			return defaultModelFont();
		} else {
			QFont font = defaultModelFont();
			font.setBold(true);
			return font;
		}
	}
	return QVariant();
}

bool DivePlannerPointsModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
	int i, shift;
	if (role == Qt::EditRole) {
		divedatapoint &p = divepoints[index.row()];
		switch (index.column()) {
		case DEPTH:
			if (value.toInt() >= 0) {
				p.depth = units_to_depth(value.toInt());
				if (updateMaxDepth())
					CylindersModel::instance()->updateBestMixes();
			}
			break;
		case RUNTIME:
			p.time = value.toInt() * 60;
			break;
		case DURATION:
			i = index.row();
			if (i)
				shift = divepoints[i].time - divepoints[i - 1].time - value.toInt() * 60;
			else
				shift = divepoints[i].time - value.toInt() * 60;
			while (i < divepoints.size())
				divepoints[i++].time -= shift;
			break;
		case CCSETPOINT: {
			int po2 = 0;
			QByteArray gasv = value.toByteArray();
			if (validate_po2(gasv.data(), &po2))
				p.setpoint = po2;
		} break;
		case GAS:
			if (value.toInt() >= 0 && value.toInt() < MAX_CYLINDERS)
				p.cylinderid = value.toInt();
			/* Did we change the start (dp 0) cylinder to another cylinderid than 0? */
			if (value.toInt() != 0 && index.row() == 0)
				CylindersModel::instance()->moveAtFirst(value.toInt());
			CylindersModel::instance()->updateTrashIcon();
			break;
		case DIVEMODE:
			if (value.toInt() < FREEDIVE) {
				p.divemode = (enum divemode_t) value.toInt();
				p.setpoint = p.divemode == CCR ? prefs.defaultsetpoint : 0;
			}
			if (index.row() == 0)
				displayed_dive.dc.divemode = (enum divemode_t) value.toInt();
			break;
		}
		editStop(index.row(), p);
	}
	return QAbstractItemModel::setData(index, value, role);
}

void DivePlannerPointsModel::gasChange(const QModelIndex &index, int newcylinderid)
{
	int i = index.row(), oldcylinderid = divepoints[i].cylinderid;
	while (i < rowCount() && oldcylinderid == divepoints[i].cylinderid)
		divepoints[i++].cylinderid = newcylinderid;
	emitDataChanged();
}

void DivePlannerPointsModel::cylinderRenumber(int mapping[])
{
	for (int i = 0; i < rowCount(); i++) {
		if (mapping[divepoints[i].cylinderid] >= 0)
			divepoints[i].cylinderid = mapping[divepoints[i].cylinderid];
	}
	emitDataChanged();
}

QVariant DivePlannerPointsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
		switch (section) {
		case DEPTH:
			return tr("Final depth");
		case RUNTIME:
			return tr("Run time");
		case DURATION:
			return tr("Duration");
		case GAS:
			return tr("Used gas");
		case CCSETPOINT:
			return tr("CC setpoint");
		case DIVEMODE:
			return tr("Dive mode");
		}
	} else if (role == Qt::FontRole) {
		return defaultModelFont();
	}
	return QVariant();
}

Qt::ItemFlags DivePlannerPointsModel::flags(const QModelIndex &index) const
{
	if (index.column() != REMOVE)
		return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
	else
		return QAbstractItemModel::flags(index);
}

int DivePlannerPointsModel::rowCount(const QModelIndex&) const
{
	return divepoints.count();
}

DivePlannerPointsModel::DivePlannerPointsModel(QObject *parent) : QAbstractTableModel(parent),
	mode(NOTHING),
	recalc(false)
{
	memset(&diveplan, 0, sizeof(diveplan));
	startTime.setTimeSpec(Qt::UTC);
}

DivePlannerPointsModel *DivePlannerPointsModel::instance()
{
	static DivePlannerPointsModel self;
	return &self;
}

void DivePlannerPointsModel::emitDataChanged()
{
	emit dataChanged(createIndex(0, 0), createIndex(rowCount() - 1, COLUMNS - 1));
}

void DivePlannerPointsModel::setBottomSac(double sac)
{
	diveplan.bottomsac = units_to_sac(sac);
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setBottomSac(diveplan.bottomsac);
	emitDataChanged();
}

void DivePlannerPointsModel::setDecoSac(double sac)
{
	diveplan.decosac = units_to_sac(sac);
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setDecoSac(diveplan.decosac);
	emitDataChanged();
}

void DivePlannerPointsModel::setSacFactor(double factor)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setSacFactor((int) round(factor * 100));
	emitDataChanged();
}

void DivePlannerPointsModel::setProblemSolvingTime(int minutes)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setProblemSolvingTime(minutes);
	emitDataChanged();
}

void DivePlannerPointsModel::setGFHigh(const int gfhigh)
{
	if (diveplan.gfhigh != gfhigh) {
		diveplan.gfhigh = gfhigh;
		emitDataChanged();
	}
}

void DivePlannerPointsModel::setGFLow(const int gflow)
{
	if (diveplan.gflow != gflow) {
		diveplan.gflow = gflow;
		emitDataChanged();
	}
}

void DivePlannerPointsModel::setRebreatherMode(int mode)
{
	int i;
	displayed_dive.dc.divemode = (divemode_t) mode;
	for (i=0; i < rowCount(); i++) {
		divepoints[i].setpoint = mode == CCR ? prefs.defaultsetpoint : 0;
		divepoints[i].divemode = (enum divemode_t) mode;
	}
	emitDataChanged();
}

void DivePlannerPointsModel::setVpmbConservatism(int level)
{
	if (diveplan.vpmb_conservatism != level) {
		diveplan.vpmb_conservatism = level;
		emitDataChanged();
	}
}

void DivePlannerPointsModel::setSurfacePressure(int pressure)
{
	diveplan.surface_pressure = pressure;
	emitDataChanged();
}

void DivePlannerPointsModel::setSalinity(int salinity)
{
	diveplan.salinity = salinity;
	emitDataChanged();
}

int DivePlannerPointsModel::getSurfacePressure()
{
	return diveplan.surface_pressure;
}

void DivePlannerPointsModel::setLastStop6m(bool value)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setLastStop(value);
	emitDataChanged();
}

void DivePlannerPointsModel::setAscrate75(int rate)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setAscrate75(lrint(rate * UNIT_FACTOR));
	emitDataChanged();
}

void DivePlannerPointsModel::setAscrate50(int rate)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setAscrate50(lrint(rate * UNIT_FACTOR));
	emitDataChanged();
}

void DivePlannerPointsModel::setAscratestops(int rate)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setAscratestops(lrint(rate * UNIT_FACTOR));
	emitDataChanged();
}

void DivePlannerPointsModel::setAscratelast6m(int rate)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setAscratelast6m(lrint(rate * UNIT_FACTOR));
	emitDataChanged();
}

void DivePlannerPointsModel::setDescrate(int rate)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setDescrate(lrint(rate * UNIT_FACTOR));
	emitDataChanged();
}

void DivePlannerPointsModel::setVerbatim(bool value)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setVerbatimPlan(value);
	emitDataChanged();
}

void DivePlannerPointsModel::setDisplayRuntime(bool value)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setDisplayRuntime(value);
	emitDataChanged();
}

void DivePlannerPointsModel::setDisplayDuration(bool value)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setDisplayDuration(value);
	emitDataChanged();
}

void DivePlannerPointsModel::setDisplayTransitions(bool value)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setDisplayTransitions(value);
	emitDataChanged();
}

void DivePlannerPointsModel::setDisplayVariations(bool value)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setDisplayVariations(value);
	emitDataChanged();
}

void DivePlannerPointsModel::setDecoMode(int mode)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setDecoMode(deco_mode(mode));
	emit recreationChanged(mode == int(prefs.planner_deco_mode));
	emitDataChanged();
}

void DivePlannerPointsModel::setSafetyStop(bool value)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setSafetyStop(value);
	emitDataChanged();
}

void DivePlannerPointsModel::setReserveGas(int reserve)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	if (prefs.units.pressure == units::BAR)
		planner->setReserveGas(reserve * 1000);
	else
		planner->setReserveGas(psi_to_mbar(reserve));
	emitDataChanged();
}

void DivePlannerPointsModel::setDropStoneMode(bool value)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setDropStoneMode(value);
	if (prefs.drop_stone_mode) {
	/* Remove the first entry if we enable drop_stone_mode */
		if (rowCount() >= 2) {
			beginRemoveRows(QModelIndex(), 0, 0);
			divepoints.remove(0);
			endRemoveRows();
		}
	} else {
		/* Add a first entry if we disable drop_stone_mode */
		beginInsertRows(QModelIndex(), 0, 0);
		/* Copy the first current point */
		divedatapoint p = divepoints.at(0);
		p.time = p.depth.mm / prefs.descrate;
		divepoints.push_front(p);
		endInsertRows();
	}
	emitDataChanged();
}

void DivePlannerPointsModel::setSwitchAtReqStop(bool value)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setSwitchAtRequiredStop(value);
	emitDataChanged();
}

void DivePlannerPointsModel::setMinSwitchDuration(int duration)
{
	auto planner = SettingsObjectWrapper::instance()->planner_settings;
	planner->setMinSwitchDuration(duration * 60);
	emitDataChanged();
}

void DivePlannerPointsModel::setStartDate(const QDate &date)
{
	startTime.setDate(date);
	diveplan.when = startTime.toTime_t();
	displayed_dive.when = diveplan.when;
	emitDataChanged();
}

void DivePlannerPointsModel::setStartTime(const QTime &t)
{
	startTime.setTime(t);
		diveplan.when = startTime.toTime_t();
	displayed_dive.when = diveplan.when;
	emitDataChanged();
}


bool divePointsLessThan(const divedatapoint &p1, const divedatapoint &p2)
{
	return p1.time < p2.time;
}

int DivePlannerPointsModel::lastEnteredPoint()
{
	for (int i = divepoints.count() - 1; i >= 0; i--)
		if (divepoints.at(i).entered)
			return i;
	return -1;
}

// cylinderid_in == -1 means same gas as before.
// divemode == UNDEF_COMP_TYPE means determine from previous point.
int DivePlannerPointsModel::addStop(int milimeters, int seconds, int cylinderid_in, int ccpoint, bool entered, enum divemode_t divemode)
{
	int cylinderid = 0;
	bool usePrevious = false;
	if (cylinderid_in >= 0)
		cylinderid = cylinderid_in;
	else
		usePrevious = true;
	if (recalcQ())
		removeDeco();

	int row = divepoints.count();
	if (seconds == 0 && milimeters == 0 && row != 0) {
		/* this is only possible if the user clicked on the 'plus' sign on the DivePoints Table */
		const divedatapoint t = divepoints.at(lastEnteredPoint());
		milimeters = t.depth.mm;
		seconds = t.time + 600; // 10 minutes.
		cylinderid = t.cylinderid;
		ccpoint = t.setpoint;
	} else if (seconds == 0 && milimeters == 0 && row == 0) {
		milimeters = M_OR_FT(5, 15); // 5m / 15ft
		seconds = 600;		     // 10 min
		// Default to the first cylinder
		cylinderid = 0;
	}

	// check if there's already a new stop before this one:
	for (int i = 0; i < row; i++) {
		const divedatapoint &dp = divepoints.at(i);
		if (dp.time == seconds) {
			row = i;
			beginRemoveRows(QModelIndex(), row, row);
			divepoints.remove(row);
			endRemoveRows();
			break;
		}
		if (dp.time > seconds) {
			row = i;
			break;
		}
	}
	// Previous, actually means next as we are typically subdiving a segment and the gas for
	// the segment is determined by the waypoint at the end.
	if (usePrevious) {
		if (row  < divepoints.count()) {
			cylinderid = divepoints.at(row).cylinderid;
			if (divemode == UNDEF_COMP_TYPE)
				divemode = divepoints.at(row).divemode;
			ccpoint = divepoints.at(row).setpoint;
		} else if (row > 0) {
			cylinderid = divepoints.at(row - 1).cylinderid;
			if (divemode == UNDEF_COMP_TYPE)
				divemode = divepoints.at(row - 1).divemode;
			ccpoint = divepoints.at(row -1).setpoint;
		}
	}
	if (divemode == UNDEF_COMP_TYPE)
		divemode = displayed_dive.dc.divemode;

	// add the new stop
	beginInsertRows(QModelIndex(), row, row);
	divedatapoint point;
	point.depth.mm = milimeters;
	point.time = seconds;
	point.cylinderid = cylinderid;
	point.setpoint = ccpoint;
	point.entered = entered;
	point.divemode = divemode;
	point.next = NULL;
	divepoints.append(point);
	std::sort(divepoints.begin(), divepoints.end(), divePointsLessThan);
	endInsertRows();
	return row;
}

void DivePlannerPointsModel::editStop(int row, divedatapoint newData)
{
	/*
	 * When moving divepoints rigorously, we might end up with index
	 * out of range, thus returning the last one instead.
	 */
	int old_first_cylid = divepoints[0].cylinderid;
	if (row >= divepoints.count())
		return;
	divepoints[row] = newData;
	std::sort(divepoints.begin(), divepoints.end(), divePointsLessThan);
	if (updateMaxDepth())
		CylindersModel::instance()->updateBestMixes();
	if (divepoints[0].cylinderid != old_first_cylid)
		CylindersModel::instance()->moveAtFirst(divepoints[0].cylinderid);
	emitDataChanged();
}

int DivePlannerPointsModel::size()
{
	return divepoints.size();
}

divedatapoint DivePlannerPointsModel::at(int row)
{
	/*
	 * When moving divepoints rigorously, we might end up with index
	 * out of range, thus returning the last one instead.
	 */
	if (row >= divepoints.count())
		return divepoints.at(divepoints.count() - 1);
	return divepoints.at(row);
}

void DivePlannerPointsModel::remove(const QModelIndex &index)
{
	if (index.column() != REMOVE || rowCount() == 1)
		return;

	divedatapoint dp = at(index.row());
	if (!dp.entered)
		return;

	int old_first_cylid = divepoints[0].cylinderid;

/* TODO: this seems so wrong.
 * We can't do this here if we plan to use QML on mobile
 * as mobile has no ControlModifier.
 * The correct thing to do is to create a new method
 * remove method that will pass the first and last index of the
 * removed rows, and remove those in a go.
 */
	int i;
	int rows = rowCount();
	if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
		beginRemoveRows(QModelIndex(), index.row(), rows - 1);
		for (i = rows - 1; i >= index.row(); i--)
			divepoints.remove(i);
	} else {
		beginRemoveRows(QModelIndex(), index.row(), index.row());
		divepoints.remove(index.row());
	}
	endRemoveRows();
	CylindersModel::instance()->updateTrashIcon();
	if (divepoints[0].cylinderid != old_first_cylid)
		CylindersModel::instance()->moveAtFirst(divepoints[0].cylinderid);
}

struct diveplan &DivePlannerPointsModel::getDiveplan()
{
	return diveplan;
}

void DivePlannerPointsModel::cancelPlan()
{
	/* TODO:
	 * This check shouldn't be here - this is the interface responsability.
	 * as soon as the interface thinks that it could cancel the plan, this should be
	 * called.
	 */

	/*
	if (mode == PLAN && rowCount()) {
		if (QMessageBox::warning(MainWindow::instance(), TITLE_OR_TEXT(tr("Discard the plan?"),
												 tr("You are about to discard your plan.")),
					 QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Discard) != QMessageBox::Discard) {
			return;
		}
	}
	*/

	setPlanMode(NOTHING);
	free_dps(&diveplan);

	emit planCanceled();
}

DivePlannerPointsModel::Mode DivePlannerPointsModel::currentMode() const
{
	return mode;
}

bool DivePlannerPointsModel::tankInUse(int cylinderid)
{
	for (int j = 0; j < rowCount(); j++) {
		divedatapoint &p = divepoints[j];
		if (p.time == 0) // special entries that hold the available gases
			continue;
		if (!p.entered) // removing deco gases is ok
			continue;
		if (p.cylinderid == cylinderid) // tank is in use
			return true;
	}
	return false;
}

void DivePlannerPointsModel::clear()
{
	bool oldRecalc = setRecalc(false);

	CylindersModel::instance()->updateDive();
	if (rowCount() > 0) {
		beginRemoveRows(QModelIndex(), 0, rowCount() - 1);
		divepoints.clear();
		endRemoveRows();
	}
	CylindersModel::instance()->clear();
	setRecalc(oldRecalc);
}

void DivePlannerPointsModel::createTemporaryPlan()
{
	// Get the user-input and calculate the dive info
	free_dps(&diveplan);
	int lastIndex = -1;
	for (int i = 0; i < rowCount(); i++) {
		divedatapoint p = at(i);
		int deltaT = lastIndex != -1 ? p.time - at(lastIndex).time : p.time;
		lastIndex = i;
		if (i == 0 && mode == PLAN && prefs.drop_stone_mode) {
			/* Okay, we add a first segment where we go down to depth */
			plan_add_segment(&diveplan, p.depth.mm / prefs.descrate, p.depth.mm, p.cylinderid, p.setpoint, true, p.divemode);
			deltaT -= p.depth.mm / prefs.descrate;
		}
		if (p.entered)
			plan_add_segment(&diveplan, deltaT, p.depth.mm, p.cylinderid, p.setpoint, true, p.divemode);
	}

	// what does the cache do???
	struct deco_state *cache = NULL;
	struct divedatapoint *dp = NULL;
	for (int i = 0; i < MAX_CYLINDERS; i++) {
		cylinder_t *cyl = &displayed_dive.cylinder[i];
		if (cyl->depth.mm && cyl->cylinder_use != NOT_USED) {
			dp = create_dp(0, cyl->depth.mm, i, 0);
			if (diveplan.dp) {
				dp->next = diveplan.dp;
				diveplan.dp = dp;
			} else {
				dp->next = NULL;
				diveplan.dp = dp;
			}
		}
	}
#if DEBUG_PLAN
	dump_plan(&diveplan);
#endif
	if (recalcQ() && !diveplan_empty(&diveplan)) {
		struct decostop stoptable[60];
		struct deco_state plan_deco_state;
		struct diveplan *plan_copy;

		plan(&plan_deco_state, &diveplan, &displayed_dive, DECOTIMESTEP, stoptable, &cache, isPlanner(), false);
		plan_copy = (struct diveplan *)malloc(sizeof(struct diveplan));
		lock_planner();
		cloneDiveplan(&diveplan, plan_copy);
		unlock_planner();
#ifdef VARIATIONS_IN_BACKGROUND
		QtConcurrent::run(this, &DivePlannerPointsModel::computeVariations, plan_copy, &plan_deco_state);
#else
		computeVariations(plan_copy, &plan_deco_state);
#endif
		final_deco_state = plan_deco_state;
		emit calculatedPlanNotes();
	}
	// throw away the cache
	free(cache);
#if DEBUG_PLAN
	save_dive(stderr, &displayed_dive);
	dump_plan(&diveplan);
#endif
}

void DivePlannerPointsModel::deleteTemporaryPlan()
{
	free_dps(&diveplan);
}

void DivePlannerPointsModel::savePlan()
{
	createPlan(false);
}

void DivePlannerPointsModel::saveDuplicatePlan()
{
	createPlan(true);
}

struct divedatapoint * DivePlannerPointsModel::cloneDiveplan(struct diveplan *plan_src, struct diveplan *plan_copy)
{
	divedatapoint *src, *last_segment;
	divedatapoint **dp;

	src = plan_src->dp;
	*plan_copy = *plan_src;
	dp = &plan_copy->dp;
	while (src && (!src->time || src->entered)) {
		*dp = (struct divedatapoint *)malloc(sizeof(struct divedatapoint));
		**dp = *src;
		dp = &(*dp)->next;
		src = src->next;
	}
	(*dp) = NULL;

	last_segment = plan_copy->dp;
	while (last_segment && last_segment->next && last_segment->next->next)
		last_segment = last_segment->next;
	return last_segment;
}

int DivePlannerPointsModel::analyzeVariations(struct decostop *min, struct decostop *mid, struct decostop *max, const char *unit)
{
	int minsum = 0;
	int midsum = 0;
	int maxsum = 0;
	int leftsum = 0;
	int rightsum = 0;

	while (min->depth) {
		minsum += min->time;
		++min;
	}
	while (mid->depth) {
		midsum += mid->time;
		++mid;
	}
	while (max->depth) {
		maxsum += max->time;
		++max;
	}

	leftsum = midsum - minsum;
	rightsum = maxsum - midsum;

#ifdef DEBUG_STOPVAR
	printf("Total + %d:%02d/%s +- %d s/%s\n\n", FRACTION((leftsum + rightsum) / 2, 60), unit,
						       (rightsum - leftsum) / 2, unit);
#else
	Q_UNUSED(unit)
#endif
	return (leftsum + rightsum) / 2;
}

void DivePlannerPointsModel::computeVariations(struct diveplan *original_plan, struct deco_state *previos_ds)
{

//	bool oldRecalc = setRecalc(false);
	struct dive *dive = alloc_dive();
	copy_dive(&displayed_dive, dive);
	struct decostop original[60], deeper[60], shallower[60], shorter[60], longer[60];
	struct deco_state *cache = NULL, *save = NULL;
	struct diveplan plan_copy;
	struct divedatapoint *last_segment;
	struct deco_state ds = *previos_ds;

	if (!original_plan)
		return;


	if (in_planner() && prefs.display_variations && decoMode() != RECREATIONAL) {
		int my_instance = ++instanceCounter;
		cache_deco_state(&ds, &save);

		duration_t delta_time = { .seconds = 60 };
		QString time_units = tr("min");
		depth_t delta_depth;
		QString depth_units;

		if (prefs.units.length == units::METERS) {
			delta_depth.mm = 1000; // 1m
			depth_units = tr("m");
		} else {
			delta_depth.mm = feet_to_mm(1.0); // 1ft
			depth_units = tr("ft");
		}

		last_segment = cloneDiveplan(original_plan, &plan_copy);
		if (!last_segment)
			goto finish;
		if (my_instance != instanceCounter)
			goto finish;
		plan(&ds, &plan_copy, dive, 1, original, &cache, true, false);
		free_dps(&plan_copy);
		restore_deco_state(save, &ds, false);

		last_segment = cloneDiveplan(original_plan, &plan_copy);
		last_segment->depth.mm += delta_depth.mm;
		last_segment->next->depth.mm += delta_depth.mm;
		if (my_instance != instanceCounter)
			goto finish;
		plan(&ds, &plan_copy, dive, 1, deeper, &cache, true, false);
		free_dps(&plan_copy);
		restore_deco_state(save, &ds, false);

		last_segment = cloneDiveplan(original_plan, &plan_copy);
		last_segment->depth.mm -= delta_depth.mm;
		last_segment->next->depth.mm -= delta_depth.mm;
		if (my_instance != instanceCounter)
			goto finish;
		plan(&ds, &plan_copy, dive, 1, shallower, &cache, true, false);
		free_dps(&plan_copy);
		restore_deco_state(save, &ds, false);

		last_segment = cloneDiveplan(original_plan, &plan_copy);
		last_segment->next->time += delta_time.seconds;
		if (my_instance != instanceCounter)
			goto finish;
		plan(&ds, &plan_copy, dive, 1, longer, &cache, true, false);
		free_dps(&plan_copy);
		restore_deco_state(save, &ds, false);

		last_segment = cloneDiveplan(original_plan, &plan_copy);
		last_segment->next->time -= delta_time.seconds;
		if (my_instance != instanceCounter)
			goto finish;
		plan(&ds, &plan_copy, dive, 1, shorter, &cache, true, false);
		free_dps(&plan_copy);
		restore_deco_state(save, &ds, false);

		char buf[200];
		sprintf(buf, ", %s: + %d:%02d /%s + %d:%02d /min", qPrintable(tr("Stop times")),
			FRACTION(analyzeVariations(shallower, original, deeper, qPrintable(depth_units)), 60), qPrintable(depth_units),
			FRACTION(analyzeVariations(shorter, original, longer, qPrintable(time_units)), 60));

		emit variationsComputed(QString(buf));
#ifdef DEBUG_STOPVAR
		printf("\n\n");
#endif
	}
finish:
	free_dps(original_plan);
	free(original_plan);
	free(save);
	free(cache);
	free(dive);
//	setRecalc(oldRecalc);
}

void DivePlannerPointsModel::createPlan(bool replanCopy)
{
	// Ok, so, here the diveplan creates a dive
	struct deco_state *cache = NULL;
	bool oldRecalc = setRecalc(false);
	removeDeco();
	createTemporaryPlan();
	setRecalc(oldRecalc);

	//TODO: C-based function here?
	struct decostop stoptable[60];
	plan(&ds_after_previous_dives, &diveplan, &displayed_dive, DECOTIMESTEP, stoptable, &cache, isPlanner(), true);
	struct diveplan *plan_copy;
	plan_copy = (struct diveplan *)malloc(sizeof(struct diveplan));
	lock_planner();
	cloneDiveplan(&diveplan, plan_copy);
	unlock_planner();
	computeVariations(plan_copy, &ds_after_previous_dives);

	free(cache);
	if (!current_dive || displayed_dive.id != current_dive->id) {
		// we were planning a new dive, not re-planning an existing on
		record_dive(clone_dive(&displayed_dive));
	} else if (current_dive && displayed_dive.id == current_dive->id) {
		// we are replanning a dive - make sure changes are reflected
		// correctly in the dive structure and copy it back into the dive table
		displayed_dive.maxdepth.mm = 0;
		displayed_dive.dc.maxdepth.mm = 0;
		fixup_dive(&displayed_dive);
		// Try to identify old planner output and remove only this part
		// Treat user provided text as plain text.
		QTextDocument notesDocument;
		notesDocument.setHtml(current_dive->notes);
		QString oldnotes(notesDocument.toPlainText());
		int disclaimerPosition = oldnotes.indexOf(disclaimer);
		if (disclaimerPosition == 0)
			oldnotes.clear();
		else if (disclaimerPosition >= 1)
			oldnotes.truncate(disclaimerPosition-1);
		// Deal with line breaks
		oldnotes.replace("\n", "<br>");
		oldnotes.append(displayed_dive.notes);
		displayed_dive.notes = copy_qstring(oldnotes);
		// If we save as new create a copy of the dive here
		if (replanCopy) {
			struct dive *copy = alloc_dive();
			copy_dive(current_dive, copy);
			copy->id = 0;
			copy->selected = false;
			copy->divetrip = NULL;
			if (current_dive->divetrip)
				add_dive_to_trip(copy, current_dive->divetrip);
			record_dive(copy);
		}
		copy_dive(&displayed_dive, current_dive);
	}
	mark_divelist_changed(true);
	sort_table(&dive_table);

	// Remove and clean the diveplan, so we don't delete
	// the dive by mistake.
	free_dps(&diveplan);
	setPlanMode(NOTHING);
	planCreated();
}
