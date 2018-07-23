// SPDX-License-Identifier: GPL-2.0
#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include <QObject>

#include "isocialnetworkintegration.h"

class PluginManager {
public:
	static PluginManager& instance();
	void loadPlugins();
private:
	PluginManager();
	PluginManager(const PluginManager&);
	PluginManager& operator=(const PluginManager&);
};

#endif
