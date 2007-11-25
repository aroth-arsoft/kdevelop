/***************************************************************************
 *   Copyright 2001 Bernd Gehrmann <bernd@kdevelop.org>                    *
 *   Copyright 2004-2005 Sascha Cunz <sascha@kdevelop.org>                 *
 *   Copyright 2005 Ian Reinhart Geiser <ian@geiseri.com>                  *
 *   Copyright 2007 Alexander Dymo <adymo@kdevelop.org>                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "appwizardplugin.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextCodec>
#include <QTextStream>

#include <ktar.h>
#include <kzip.h>
#include <kdebug.h>
#include <klocale.h>
#include <kaction.h>
#include <ktempdir.h>
#include <kmessagebox.h>
#include <kstandarddirs.h>
#include <kmacroexpander.h>
#include <kpluginfactory.h>
#include <kpluginloader.h>
#include <kactioncollection.h>
#include <kio/netaccess.h>

#include <icore.h>
#include <iprojectcontroller.h>

#include "appwizarddialog.h"
#include "projectselectionpage.h"
#include "projecttemplatesmodel.h"
#include "importproject.h"

K_PLUGIN_FACTORY(AppWizardFactory,
    registerPlugin<AppWizardPlugin>();
    KComponentData compData = componentData();
    KStandardDirs *dirs = compData.dirs();
    dirs->addResourceType("apptemplates", "data", "kdevappwizard/templates/");
    dirs->addResourceType("apptemplate_descriptions","data", "kdevappwizard/template_descriptions/");
    dirs->addResourceType("appimports", "data", "kdevappwizard/imports/");
    dirs->addResourceType("appimportfiles", "data", "kdevappwizard/importfiles/");
    setComponentData(compData);
)
K_EXPORT_PLUGIN(AppWizardFactory("kdevappwizard"))

AppWizardPlugin::AppWizardPlugin(QObject *parent, const QVariantList &)
    :KDevelop::IPlugin(AppWizardFactory::componentData(), parent)
{
    setXMLFile("kdevappwizard.rc");

    QAction *action = actionCollection()->addAction("project_new");
    action->setIcon(KIcon("window-new"));
    action->setText(i18n("&New Project..."));
    connect(action, SIGNAL(triggered(bool)), this, SLOT(slotNewProject()));
    action->setToolTip( i18n("Generate a new project from a template") );
    action->setWhatsThis( i18n("<b>New project</b><p>"
                               "This starts KDevelop's application wizard. "
                               "It helps you to generate a skeleton for your "
                               "application from a set of templates.</p>") );

    action = actionCollection()->addAction( "project_import" );
    action->setIcon(KIcon("project-import"));
    action->setText(i18n( "&Import Existing Project..." ));
    connect( action, SIGNAL( triggered( bool ) ), SLOT( slotImportProject() ) );
    action->setToolTip( i18n( "Import existing project" ) );

    m_templatesModel = new ProjectTemplatesModel(this);
}

AppWizardPlugin::~AppWizardPlugin()
{
}

void AppWizardPlugin::slotNewProject()
{
    m_templatesModel->refresh();
    AppWizardDialog dlg;

    ProjectSelectionPage *selectionPage = new ProjectSelectionPage(m_templatesModel, &dlg);
    dlg.addPage(selectionPage, i18nc("Page for general configuration options", "General"));

    if (dlg.exec() == QDialog::Accepted)
    {
        QString project = createProject(selectionPage);
        if (!project.isEmpty())
            core()->projectController()->openProject(KUrl::fromPath(project));
    }
}

void AppWizardPlugin::slotImportProject()
{
    ImportProject import(this, QApplication::activeWindow());
    import.exec();
}

QString AppWizardPlugin::createProject(ProjectSelectionPage *selectionPage)
{
    QFileInfo templateInfo(selectionPage->selectedTemplate());
    if (!templateInfo.exists())
        return "";

    QString templateName = templateInfo.baseName();
    kDebug(9010) << "creating project for template:" << templateName;

    QString templateArchive = componentData().dirs()->findResource("apptemplates", templateName + ".zip");
    if( templateArchive.isEmpty() )
    {
        templateArchive = componentData().dirs()->findResource("apptemplates", templateName + ".tar.bz2");
    }

    if (templateArchive.isEmpty())
        return "";

    //prepare variable substitution hash
    m_variables.clear();
    m_variables["APPNAME"] = selectionPage->appName();
    m_variables["APPNAMEUC"] = selectionPage->appName().toUpper();
    m_variables["APPNAMELC"] = selectionPage->appName().toLower();

    QString dest = selectionPage->location();
	KArchive* arch = 0;
	if( templateArchive.endsWith(".zip") )
	{
		arch = new KZip(templateArchive);
	}
	else
	{
	    arch = new KTar(templateArchive, "application/x-bzip");
	}
	if (arch->open(QIODevice::ReadOnly))
		unpackArchive(arch->directory(), dest);
	else
		kDebug(9010) << "failed to open template archive";

    return QDir::cleanPath(dest + '/' + selectionPage->appName().toLower() + ".kdev4");
}

void AppWizardPlugin::unpackArchive(const KArchiveDirectory *dir, const QString &dest)
{
    KIO::NetAccess::mkdir(dest, 0);
    kDebug(9010) << "unpacking dir:" << dir->name() << "to" << dest;
    QStringList entries = dir->entries();
    kDebug(9010) << "entries:" << entries.join(",");

    KTempDir tdir;

    foreach (QString entry, entries)
    {
        if (entry.endsWith(".kdevtemplate"))
            continue;
        if (dir->entry(entry)->isDirectory())
        {
            const KArchiveDirectory *file = (KArchiveDirectory *)dir->entry(entry);
            unpackArchive(file, dest + '/' + file->name());
        }
        else if (dir->entry(entry)->isFile())
        {
            const KArchiveFile *file = (KArchiveFile *)dir->entry(entry);
            file->copyTo(tdir.name());
            QString destName = dest + '/' + file->name();
            if (!copyFile(QDir::cleanPath(tdir.name()+'/'+file->name()),
                    KMacroExpander::expandMacros(destName, m_variables)))
            {
                KMessageBox::sorry(0, i18n("The file %1 cannot be created.", dest));
                return;
            }
        }
    }
    tdir.unlink();
}

bool AppWizardPlugin::copyFile(const QString &source, const QString &dest)
{
    kDebug(9010) << "copy:" << source << "to" << dest;
    QFile inputFile(source);
    QFile outputFile(dest);

    if (inputFile.open(QFile::ReadOnly) && outputFile.open(QFile::WriteOnly))
    {
        QTextStream input(&inputFile);
        input.setCodec(QTextCodec::codecForName("UTF-8"));
        QTextStream output(&outputFile);
        output.setCodec(QTextCodec::codecForName("UTF-8"));
        while(!input.atEnd())
        {
            QString line = input.readLine();
            output << KMacroExpander::expandMacros(line, m_variables) << "\n";
        }
        // Preserve file mode...
        struct stat fmode;
        ::fstat(inputFile.handle(), &fmode);
        ::fchmod(outputFile.handle(), fmode.st_mode);
        return true;
    }
    else
    {
        inputFile.close();
        outputFile.close();
        return false;
    }
}

#include "appwizardplugin.moc"
