#pragma once
#include <QtCore/qglobal.h>

#if defined(GEOMETRY_LIBRARY)
#  define GEOMETRY_EXPORT Q_DECL_EXPORT
#else
#  define GEOMETRY_EXPORT Q_DECL_IMPORT
#endif
