// SPDX-License-Identifier: GPL-2.0
#ifndef DIVETOOLTIPITEM_H
#define DIVETOOLTIPITEM_H

#include <QGraphicsRectItem>
#include <QVector>
#include <QPair>
#include <QRectF>
#include <QIcon>
#include <QTime>
#include "core/display.h"

class DiveCartesianAxis;
class QGraphicsLineItem;
class QGraphicsSimpleTextItem;
class QGraphicsPixmapItem;

/* To use a tooltip, simply ->setToolTip on the QGraphicsItem that you want
 * or, if it's a "global" tooltip, set it on the mouseMoveEvent of the ProfileGraphicsView.
 */
class ToolTipItem : public QObject, public QGraphicsRectItem {
	Q_OBJECT
	void updateTitlePosition();
	Q_PROPERTY(QRectF rect READ rect WRITE setRect)

public:
	enum Status {
		COLLAPSED,
		EXPANDED
	};

	explicit ToolTipItem(QGraphicsItem *parent = 0);
	virtual ~ToolTipItem();

	void collapse();
	void expand();
	void clear();
	void addToolTip(const QString &toolTip, const QIcon &icon = QIcon(), const QPixmap &pixmap = QPixmap());
	void refresh(const QPointF &pos);
	bool isExpanded() const;
	void persistPos();
	void readPos();
	void mousePressEvent(QGraphicsSceneMouseEvent *event);
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event);
	void setTimeAxis(DiveCartesianAxis *axis);
	void setPlotInfo(const plot_info &plot);
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
public
slots:
	void setRect(const QRectF &rect);

private:
	typedef QPair<QGraphicsPixmapItem *, QGraphicsSimpleTextItem *> ToolTip;
	QVector<ToolTip> toolTips;
	ToolTip entryToolTip;
	QGraphicsSimpleTextItem *title;
	Status status;
	QRectF rectangle;
	QRectF nextRectangle;
	DiveCartesianAxis *timeAxis;
	plot_info pInfo;
	int lastTime;
	QTime refreshTime;
	QList<QGraphicsItem*> oldSelection;
};

#endif // DIVETOOLTIPITEM_H
