#ifndef CANTORDUST_IDA_PCH_H
#define CANTORDUST_IDA_PCH_H

// Shared implementation includes for the native IDA/Qt plugin. This header
// works as a normal include with PCH disabled. Avoid IDA SDK headers here:
// they stay explicit at the points of use. Qt headers depend on the target's
// IDA-compatible namespace and must not be shared with portable mock tests.
#include "core_pch.h"

#include <QCursor>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpressionValidator>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>
#include <QtWidgets>

#endif // CANTORDUST_IDA_PCH_H
