//Added by qt3to4:
#include <QMouseEvent>
#include <QPixmap>
/***************************************************************************
    begin                : Thu Dec 23 1999
    copyright            : (C) 1999 by John Birch
    email                : jbb@kdevelop.org
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *q
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef QT_MAC
#ifndef _DBGTOOLBAR_H_
#define _DBGTOOLBAR_H_

class KWindowSystem;

#include <QWidget>

#include <kwindowsystem.h>         // needed for WId :(
#include <KSystemTrayIcon>
#include <kvbox.h>

namespace GDBDebugger
{

class DbgButton;
class DbgToolBar;
class DebuggerPart;

class DbgDocker : public KSystemTrayIcon
{
    Q_OBJECT

public:
    DbgDocker(QWidget *parent, DbgToolBar *toolBar, const QPixmap &pixmap);
    virtual ~DbgDocker()  {};
    virtual void mousePressEvent(QMouseEvent *e);

Q_SIGNALS:
    void clicked();

private:
    DbgToolBar* toolBar_;
};


class DbgToolBar : public QWidget
{
    Q_OBJECT

public:
    DbgToolBar(DebuggerPart *part, QWidget* parent);
    virtual ~DbgToolBar();

private Q_SLOTS:
    void slotDbgStatus(const QString&, int);
    void slotDock();
    void slotUndock();
    void slotIconifyAndDock();
    void slotActivateAndUndock();

    void slotKdevFocus();
    void slotPrevFocus();

private:
    void setAppIndicator(bool appIndicator);

    DebuggerPart*   part_;
    WId             activeWindow_;
    KWindowSystem*     winModule_;
    DbgButton*      bKDevFocus_;
    DbgButton*      bPrevFocus_;
    bool            appIsActive_;
    bool            docked_;
    DbgDocker*      docker_;
    KSystemTrayIcon*    dockWindow_;
};

}

#endif
#endif