// /*
//  * GDB Debugger Support
//  *
//  * Copyright 1999-2001 John Birch <jbb@kdevelop.org>
//  * Copyright 2001 by Bernd Gehrmann <bernd@kdevelop.org>
//  * Copyright 2006 Vladimir Prus <ghost@cs.msu.su>
//  * Copyright 2007 Hamish Rodda <rodda@kde.org>
//  *
//  * This program is free software; you can redistribute it and/or modify
//  * it under the terms of the GNU General Public License as
//  * published by the Free Software Foundation; either version 2 of the
//  * License, or (at your option) any later version.
//  *
//  * This program is distributed in the hope that it will be useful,
//  * but WITHOUT ANY WARRANTY; without even the implied warranty of
//  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  * GNU General Public License for more details.
//  *
//  * You should have received a copy of the GNU General Public
//  * License along with this program; if not, write to the
//  * Free Software Foundation, Inc.,
//  * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//  */

#include "debuggerplugin.h"

#include <QDir>
#include <QToolTip>
#include <QByteArray>
#include <QTimer>
#include <QMenu>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QSignalMapper>
#include <QDBusServiceWatcher>

#include <QAction>
#include <kactioncollection.h>
#include <KLocalizedString>
#include <kmainwindow.h>
#include <kparts/part.h>
#include <kparts/mainwindow.h>
#include <kmessagebox.h>
#include <kpluginfactory.h>
#include <kaboutdata.h>
#include <KToolBar>
#include <KXmlGuiWindow>
#include <KXMLGUIFactory>

#include <sublime/view.h>

#include <interfaces/icore.h>
#include <interfaces/iuicontroller.h>
#include <interfaces/idocumentcontroller.h>
#include <interfaces/iprojectcontroller.h>
#include <interfaces/iruncontroller.h>
#include <interfaces/iproject.h>
#include <interfaces/context.h>
#include <interfaces/context.h>
#include <interfaces/contextmenuextension.h>
#include <language/interfaces/editorcontext.h>
#include <interfaces/idebugcontroller.h>
#include <interfaces/iplugincontroller.h>
#include <execute/iexecuteplugin.h>
#include <interfaces/launchconfigurationtype.h>

#include "disassemblewidget.h"
#ifdef KDEV_ENABLE_GDB_ATTACH_DIALOG
#include "processselection.h"
#endif
#include "memviewdlg.h"
#include "gdboutputwidget.h"
#include "gdbglobal.h"
#include "debugsession.h"
#include "selectcoredialog.h"

#include <iostream>
#include "gdblaunchconfig.h"
#include "debugjob.h"


namespace GDBDebugger
{

K_PLUGIN_FACTORY_WITH_JSON(CppDebuggerFactory, "kdevgdb.json", registerPlugin<CppDebuggerPlugin>(); )

template<class T>
class DebuggerToolFactory : public KDevelop::IToolViewFactory
{
public:
  DebuggerToolFactory(CppDebuggerPlugin* plugin, const QString &id, Qt::DockWidgetArea defaultArea)
  : m_plugin(plugin), m_id(id), m_defaultArea(defaultArea)
  {}

  virtual QWidget* create(QWidget *parent = 0) override
  {
    return new T(m_plugin, parent);
  }

  virtual QString id() const override
  {
    return m_id;
  }

  virtual Qt::DockWidgetArea defaultPosition() override
  {
    return m_defaultArea;
  }

  virtual void viewCreated(Sublime::View* view) override
  {
      if (view->widget()->metaObject()->indexOfSignal(QMetaObject::normalizedSignature("requestRaise()")) != -1)
          QObject::connect(view->widget(), SIGNAL(requestRaise()), view, SLOT(requestRaise()));
  }

  /* At present, some debugger widgets (e.g. breakpoint) contain actions so that shortcuts
     work, but they don't need any toolbar.  So, suppress toolbar action.  */
  virtual QList<QAction*> toolBarActions( QWidget* viewWidget ) const override
  {
      Q_UNUSED(viewWidget);
      return QList<QAction*>();
  }

private:
  CppDebuggerPlugin* m_plugin;
  QString m_id;
  Qt::DockWidgetArea m_defaultArea;
};

CppDebuggerPlugin::CppDebuggerPlugin( QObject *parent, const QVariantList & ) :
    KDevelop::IPlugin( "kdevgdb", parent ),
    m_config(KSharedConfig::openConfig(), "GDB Debugger")
{
    KDEV_USE_EXTENSION_INTERFACE( KDevelop::IStatus )

    core()->debugController()->initializeUi();

    setXMLFile("kdevgdbui.rc");

    disassemblefactory = new DebuggerToolFactory<DisassembleWidget>(
    this, "org.kdevelop.debugger.DisassemblerView", Qt::BottomDockWidgetArea);

    gdbfactory = new DebuggerToolFactory<GDBOutputWidget>(
    this, "org.kdevelop.debugger.ConsoleView",Qt::BottomDockWidgetArea);

    core()->uiController()->addToolView(
        i18n("Disassemble/Registers"),
        disassemblefactory);

    core()->uiController()->addToolView(
        i18n("GDB"),
        gdbfactory);

#ifndef WITH_OKTETA
    memoryviewerfactory = nullptr;
#else
    memoryviewerfactory = new DebuggerToolFactory<MemoryViewerWidget>(
    this, "org.kdevelop.debugger.MemoryView", Qt::BottomDockWidgetArea);
    core()->uiController()->addToolView(
        i18n("Memory"),
        memoryviewerfactory);
#endif

    setupActions();

    setupDBus();

    QList<IPlugin*> plugins = KDevelop::ICore::self()->pluginController()->allPluginsForExtension("org.kdevelop.IExecutePlugin");
    foreach(IPlugin* plugin, plugins) {
        IExecutePlugin* iface = plugin->extension<IExecutePlugin>();
        Q_ASSERT(iface);
        KDevelop::LaunchConfigurationType* type = core()->runController()->launchConfigurationTypeForId( iface->nativeAppConfigTypeId() );
        Q_ASSERT(type);
        type->addLauncher( new GdbLauncher( this, iface ) );
    }
}

void CppDebuggerPlugin::unload()
{
    core()->uiController()->removeToolView(disassemblefactory);
    core()->uiController()->removeToolView(gdbfactory);
    core()->uiController()->removeToolView(memoryviewerfactory);
}

void CppDebuggerPlugin::setupActions()
{
    KActionCollection* ac = actionCollection();

    QAction * action = new QAction(QIcon::fromTheme("core"), i18n("Examine Core File..."), this);
    action->setToolTip( i18n("Examine core file") );
    action->setWhatsThis( i18n("<b>Examine core file</b><p>"
                               "This loads a core file, which is typically created "
                               "after the application has crashed, e.g. with a "
                               "segmentation fault. The core file contains an "
                               "image of the program memory at the time it crashed, "
                               "allowing you to do a post-mortem analysis.</p>") );
    connect(action, &QAction::triggered, this, &CppDebuggerPlugin::slotExamineCore);
    ac->addAction("debug_core", action);

    #ifdef KDEV_ENABLE_GDB_ATTACH_DIALOG
    action = new QAction(QIcon::fromTheme("connect_creating"), i18n("Attach to Process"), this);
    action->setToolTip( i18n("Attach to process...") );
    action->setWhatsThis(i18n("<b>Attach to process</b><p>Attaches the debugger to a running process.</p>"));
    connect(action, SIGNAL(triggered(bool)), this, SLOT(slotAttachProcess()));
    ac->addAction("debug_attach", action);
    #endif
}

void CppDebuggerPlugin::setupDBus()
{
    m_drkonqiMap = new QSignalMapper(this);
    connect(m_drkonqiMap, static_cast<void(QSignalMapper::*)(QObject*)>(&QSignalMapper::mapped), this, &CppDebuggerPlugin::slotDebugExternalProcess);

    QDBusConnectionInterface* dbusInterface = QDBusConnection::sessionBus().interface();
    foreach (const QString& service, dbusInterface->registeredServiceNames().value())
        slotDBusServiceRegistered(service);

    QDBusServiceWatcher* watcher = new QDBusServiceWatcher(this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered,
            this, &CppDebuggerPlugin::slotDBusServiceRegistered);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &CppDebuggerPlugin::slotDBusServiceUnregistered);
}

void CppDebuggerPlugin::slotDBusServiceRegistered( const QString& service )
{
    if (service.startsWith("org.kde.drkonqi")) {
        // New registration
        QDBusInterface* drkonqiInterface = new QDBusInterface(service, "/krashinfo", QString(), QDBusConnection::sessionBus(), this);
        m_drkonqis.insert(service, drkonqiInterface);

        connect(drkonqiInterface, SIGNAL(acceptDebuggingApplication()), m_drkonqiMap, SLOT(map()));
        m_drkonqiMap->setMapping(drkonqiInterface, drkonqiInterface);

        drkonqiInterface->call("registerDebuggingApplication", i18n("KDevelop"));
    }
}

void CppDebuggerPlugin::slotDBusServiceUnregistered( const QString& service )
{
    if (service.startsWith("org.kde.drkonqi")) {
        // Deregistration
        if (m_drkonqis.contains(service))
            delete m_drkonqis.take(service);
    }
}

void CppDebuggerPlugin::slotDebugExternalProcess(QObject* interface)
{
    QDBusReply<int> reply = static_cast<QDBusInterface*>(interface)->call("pid");

    if (reply.isValid()) {
        attachProcess(reply.value());
        QTimer::singleShot(500, this, SLOT(slotCloseDrKonqi()));

        m_drkonqi = m_drkonqis.key(static_cast<QDBusInterface*>(interface));
    }

    KDevelop::ICore::self()->uiController()->activeMainWindow()->raise();
}

void CppDebuggerPlugin::slotCloseDrKonqi()
{
    if (!m_drkonqi.isEmpty()) {
        QDBusInterface drkonqiInterface(m_drkonqi, "/MainApplication", "org.kde.KApplication");
        drkonqiInterface.call("quit");
        m_drkonqi.clear();
    }
}

CppDebuggerPlugin::~CppDebuggerPlugin()
{
}


void CppDebuggerPlugin::initializeGuiState()
{
}

KDevelop::ContextMenuExtension CppDebuggerPlugin::contextMenuExtension( KDevelop::Context* context )
{
    KDevelop::ContextMenuExtension menuExt = KDevelop::IPlugin::contextMenuExtension( context );

    if( context->type() != KDevelop::Context::EditorContext )
        return menuExt;

    KDevelop::EditorContext *econtext = dynamic_cast<KDevelop::EditorContext*>(context);
    if (!econtext)
        return menuExt;

    m_contextIdent = econtext->currentWord();

    if (!m_contextIdent.isEmpty())
    {
        // PORTING TODO
        //QString squeezed = KStringHandler::csqueeze(m_contextIdent, 30);
        QAction* action = new QAction( i18n("Evaluate: %1", m_contextIdent), this);
        connect(action, &QAction::triggered, this, &CppDebuggerPlugin::contextEvaluate);
        action->setWhatsThis(i18n("<b>Evaluate expression</b><p>Shows the value of the expression under the cursor.</p>"));
        menuExt.addAction( KDevelop::ContextMenuExtension::DebugGroup, action);

        action = new QAction( i18n("Watch: %1", m_contextIdent), this);
        connect(action, &QAction::triggered, this, &CppDebuggerPlugin::contextWatch);
        action->setWhatsThis(i18n("<b>Watch expression</b><p>Adds an expression under the cursor to the Variables/Watch list.</p>"));
        menuExt.addAction( KDevelop::ContextMenuExtension::DebugGroup, action);
    }

    return menuExt;
}

void CppDebuggerPlugin::contextWatch()
{
    emit addWatchVariable(m_contextIdent);
}

void CppDebuggerPlugin::contextEvaluate()
{
    emit evaluateExpression(m_contextIdent);
}

DebugSession* CppDebuggerPlugin::createSession()
{
    DebugSession *session = new DebugSession();
    KDevelop::ICore::self()->debugController()->addSession(session);
    connect(session, &DebugSession::showMessage, this, &CppDebuggerPlugin::controllerMessage);
    connect(session, &DebugSession::reset, this, &CppDebuggerPlugin::reset);
    connect(session, &DebugSession::finished, this, &CppDebuggerPlugin::slotFinished);
    connect(session, &DebugSession::raiseGdbConsoleViews, this, &CppDebuggerPlugin::raiseGdbConsoleViews);
    return session;
}

void CppDebuggerPlugin::slotExamineCore()
{
    emit showMessage(this, i18n("Choose a core file to examine..."), 1000);

    SelectCoreDialog dlg(KDevelop::ICore::self()->uiController()->activeMainWindow());
    if (dlg.exec() == QDialog::Rejected) {
        return;
    }

    emit showMessage(this, i18n("Examining core file %1", dlg.core().toLocalFile()), 1000);

    DebugSession* session = createSession();
    session->examineCoreFile(dlg.binary(), dlg.core());

    KillSessionJob *job = new KillSessionJob(session);
    job->setObjectName(i18n("Debug core file"));
    core()->runController()->registerJob(job);
    job->start();
}

#ifdef KDEV_ENABLE_GDB_ATTACH_DIALOG
void CppDebuggerPlugin::slotAttachProcess()
{
    emit showMessage(this, i18n("Choose a process to attach to..."), 1000);

    ProcessSelectionDialog dlg;
    if (!dlg.exec() || !dlg.pidSelected())
        return;

    int pid = dlg.pidSelected();
    if(QApplication::applicationPid()==pid)
        KMessageBox::error(KDevelop::ICore::self()->uiController()->activeMainWindow(),
                            i18n("Not attaching to process %1: cannot attach the debugger to itself.", pid));
    else
        attachProcess(pid);
}
#endif

void CppDebuggerPlugin::attachProcess(int pid)
{
    emit showMessage(this, i18n("Attaching to process %1", pid), 1000);

    DebugSession* session = createSession();
    session->attachToProcess(pid);

    KillSessionJob *job = new KillSessionJob(session);
    job->setObjectName(i18n("Debug process %1", pid));
    core()->runController()->registerJob(job);
    job->start();
}

// Used to disable breakpoint actions when non-text document selected

// save/restore partial project session

KConfigGroup CppDebuggerPlugin::config() const
{
    return m_config;
}

QString CppDebuggerPlugin::statusName() const
{
    return i18n("Debugger");
}

void CppDebuggerPlugin::slotFinished()
{
    /* TODO: is this required?
    Q_ASSERT(dynamic_cast<DebugSession*>(sender()));
    DebugSession* session = static_cast<DebugSession*>(sender());
    */
}

void CppDebuggerPlugin::controllerMessage( const QString& msg, int timeout )
{
    emit showMessage(this, msg, timeout);
}

}

#include "debuggerplugin.moc"
