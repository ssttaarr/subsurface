// SPDX-License-Identifier: GPL-2.0
#include "desktop-widgets/divecomputermanagementdialog.h"
#include "desktop-widgets/mainwindow.h"
#include "core/qthelper.h"
#include "qt-models/divecomputermodel.h"
#include <QMessageBox>
#include <QShortcut>

DiveComputerManagementDialog::DiveComputerManagementDialog(QWidget *parent, Qt::WindowFlags f) : QDialog(parent, f)
{
	ui.setupUi(this);
	init();
	connect(ui.tableView, SIGNAL(clicked(QModelIndex)), this, SLOT(tryRemove(QModelIndex)));
	QShortcut *close = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_W), this);
	connect(close, SIGNAL(activated()), this, SLOT(close()));
	QShortcut *quit = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q), this);
	connect(quit, SIGNAL(activated()), parent, SLOT(close()));
}

void DiveComputerManagementDialog::init()
{
	model.reset(new DiveComputerModel);
	ui.tableView->setModel(model.data());
	ui.tableView->resizeColumnsToContents();
	ui.tableView->setColumnWidth(DiveComputerModel::REMOVE, 22);
	layout()->activate();
}

DiveComputerManagementDialog *DiveComputerManagementDialog::instance()
{
	static DiveComputerManagementDialog *self = new DiveComputerManagementDialog(MainWindow::instance());
	return self;
}

void DiveComputerManagementDialog::tryRemove(const QModelIndex &index)
{
	if (index.column() != DiveComputerModel::REMOVE)
		return;

	QMessageBox::StandardButton response = QMessageBox::question(
	    this, TITLE_OR_TEXT(
		      tr("Remove the selected dive computer?"),
		      tr("Are you sure that you want to \n remove the selected dive computer?")),
	    QMessageBox::Ok | QMessageBox::Cancel);

	if (response == QMessageBox::Ok)
		model->remove(index);
}

void DiveComputerManagementDialog::accept()
{
	model->keepWorkingList();
	hide();
	close();
}

void DiveComputerManagementDialog::reject()
{
	hide();
	close();
}
