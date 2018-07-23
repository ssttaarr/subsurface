// SPDX-License-Identifier: GPL-2.0
#ifndef PREFSMACROS_H
#define PREFSMACROS_H

#include "core/qthelper.h"
#include "subsurface-string.h"

#define SB(V, B) s.setValue(V, (int)(B->isChecked() ? 1 : 0))

#define GET_UNIT(name, field, f, t)                                 \
	{															\
	v = s.value(QString(name));                                 \
	if (v.isValid())                                            \
		prefs.units.field = (v.toInt() == (t)) ? (t) : (f); \
	else                                                        \
		prefs.units.field = default_prefs.units.field;			\
	}

#define GET_UNIT3(name, field, f, l, type)                          \
	{															\
	v = s.value(QString(name));                                 \
	if (v.isValid() && v.toInt() >= (f) && v.toInt() <= (l))    \
		prefs.units.field = (type)v.toInt();                \
	else                                                        \
		prefs.units.field = default_prefs.units.field;			\
	}

#define GET_UNIT_BOOL(name, field)                      \
	{												\
	v = s.value(QString(name));                     \
	if (v.isValid())                                \
		prefs.units.field = v.toBool();         \
	else                                            \
		prefs.units.field = default_prefs.units.field;	\
	}

#define GET_BOOL(name, field)                           \
	{												\
	v = s.value(QString(name));                     \
	if (v.isValid())                                \
		prefs.field = v.toBool();               \
	else                                            \
		prefs.field = default_prefs.field;			\
	}

#define GET_DOUBLE(name, field)             \
	{									\
	v = s.value(QString(name));         \
	if (v.isValid())                    \
		prefs.field = v.toDouble(); \
	else                                \
		prefs.field = default_prefs.field;	\
	}

#define GET_INT(name, field)             \
	{								\
	v = s.value(QString(name));      \
	if (v.isValid())                 \
		prefs.field = v.toInt(); \
	else                             \
		prefs.field = default_prefs.field;	\
	}

#define GET_ENUM(name, type, field)                 \
	{											\
	v = s.value(QString(name));                 \
	if (v.isValid())                            \
		prefs.field = (enum type)v.toInt(); \
	else                                        \
		prefs.field = default_prefs.field;		\
	}

#define GET_INT_DEF(name, field, defval)                                 \
	{																\
	v = s.value(QString(name));                                      \
	if (v.isValid())                                                 \
		prefs.field = v.toInt();                                 \
	else                                                             \
		prefs.field = defval;								\
	}

#define GET_TXT(name, field)                                   \
	{													\
	v = s.value(QString(name));                            \
	if (v.isValid())                                       \
		prefs.field = copy_qstring(v.toString());      \
	else                                                   \
		prefs.field = copy_string(default_prefs.field);	\
	}

#define SAVE_OR_REMOVE_SPECIAL(_setting, _default, _compare, _value)     \
	{															\
	if (_compare != _default)                                        \
		s.setValue(_setting, _value);                            \
	else                                                             \
		s.remove(_setting);											\
	}

#define SAVE_OR_REMOVE(_setting, _default, _value)                       \
	{													\
	if (_value != _default)                                          \
		s.setValue(_setting, _value);                            \
	else                                                             \
		s.remove(_setting);			\
	}

#endif // PREFSMACROS_H

