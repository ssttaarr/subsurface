// SPDX-License-Identifier: GPL-2.0
#include "preferencesdialog.h"

#include "abstractpreferenceswidget.h"
#include "preferences_language.h"
#include "preferences_georeference.h"
#include "preferences_defaults.h"
#include "preferences_units.h"
#include "preferences_graph.h"
#include "preferences_network.h"

#include "core/qthelper.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QStackedWidget>
#include <QDialogButtonBox>
#include <QAbstractButton>
#include <QDebug>

PreferencesDialog* PreferencesDialog::instance()
{
	static PreferencesDialog *self = new PreferencesDialog();
	return self;
}

void PreferencesDialog::emitSettingsChanged()
{
	emit settingsChanged();
}

PreferencesDialog::PreferencesDialog()
{
	//FIXME: This looks wrong.
	//QSettings s;
	//s.beginGroup("GeneralSettings");
	//s.setValue("default_directory", system_default_directory());
	//s.endGroup();

	setWindowIcon(QIcon(":subsurface-icon"));
	pagesList = new QListWidget();
	pagesStack = new QStackedWidget();
	buttonBox = new QDialogButtonBox(
		QDialogButtonBox::Save |
		QDialogButtonBox::Apply |
		QDialogButtonBox::Cancel);

	pagesList->setMinimumWidth(120);
	pagesList->setMaximumWidth(120);

	QHBoxLayout *h = new QHBoxLayout();
	h->addWidget(pagesList);
	h->addWidget(pagesStack);
	QVBoxLayout *v = new QVBoxLayout();
	v->addLayout(h);
	v->addWidget(buttonBox);

	setLayout(v);

	addPreferencePage(new PreferencesLanguage());
	addPreferencePage(new PreferencesGeoreference());
	addPreferencePage(new PreferencesDefaults());
	addPreferencePage(new PreferencesUnits());
	addPreferencePage(new PreferencesGraph());
	addPreferencePage(new PreferencesNetwork());
	refreshPages();

	connect(pagesList, &QListWidget::currentRowChanged,
		pagesStack, &QStackedWidget::setCurrentIndex);
	connect(buttonBox, &QDialogButtonBox::clicked,
		this, &PreferencesDialog::buttonClicked);
}

PreferencesDialog::~PreferencesDialog()
{
}

void PreferencesDialog::buttonClicked(QAbstractButton* btn)
{
	QDialogButtonBox::ButtonRole role = buttonBox->buttonRole(btn);
	switch(role) {
	case QDialogButtonBox::ApplyRole : applyRequested(false); return;
	case QDialogButtonBox::AcceptRole : applyRequested(true); return;
	case QDialogButtonBox::RejectRole : cancelRequested(); return;
	case QDialogButtonBox::ResetRole : defaultsRequested(); return;
	default: return;
	}
}

bool abstractpreferenceswidget_lessthan(AbstractPreferencesWidget *p1, AbstractPreferencesWidget *p2)
{
	return p1->positionHeight() < p2->positionHeight();
}

void PreferencesDialog::addPreferencePage(AbstractPreferencesWidget *page)
{
	pages.push_back(page);
	qSort(pages.begin(), pages.end(), abstractpreferenceswidget_lessthan);
}

void PreferencesDialog::refreshPages()
{
	// Remove things
	pagesList->clear();
	while(pagesStack->count()) {
		QWidget *curr = pagesStack->widget(0);
		pagesStack->removeWidget(curr);
		curr->setParent(0);
	}

	// Read things
	Q_FOREACH(AbstractPreferencesWidget *page, pages) {
		QListWidgetItem *item = new QListWidgetItem(page->icon(), page->name());
		pagesList->addItem(item);
		pagesStack->addWidget(page);
		page->refreshSettings();
	}
}

void PreferencesDialog::applyRequested(bool closeIt)
{
	Q_FOREACH(AbstractPreferencesWidget *page, pages) {
		connect(page, &AbstractPreferencesWidget::settingsChanged, this, &PreferencesDialog::settingsChanged, Qt::UniqueConnection);
		page->syncSettings();
	}
	emit settingsChanged();
	if (closeIt)
		accept();
}

void PreferencesDialog::cancelRequested()
{
	Q_FOREACH(AbstractPreferencesWidget *page, pages) {
		page->refreshSettings();
	}
	reject();
}

void PreferencesDialog::defaultsRequested()
{
	copy_prefs(&default_prefs, &prefs);
	Q_FOREACH(AbstractPreferencesWidget *page, pages) {
		page->refreshSettings();
	}
	emit settingsChanged();
	accept();
}
