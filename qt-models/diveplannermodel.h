// SPDX-License-Identifier: GPL-2.0
#ifndef DIVEPLANNERMODEL_H
#define DIVEPLANNERMODEL_H

#include <QAbstractTableModel>
#include <QDateTime>

#include "core/dive.h"

class DivePlannerPointsModel : public QAbstractTableModel {
	Q_OBJECT
public:
	static DivePlannerPointsModel *instance();
	enum Sections {
		REMOVE,
		DEPTH,
		DURATION,
		RUNTIME,
		GAS,
		CCSETPOINT,
		DIVEMODE,
		COLUMNS
	};
	enum Mode {
		NOTHING,
		PLAN,
		ADD
	};
	virtual int columnCount(const QModelIndex &parent = QModelIndex()) const;
	virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
	virtual int rowCount(const QModelIndex &parent = QModelIndex()) const;
	virtual QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
	virtual bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole);
	virtual Qt::ItemFlags flags(const QModelIndex &index) const;
	void gasChange(const QModelIndex &index, int newcylinderid);
	void cylinderRenumber(int mapping[]);
	void removeSelectedPoints(const QVector<int> &rows);
	void setPlanMode(Mode mode);
	bool isPlanner();
	void createSimpleDive();
	void setupStartTime();
	void clear();
	Mode currentMode() const;
	bool setRecalc(bool recalc);
	bool recalcQ();
	bool tankInUse(int cylinderid);
	void setupCylinders();
	bool updateMaxDepth();
	/**
	 * @return the row number.
	 */
	void editStop(int row, divedatapoint newData);
	divedatapoint at(int row);
	int size();
	struct diveplan &getDiveplan();
	int lastEnteredPoint();
	void removeDeco();
	static bool addingDeco;
	struct deco_state final_deco_state;

public
slots:
	int addStop(int millimeters = 0, int seconds = 0, int cylinderid_in = -1, int ccpoint = 0, bool entered = true, enum divemode_t = UNDEF_COMP_TYPE);
	void addCylinder_clicked();
	void setGFHigh(const int gfhigh);
	void setGFLow(const int gflow);
	void setVpmbConservatism(int level);
	void setSurfacePressure(int pressure);
	void setSalinity(int salinity);
	int getSurfacePressure();
	void setBottomSac(double sac);
	void setDecoSac(double sac);
	void setStartTime(const QTime &t);
	void setStartDate(const QDate &date);
	void setLastStop6m(bool value);
	void setDropStoneMode(bool value);
	void setVerbatim(bool value);
	void setDisplayRuntime(bool value);
	void setDisplayDuration(bool value);
	void setDisplayTransitions(bool value);
	void setDisplayVariations(bool value);
	void setDecoMode(int mode);
	void setSafetyStop(bool value);
	void savePlan();
	void saveDuplicatePlan();
	void remove(const QModelIndex &index);
	void cancelPlan();
	void createTemporaryPlan();
	void deleteTemporaryPlan();
	void loadFromDive(dive *d);
	void emitDataChanged();
	void setRebreatherMode(int mode);
	void setReserveGas(int reserve);
	void setSwitchAtReqStop(bool value);
	void setMinSwitchDuration(int duration);
	void setSacFactor(double factor);
	void setProblemSolvingTime(int minutes);
	void setAscrate75(int rate);
	void setAscrate50(int rate);
	void setAscratestops(int rate);
	void setAscratelast6m(int rate);
	void setDescrate(int rate);

signals:
	void planCreated();
	void planCanceled();
	void cylinderModelEdited();
	void startTimeChanged(QDateTime);
	void recreationChanged(bool);
	void calculatedPlanNotes();
	void variationsComputed(QString);

private:
	explicit DivePlannerPointsModel(QObject *parent = 0);
	void createPlan(bool replanCopy);
	struct diveplan diveplan;
	struct divedatapoint *cloneDiveplan(struct diveplan *plan_src, struct diveplan *plan_copy);
	void computeVariations(struct diveplan *diveplan, struct deco_state *ds);
	int analyzeVariations(struct decostop *min, struct decostop *mid, struct decostop *max, const char *unit);
	Mode mode;
	bool recalc;
	QVector<divedatapoint> divepoints;
	QDateTime startTime;
	int instanceCounter = 0;
	struct deco_state ds_after_previous_dives;
};

#endif
