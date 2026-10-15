#ifndef CANTORDUST_IDA_PCH_H
#define CANTORDUST_IDA_PCH_H

// Separate PCH for the IDA/Qt plugin. Avoid IDA SDK headers here: the SDK
// owns its compiler configuration and its headers should stay explicit at
// their points of use. The Qt headers use the target's IDA-compatible Qt
// namespace/defines and must not be shared with the portable core or tests.
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
