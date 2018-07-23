// SPDX-License-Identifier: GPL-2.0
#ifndef QT_GUI_H
#define QT_GUI_H

void init_qt_late();
void init_ui();

void run_ui();
void register_qml_types();
void exit_ui();
void set_non_bt_addresses();

#if defined(SUBSURFACE_MOBILE)
#include <QQuickWindow>
#endif

#endif // QT_GUI_H
