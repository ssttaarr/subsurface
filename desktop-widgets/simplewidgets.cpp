// SPDX-License-Identifier: GPL-2.0
#include "desktop-widgets/simplewidgets.h"
#include "qt-models/filtermodels.h"

#include <QProcess>
#include <QFileDialog>
#include <QShortcut>
#include <QCalendarWidget>
#include <QKeyEvent>
#include <QAction>
#include <QDesktopServices>
#include <QToolTip>

#include "core/file.h"
#include "desktop-widgets/mainwindow.h"
#include "core/qthelper.h"
#include "libdivecomputer/parser.h"
#include "desktop-widgets/divelistview.h"
#include "core/display.h"
#include "profile-widget/profilewidget2.h"
#include "desktop-widgets/undocommands.h"
#include "core/metadata.h"

class MinMaxAvgWidgetPrivate {
public:
	QLabel *avgIco, *avgValue;
	QLabel *minIco, *minValue;
	QLabel *maxIco, *maxValue;

	MinMaxAvgWidgetPrivate(MinMaxAvgWidget *owner)
	{
		avgIco = new QLabel(owner);
		avgIco->setPixmap(QIcon(":value-average-icon").pixmap(16, 16));
		avgIco->setToolTip(gettextFromC::tr("Average"));
		minIco = new QLabel(owner);
		minIco->setPixmap(QIcon(":value-minimum-icon").pixmap(16, 16));
		minIco->setToolTip(gettextFromC::tr("Minimum"));
		maxIco = new QLabel(owner);
		maxIco->setPixmap(QIcon(":value-maximum-icon").pixmap(16, 16));
		maxIco->setToolTip(gettextFromC::tr("Maximum"));
		avgValue = new QLabel(owner);
		minValue = new QLabel(owner);
		maxValue = new QLabel(owner);

		QGridLayout *formLayout = new QGridLayout();
		formLayout->addWidget(maxIco, 0, 0);
		formLayout->addWidget(maxValue, 0, 1);
		formLayout->addWidget(avgIco, 1, 0);
		formLayout->addWidget(avgValue, 1, 1);
		formLayout->addWidget(minIco, 2, 0);
		formLayout->addWidget(minValue, 2, 1);
		owner->setLayout(formLayout);
	}
};

double MinMaxAvgWidget::average() const
{
	return d->avgValue->text().toDouble();
}

double MinMaxAvgWidget::maximum() const
{
	return d->maxValue->text().toDouble();
}
double MinMaxAvgWidget::minimum() const
{
	return d->minValue->text().toDouble();
}

MinMaxAvgWidget::MinMaxAvgWidget(QWidget*) : d(new MinMaxAvgWidgetPrivate(this))
{
}

MinMaxAvgWidget::~MinMaxAvgWidget()
{
}

void MinMaxAvgWidget::clear()
{
	d->avgValue->setText(QString());
	d->maxValue->setText(QString());
	d->minValue->setText(QString());
}

void MinMaxAvgWidget::setAverage(double average)
{
	d->avgValue->setText(QString::number(average));
}

void MinMaxAvgWidget::setMaximum(double maximum)
{
	d->maxValue->setText(QString::number(maximum));
}
void MinMaxAvgWidget::setMinimum(double minimum)
{
	d->minValue->setText(QString::number(minimum));
}

void MinMaxAvgWidget::setAverage(const QString &average)
{
	d->avgValue->setText(average);
}

void MinMaxAvgWidget::setMaximum(const QString &maximum)
{
	d->maxValue->setText(maximum);
}

void MinMaxAvgWidget::setMinimum(const QString &minimum)
{
	d->minValue->setText(minimum);
}

void MinMaxAvgWidget::overrideMinToolTipText(const QString &newTip)
{
	d->minIco->setToolTip(newTip);
	d->minValue->setToolTip(newTip);
}

void MinMaxAvgWidget::overrideAvgToolTipText(const QString &newTip)
{
	d->avgIco->setToolTip(newTip);
	d->avgValue->setToolTip(newTip);
}

void MinMaxAvgWidget::overrideMaxToolTipText(const QString &newTip)
{
	d->maxIco->setToolTip(newTip);
	d->maxValue->setToolTip(newTip);
}

void MinMaxAvgWidget::setAvgVisibility(const bool visible)
{
	d->avgIco->setVisible(visible);
	d->avgValue->setVisible(visible);
}

RenumberDialog *RenumberDialog::instance()
{
	static RenumberDialog *self = new RenumberDialog(MainWindow::instance());
	return self;
}

void RenumberDialog::renumberOnlySelected(bool selected)
{
	if (selected && amount_selected == 1)
		ui.renumberText->setText(tr("New number"));
	else
		ui.renumberText->setText(tr("New starting number"));

	if (selected)
		ui.groupBox->setTitle(tr("Renumber selected dives"));
	else
		ui.groupBox->setTitle(tr("Renumber all dives"));

	selectedOnly = selected;
}

void RenumberDialog::buttonClicked(QAbstractButton *button)
{
	if (ui.buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole) {
		MainWindow::instance()->dive_list()->rememberSelection();
		// we remember a map from dive uuid to a pair of old number / new number
		QMap<int, QPair<int, int>> renumberedDives;
		int i;
		int newNr = ui.spinBox->value();
		struct dive *dive = NULL;
		for_each_dive (i, dive) {
			if (!selectedOnly || dive->selected) {
				invalidate_dive_cache(dive);
				renumberedDives.insert(dive->id, QPair<int, int>(dive->number, newNr++));
			}
		}
		UndoRenumberDives *undoCommand = new UndoRenumberDives(renumberedDives);
		MainWindow::instance()->undoStack->push(undoCommand);

		MainWindow::instance()->dive_list()->fixMessyQtModelBehaviour();
		mark_divelist_changed(true);
		MainWindow::instance()->dive_list()->restoreSelection();
	}
}

RenumberDialog::RenumberDialog(QWidget *parent) : QDialog(parent), selectedOnly(false)
{
	ui.setupUi(this);
	connect(ui.buttonBox, SIGNAL(clicked(QAbstractButton *)), this, SLOT(buttonClicked(QAbstractButton *)));
	QShortcut *close = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_W), this);
	connect(close, SIGNAL(activated()), this, SLOT(close()));
	QShortcut *quit = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q), this);
	connect(quit, SIGNAL(activated()), parent, SLOT(close()));
}

SetpointDialog *SetpointDialog::instance()
{
	static SetpointDialog *self = new SetpointDialog(MainWindow::instance());
	return self;
}

void SetpointDialog::setpointData(struct divecomputer *divecomputer, int second)
{
	dc = divecomputer;
	time = second < 0 ? 0 : second;
}

void SetpointDialog::buttonClicked(QAbstractButton *button)
{
	if (ui.buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole && dc) {
		add_event(dc, time, SAMPLE_EVENT_PO2, 0, (int)(1000.0 * ui.spinbox->value()), 
			QT_TRANSLATE_NOOP("gettextFromC", "SP change"));
		invalidate_dive_cache(current_dive);
	}
	mark_divelist_changed(true);
	MainWindow::instance()->graphics()->replot();
}

SetpointDialog::SetpointDialog(QWidget *parent) : QDialog(parent),
	dc(0), time(0)
{
	ui.setupUi(this);
	connect(ui.buttonBox, SIGNAL(clicked(QAbstractButton *)), this, SLOT(buttonClicked(QAbstractButton *)));
	QShortcut *close = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_W), this);
	connect(close, SIGNAL(activated()), this, SLOT(close()));
	QShortcut *quit = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q), this);
	connect(quit, SIGNAL(activated()), parent, SLOT(close()));
}

ShiftTimesDialog *ShiftTimesDialog::instance()
{
	static ShiftTimesDialog *self = new ShiftTimesDialog(MainWindow::instance());
	return self;
}

void ShiftTimesDialog::buttonClicked(QAbstractButton *button)
{
	int amount;

	if (ui.buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole) {
		amount = ui.timeEdit->time().hour() * 3600 + ui.timeEdit->time().minute() * 60;
		if (ui.backwards->isChecked())
			amount *= -1;
		if (amount != 0) {
			// DANGER, DANGER - this could get our dive_table unsorted...
			int i;
			struct dive *dive;
			QList<int> affectedDives;
			for_each_dive (i, dive) {
				if (!dive->selected)
					continue;

				affectedDives.append(dive->id);
			}
			MainWindow::instance()->undoStack->push(new UndoShiftTime(affectedDives, amount));
			sort_table(&dive_table);
			mark_divelist_changed(true);
			MainWindow::instance()->dive_list()->rememberSelection();
			MainWindow::instance()->refreshDisplay();
			MainWindow::instance()->dive_list()->restoreSelection();
		}
	}
}

void ShiftTimesDialog::showEvent(QShowEvent*)
{
	ui.timeEdit->setTime(QTime(0, 0, 0, 0));
	when = get_times(); //get time of first selected dive
	ui.currentTime->setText(get_dive_date_string(when));
	ui.shiftedTime->setText(get_dive_date_string(when));
}

void ShiftTimesDialog::changeTime()
{
	int amount;

	amount = ui.timeEdit->time().hour() * 3600 + ui.timeEdit->time().minute() * 60;
	if (ui.backwards->isChecked())
		amount *= -1;

	ui.shiftedTime->setText(get_dive_date_string(amount + when));
}

ShiftTimesDialog::ShiftTimesDialog(QWidget *parent) : QDialog(parent),
	when(0)
{
	ui.setupUi(this);
	connect(ui.buttonBox, SIGNAL(clicked(QAbstractButton *)), this, SLOT(buttonClicked(QAbstractButton *)));
	connect(ui.timeEdit, SIGNAL(timeChanged(const QTime)), this, SLOT(changeTime()));
	connect(ui.backwards, SIGNAL(toggled(bool)), this, SLOT(changeTime()));
	QShortcut *close = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_W), this);
	connect(close, SIGNAL(activated()), this, SLOT(close()));
	QShortcut *quit = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q), this);
	connect(quit, SIGNAL(activated()), parent, SLOT(close()));
}

void ShiftImageTimesDialog::buttonClicked(QAbstractButton *button)
{
	if (ui.buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole) {
		m_amount = ui.timeEdit->time().hour() * 3600 + ui.timeEdit->time().minute() * 60;
		if (ui.backwards->isChecked())
			m_amount *= -1;
	}
}

void ShiftImageTimesDialog::syncCameraClicked()
{
	QPixmap picture;
	QStringList fileNames = QFileDialog::getOpenFileNames(this,
							      tr("Open image file"),
							      DiveListView::lastUsedImageDir(),
							      tr("Image files") + " (*.jpg *.jpeg)");
	if (fileNames.isEmpty())
		return;

	picture.load(fileNames.at(0));
	ui.displayDC->setEnabled(true);
	QGraphicsScene *scene = new QGraphicsScene(this);

	scene->addPixmap(picture.scaled(ui.DCImage->size()));
	ui.DCImage->setScene(scene);

	dcImageEpoch = picture_get_timestamp(qPrintable(fileNames.at(0)));
	QDateTime dcDateTime = QDateTime::fromTime_t(dcImageEpoch, Qt::UTC);
	ui.dcTime->setDateTime(dcDateTime);
	connect(ui.dcTime, SIGNAL(dateTimeChanged(const QDateTime &)), this, SLOT(dcDateTimeChanged(const QDateTime &)));
}

void ShiftImageTimesDialog::dcDateTimeChanged(const QDateTime &newDateTime)
{
	QDateTime newtime(newDateTime);
	if (!dcImageEpoch)
		return;
	newtime.setTimeSpec(Qt::UTC);
	setOffset(newtime.toTime_t() - dcImageEpoch);
}

void ShiftImageTimesDialog::matchAllImagesToggled(bool state)
{
	matchAllImages = state;
}

bool ShiftImageTimesDialog::matchAll()
{
	return matchAllImages;
}

ShiftImageTimesDialog::ShiftImageTimesDialog(QWidget *parent, QStringList fileNames) : QDialog(parent),
	fileNames(fileNames),
	m_amount(0),
	matchAllImages(false)
{
	ui.setupUi(this);
	connect(ui.buttonBox, SIGNAL(clicked(QAbstractButton *)), this, SLOT(buttonClicked(QAbstractButton *)));
	connect(ui.syncCamera, SIGNAL(clicked()), this, SLOT(syncCameraClicked()));
	connect(ui.timeEdit, SIGNAL(timeChanged(const QTime &)), this, SLOT(timeEditChanged(const QTime &)));
	connect(ui.backwards, SIGNAL(toggled(bool)), this, SLOT(timeEditChanged()));
	connect(ui.matchAllImages, SIGNAL(toggled(bool)), this, SLOT(matchAllImagesToggled(bool)));
	dcImageEpoch = (time_t)0;

	updateInvalid();
}

time_t ShiftImageTimesDialog::amount() const
{
	return m_amount;
}

void ShiftImageTimesDialog::setOffset(time_t offset)
{
	if (offset >= 0) {
		ui.forward->setChecked(true);
	} else {
		ui.backwards->setChecked(true);
		offset *= -1;
	}
	ui.timeEdit->setTime(QTime(offset / 3600, (offset % 3600) / 60, offset % 60));
}

void ShiftImageTimesDialog::updateInvalid()
{
	timestamp_t timestamp;
	bool allValid = true;
	ui.warningLabel->hide();
	ui.invalidFilesText->hide();
	QDateTime time_first = QDateTime::fromTime_t(first_selected_dive()->when, Qt::UTC);
	QDateTime time_last = QDateTime::fromTime_t(last_selected_dive()->when, Qt::UTC);
	if (first_selected_dive() == last_selected_dive())
		ui.invalidFilesText->setPlainText(tr("Selected dive date/time") + ": " + time_first.toString());
	else {
		ui.invalidFilesText->setPlainText(tr("First selected dive date/time") + ": " + time_first.toString());
		ui.invalidFilesText->append(tr("Last selected dive date/time") + ": " + time_last.toString());
	}
	ui.invalidFilesText->append(tr("\nFiles with inappropriate date/time") + ":");

	Q_FOREACH (const QString &fileName, fileNames) {
		if (picture_check_valid(qPrintable(fileName), m_amount))
			continue;

		// We've found invalid image
		timestamp = picture_get_timestamp(qPrintable(fileName));
		time_first.setTime_t(timestamp + m_amount);
		if (timestamp == 0)
			ui.invalidFilesText->append(fileName + " - " + tr("No Exif date/time found"));
		else
			ui.invalidFilesText->append(fileName + " - " + time_first.toString());
		allValid = false;
	}

	if (!allValid) {
		ui.warningLabel->show();
		ui.invalidFilesText->show();
	}
}

void ShiftImageTimesDialog::timeEditChanged(const QTime &time)
{
	QDateTimeEdit::Section timeEditSection = ui.timeEdit->currentSection();
	ui.timeEdit->setEnabled(false);
	m_amount = time.hour() * 3600 + time.minute() * 60;
	if (ui.backwards->isChecked())
		m_amount *= -1;
	updateInvalid();
	ui.timeEdit->setEnabled(true);
	ui.timeEdit->setFocus();
	ui.timeEdit->setSelectedSection(timeEditSection);
}

void ShiftImageTimesDialog::timeEditChanged()
{
	if ((m_amount > 0) == ui.backwards->isChecked())
		m_amount *= -1;
	if (m_amount)
		updateInvalid();
}

URLDialog::URLDialog(QWidget *parent) : QDialog(parent)
{
	ui.setupUi(this);
	QShortcut *close = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_W), this);
	connect(close, SIGNAL(activated()), this, SLOT(close()));
	QShortcut *quit = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q), this);
	connect(quit, SIGNAL(activated()), parent, SLOT(close()));
}

QString URLDialog::url() const
{
	return ui.urlField->text();
}

bool isGnome3Session()
{
#if defined(QT_OS_WIW) || defined(QT_OS_MAC)
	return false;
#else
	if (qApp->style()->objectName() != "gtk+")
		return false;
	QProcess p;
	p.start("pidof", QStringList() << "gnome-shell");
	p.waitForFinished(-1);
	QString p_stdout = p.readAllStandardOutput();
	return !p_stdout.isEmpty();
#endif
}

#define COMPONENT_FROM_UI(_component) what->_component = ui._component->isChecked()
#define UI_FROM_COMPONENT(_component) ui._component->setChecked(what->_component)

DiveComponentSelection::DiveComponentSelection(QWidget *parent, struct dive *target, struct dive_components *_what) : targetDive(target)
{
	ui.setupUi(this);
	what = _what;
	UI_FROM_COMPONENT(divesite);
	UI_FROM_COMPONENT(divemaster);
	UI_FROM_COMPONENT(buddy);
	UI_FROM_COMPONENT(rating);
	UI_FROM_COMPONENT(visibility);
	UI_FROM_COMPONENT(notes);
	UI_FROM_COMPONENT(suit);
	UI_FROM_COMPONENT(tags);
	UI_FROM_COMPONENT(cylinders);
	UI_FROM_COMPONENT(weights);
	connect(ui.buttonBox, SIGNAL(clicked(QAbstractButton *)), this, SLOT(buttonClicked(QAbstractButton *)));
	QShortcut *close = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_W), this);
	connect(close, SIGNAL(activated()), this, SLOT(close()));
	QShortcut *quit = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q), this);
	connect(quit, SIGNAL(activated()), parent, SLOT(close()));
}

void DiveComponentSelection::buttonClicked(QAbstractButton *button)
{
	if (ui.buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole) {
		COMPONENT_FROM_UI(divesite);
		COMPONENT_FROM_UI(divemaster);
		COMPONENT_FROM_UI(buddy);
		COMPONENT_FROM_UI(rating);
		COMPONENT_FROM_UI(visibility);
		COMPONENT_FROM_UI(notes);
		COMPONENT_FROM_UI(suit);
		COMPONENT_FROM_UI(tags);
		COMPONENT_FROM_UI(cylinders);
		COMPONENT_FROM_UI(weights);
		selective_copy_dive(&displayed_dive, targetDive, *what, true);
	}
}

void FilterBase::addContextMenuEntry(const QString &s, void (FilterModelBase::*fn)())
{
	QAction *act = new QAction(s, this);
	connect(act, &QAction::triggered, model, fn);
	ui.filterList->addAction(act);
}

FilterBase::FilterBase(FilterModelBase *model_, QWidget *parent) : QWidget(parent),
	model(model_)
{
	ui.setupUi(this);
#if QT_VERSION >= 0x050200
	ui.filterInternalList->setClearButtonEnabled(true);
#endif
	QSortFilterProxyModel *filter = new QSortFilterProxyModel();
	filter->setSourceModel(model);
	filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
	connect(ui.filterInternalList, SIGNAL(textChanged(QString)), filter, SLOT(setFilterFixedString(QString)));
	connect(ui.notButton, &QToolButton::toggled, model, &FilterModelBase::setNegate);
	ui.filterList->setModel(filter);

	addContextMenuEntry(tr("Select All"), &FilterModelBase::selectAll);
	addContextMenuEntry(tr("Unselect All"), &FilterModelBase::clearFilter);
	addContextMenuEntry(tr("Invert Selection"), &FilterModelBase::invertSelection);
	ui.filterList->setContextMenuPolicy(Qt::ActionsContextMenu);
}

void FilterBase::showEvent(QShowEvent *event)
{
	MultiFilterSortModel::instance()->addFilterModel(model);
	QWidget::showEvent(event);
}

void FilterBase::hideEvent(QHideEvent *event)
{
	MultiFilterSortModel::instance()->removeFilterModel(model);
	QWidget::hideEvent(event);
}

TagFilter::TagFilter(QWidget *parent) : FilterBase(TagFilterModel::instance(), parent)
{
	ui.label->setText(tr("Tags: "));
}

BuddyFilter::BuddyFilter(QWidget *parent) : FilterBase(BuddyFilterModel::instance(), parent)
{
	ui.label->setText(tr("Person: "));
	ui.label->setToolTip(tr("Searches for buddies and divemasters"));
}

LocationFilter::LocationFilter(QWidget *parent) : FilterBase(LocationFilterModel::instance(), parent)
{
	ui.label->setText(tr("Location: "));
}

SuitFilter::SuitFilter(QWidget *parent) : FilterBase(SuitsFilterModel::instance(), parent)
{
	ui.label->setText(tr("Suits: "));
}

MultiFilter::MultiFilter(QWidget *parent) : QWidget(parent)
{
	ui.setupUi(this);

	QWidget *expandedWidget = new QWidget();
	QHBoxLayout *l = new QHBoxLayout();

	TagFilter *tagFilter = new TagFilter(this);
	int minimumHeight = tagFilter->ui.filterInternalList->height() +
			    tagFilter->ui.verticalLayout->spacing() * tagFilter->ui.verticalLayout->count();

	QListView *dummyList = new QListView();
	QStringListModel *dummy = new QStringListModel(QStringList() << "Dummy Text");
	dummyList->setModel(dummy);

	connect(ui.close, SIGNAL(clicked(bool)), this, SLOT(closeFilter()));
	connect(ui.clear, SIGNAL(clicked(bool)), MultiFilterSortModel::instance(), SLOT(clearFilter()));
	connect(ui.maximize, SIGNAL(clicked(bool)), this, SLOT(adjustHeight()));

	l->addWidget(tagFilter);
	l->addWidget(new BuddyFilter());
	l->addWidget(new LocationFilter());
	l->addWidget(new SuitFilter());
	l->setContentsMargins(0, 0, 0, 0);
	l->setSpacing(0);
	expandedWidget->setLayout(l);

	ui.scrollArea->setWidget(expandedWidget);
	expandedWidget->resize(expandedWidget->width(), minimumHeight + dummyList->sizeHintForRow(0) * 5);
	ui.scrollArea->setMinimumHeight(expandedWidget->height() + 5);

	connect(MultiFilterSortModel::instance(), SIGNAL(filterFinished()), this, SLOT(filterFinished()));
}

void MultiFilter::filterFinished()
{
	ui.filterText->setText(tr("Filter shows %1 (of %2) dives").arg(MultiFilterSortModel::instance()->divesDisplayed).arg(dive_table.nr));
}

void MultiFilter::adjustHeight()
{
	ui.scrollArea->setVisible(!ui.scrollArea->isVisible());
}

void MultiFilter::closeFilter()
{
	MultiFilterSortModel::instance()->clearFilter();
	hide();
	MainWindow::instance()->setCheckedActionFilterTags(false);
}

TextHyperlinkEventFilter::TextHyperlinkEventFilter(QTextEdit *txtEdit) : QObject(txtEdit),
	textEdit(txtEdit),
	scrollView(textEdit->viewport())
{
	// If you install the filter on textEdit, you fail to capture any clicks.
	// The clicks go to the viewport. http://stackoverflow.com/a/31582977/10278
	textEdit->viewport()->installEventFilter(this);
}

bool TextHyperlinkEventFilter::eventFilter(QObject *target, QEvent *evt)
{
	if (target != scrollView)
		return false;

	if (evt->type() != QEvent::MouseButtonPress &&
	    evt->type() != QEvent::ToolTip)
		return false;

	// --------------------

	// Note: Qt knows that on Mac OSX, ctrl (and Control) are the command key.
	const bool isCtrlClick = evt->type() == QEvent::MouseButtonPress &&
				 static_cast<QMouseEvent *>(evt)->modifiers() & Qt::ControlModifier &&
				 static_cast<QMouseEvent *>(evt)->button() == Qt::LeftButton;

	const bool isTooltip = evt->type() == QEvent::ToolTip;

	QString urlUnderCursor;

	if (isCtrlClick || isTooltip) {
		QTextCursor cursor = isCtrlClick ?
					     textEdit->cursorForPosition(static_cast<QMouseEvent *>(evt)->pos()) :
					     textEdit->cursorForPosition(static_cast<QHelpEvent *>(evt)->pos());

		urlUnderCursor = tryToFormulateUrl(&cursor);
	}

	if (isCtrlClick) {
		handleUrlClick(urlUnderCursor);
	}

	if (isTooltip) {
		handleUrlTooltip(urlUnderCursor, static_cast<QHelpEvent *>(evt)->globalPos());
	}

	// 'return true' would mean that all event handling stops for this event.
	// 'return false' lets Qt continue propagating the event to the target.
	// Since our URL behavior is meant as 'additive' and not necessarily mutually
	// exclusive with any default behaviors, it seems ok to return false to
	// avoid unintentially hijacking any 'normal' event handling.
	return false;
}

void TextHyperlinkEventFilter::handleUrlClick(const QString &urlStr)
{
	if (!urlStr.isEmpty()) {
		QUrl url(urlStr, QUrl::StrictMode);
		QDesktopServices::openUrl(url);
	}
}

void TextHyperlinkEventFilter::handleUrlTooltip(const QString &urlStr, const QPoint &pos)
{
	if (urlStr.isEmpty()) {
		QToolTip::hideText();
	} else {
		// per Qt docs, QKeySequence::toString does localization "tr()" on strings like Ctrl.
		// Note: Qt knows that on Mac OSX, ctrl (and Control) are the command key.
		const QString ctrlKeyName = QKeySequence(Qt::CTRL).toString(QKeySequence::NativeText);
		// ctrlKeyName comes with a trailing '+', as in: 'Ctrl+'
		QToolTip::showText(pos, tr("%1click to visit %2").arg(ctrlKeyName).arg(urlStr));
	}
}

bool TextHyperlinkEventFilter::stringMeetsOurUrlRequirements(const QString &maybeUrlStr)
{
	QUrl url(maybeUrlStr, QUrl::StrictMode);
	return url.isValid() && (!url.scheme().isEmpty()) && ((!url.authority().isEmpty()) || (!url.path().isEmpty()));
}

QString TextHyperlinkEventFilter::tryToFormulateUrl(QTextCursor *cursor)
{
	// tryToFormulateUrl exists because WordUnderCursor will not
	// treat "http://m.abc.def" as a word.

	// tryToFormulateUrl invokes fromCursorTilWhitespace two times (once
	// with a forward moving cursor and once in the backwards direction) in
	// order to expand the selection to try to capture a complete string
	// like "http://m.abc.def"

	// loosely inspired by advice here: http://stackoverflow.com/q/19262064/10278

	cursor->select(QTextCursor::WordUnderCursor);
	QString maybeUrlStr = cursor->selectedText();

	const bool soFarSoGood = !maybeUrlStr.simplified().replace(" ", "").isEmpty();

	if (soFarSoGood && !stringMeetsOurUrlRequirements(maybeUrlStr)) {
		// If we don't yet have a full url, try to expand til we get one.  Note:
		// after requesting WordUnderCursor, empirically (all platforms, in
		// Qt5), the 'anchor' is just past the end of the word.

		QTextCursor cursor2(*cursor);
		QString left = fromCursorTilWhitespace(cursor, true /*searchBackwards*/);
		QString right = fromCursorTilWhitespace(&cursor2, false);
		maybeUrlStr = left + right;
	}

	return stringMeetsOurUrlRequirements(maybeUrlStr) ? maybeUrlStr : QString::null;
}

QString TextHyperlinkEventFilter::fromCursorTilWhitespace(QTextCursor *cursor, const bool searchBackwards)
{
	// fromCursorTilWhitespace calls cursor->movePosition repeatedly, while
	// preserving the original 'anchor' (qt terminology) of the cursor.
	// We widen the selection with 'movePosition' until hitting any whitespace.

	QString result;
	QString grownText;
	QString noSpaces;
	bool movedOk = false;
	int oldSize = -1;

	do {
		result = grownText; // this is a no-op on the first visit.

		if (searchBackwards) {
			movedOk = cursor->movePosition(QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
		} else {
			movedOk = cursor->movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
		}

		grownText = cursor->selectedText();
		if (grownText.size() == oldSize)
			movedOk = false;
		oldSize = grownText.size();
		noSpaces = grownText.simplified().replace(" ", "");
	} while (grownText == noSpaces && movedOk);

	// while growing the selection forwards, we have an extra step to do:
	if (!searchBackwards) {
		/*
		  The cursor keeps jumping to the start of the next word.
		  (for example) in the string "mn.abcd.edu is the spot" you land at
		  m,a,e,i (the 'i' in 'is). if we stop at e, then we only capture
		  "mn.abcd." for the url (wrong). So we have to go to 'i', to
		  capture "mn.abcd.edu " (with trailing space), and then clean it up.
		*/
		QStringList list = grownText.split(QRegExp("\\s"), QString::SkipEmptyParts);
		if (!list.isEmpty()) {
			result = list[0];
		}
	}

	return result;
}
