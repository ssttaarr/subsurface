// SPDX-License-Identifier: GPL-2.0
#ifndef NOTIFICATIONWIDGET_H
#define NOTIFICATIONWIDGET_H

#include <QWidget>
#include <QFutureWatcher>

#include "desktop-widgets/kmessagewidget.h"

namespace Ui {
	class NotificationWidget;
}

class NotificationWidget : public KMessageWidget {
	Q_OBJECT

public:
	explicit NotificationWidget(QWidget *parent = 0);
	void setFuture(const QFuture<void> &future);
	void showNotification(QString message, KMessageWidget::MessageType type);
	void hideNotification();
	QString getNotificationText();

public
slots:
	void showError(QString message);
private:
	QFutureWatcher<void> future_watcher;

private
slots:
	void finish();
};

#endif // NOTIFICATIONWIDGET_H
