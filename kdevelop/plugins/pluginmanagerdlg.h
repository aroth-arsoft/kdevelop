/***************************************************************************
                          pluginmanagerdlg.h  
                             -------------------
    begin                : Thu Sep 22 1999
    copyright            : (C) 1999 by Sandy Meier
    email                : smeier@rz.uni-potsdam.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   * 
 *                                                                         *
 ***************************************************************************/



#ifndef PLUGINMANAGERDLG_H
#define PLUGINMANAGERDLG_H

#include <qwidget.h>
#include <qdialog.h>
#include <qmultilinedit.h>
#include <qlabel.h>
#include <qpushbutton.h>
#include <qlistview.h>
#include <qlist.h>

#include "../structdef.h"

class PluginInfo;
class KDevPlugin;

/**
  *@author Sandy Meier
  */

class PluginManagerDlg : public QDialog  {
   Q_OBJECT
public: 
	PluginManagerDlg(QWidget *parent=0, const char *name=0,TImportantPtrInfo* info=0);
	~PluginManagerDlg();
	void searchPlugins();
	void initDialog();

protected:
    QPushButton* ok_button;
    QPushButton* cancel_button;
    QListView* plugin_listbox;
    QLabel* author_label;
    QLabel* version_label;
    QLabel* copyright_label;
    QLabel* homepage_label;
    QLabel* email_label;
    QLabel* size_label;
    QMultiLineEdit* desc_multilineedit;
    TImportantPtrInfo* ptrinfo;
    QList<PluginInfo>* plugin_infos;
    
    protected slots:
       void slotSelectionChanged ( QListViewItem * item);
       
};

class PluginInfo {
 public:
    QString path;
    KDevPlugin* plugin;
};

#endif
