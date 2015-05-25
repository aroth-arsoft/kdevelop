/*
 * GDB Debugger Support
 *
 * Copyright 1999-2001 John Birch <jbb@kdevelop.org>
 * Copyright 2001 by Bernd Gehrmann <bernd@kdevelop.org>
 * Copyright 2006 Vladimir Prus <ghost@cs.msu.su>
 * Copyright 2007 Hamish Rodda <rodda@kde.org>
 * Copyright 2009 Niko Sams <niko.sams@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "debugsession.h"

#include <typeinfo>

#include <QtCore/QFileInfo>
#include <QApplication>
#include <QRegExp>
#include <QStandardPaths>

#include <KMessageBox>
#include <KLocalizedString>
#include <KToolBar>
#include <KParts/MainWindow>
#include <KSharedConfig>
#include <KShell>
#include <QUrl>
#include <QDir>

#include <interfaces/idocument.h>
#include <interfaces/icore.h>
#include <interfaces/iuicontroller.h>
#include <interfaces/idocumentcontroller.h>
#include <util/processlinemaker.h>
#include <util/environmentgrouplist.h>
#include <execute/iexecuteplugin.h>
#include <interfaces/ilaunchconfiguration.h>
#include <interfaces/iplugincontroller.h>
#include <interfaces/idebugcontroller.h>
#include <debugger/breakpoint/breakpointmodel.h>

#include "breakpointcontroller.h"
#include "variablecontroller.h"
#include "gdb.h"
#include "gdbcommandqueue.h"
#include "stty.h"
#include "gdbframestackmodel.h"
#include "debug.h"

using namespace KDevelop;

namespace GDBDebugger {

DebugSession::DebugSession()
    : m_breakpointController(nullptr)
    , m_variableController(nullptr)
    , m_frameStackModel(nullptr)
    , m_sessionState(NotStartedState)
    , m_config(KSharedConfig::openConfig(), "GDB Debugger")
    , m_testing(false)
    , commandQueue_(new CommandQueue)
    , m_tty(0)
    , state_(s_dbgNotStarted | s_appNotStarted)
    , state_reload_needed(false)
    , stateReloadInProgress_(false)
{
    configure();

    m_breakpointController = new BreakpointController(this);
    m_variableController = new VariableController(this);
    m_frameStackModel = new GdbFrameStackModel(this);

    m_procLineMaker = new KDevelop::ProcessLineMaker(this);

    connect(m_procLineMaker, &ProcessLineMaker::receivedStdoutLines,
            this, &DebugSession::applicationStandardOutputLines);
    connect(m_procLineMaker, &ProcessLineMaker::receivedStderrLines,
            this, &DebugSession::applicationStandardErrorLines);
    setupController();
}

// Deleting the session involves shutting down gdb nicely.
// When were attached to a process, we must first detach so that the process
// can continue running as it was before being attached. gdb is quite slow to
// detach from a process, so we must process events within here to get a "clean"
// shutdown.
DebugSession::~DebugSession()
{
    qCDebug(DEBUGGERGDB);

    if (!stateIsOn(s_dbgNotStarted)) {
        stopDebugger();
        // This currently isn't working, so comment out until it can be resolved - at the moment it just causes a delay on stopping kdevelop
        //m_process->waitForFinished();
    }

    delete commandQueue_;
}

void DebugSession::setTesting(bool testing)
{
    m_testing = testing;
}

KDevelop::IDebugSession::DebuggerState DebugSession::state() const {
    return m_sessionState;
}

BreakpointController* DebugSession::breakpointController() const
{
    return m_breakpointController;
}

IVariableController* DebugSession::variableController() const
{
    return m_variableController;
}

IFrameStackModel* DebugSession::frameStackModel() const
{
    return m_frameStackModel;
}

#define ENUM_NAME(o,e,v) (o::staticMetaObject.enumerator(o::staticMetaObject.indexOfEnumerator(#e)).valueToKey((v)))
void DebugSession::setSessionState(DebuggerState state)
{
    qCDebug(DEBUGGERGDB) << "STATE CHANGED" << this << state << ENUM_NAME(IDebugSession, DebuggerState, state);
    if (state != m_sessionState) {
        m_sessionState = state;
        emit stateChanged(state);
    }
}

void DebugSession::setupController()
{
    // controller -> procLineMaker
    connect(this, &DebugSession::ttyStdout,
            m_procLineMaker, &ProcessLineMaker::slotReceivedStdout);
    connect(this, &DebugSession::ttyStderr,
            m_procLineMaker, &ProcessLineMaker::slotReceivedStderr);

//     connect(statusBarIndicator, SIGNAL(doubleClicked()),
//             controller, SLOT(explainDebuggerStatus()));

    // TODO: reimplement / re-enable
    //connect(this, SIGNAL(addWatchVariable(QString)), controller->variables(), SLOT(slotAddWatchVariable(QString)));
    //connect(this, SIGNAL(evaluateExpression(QString)), controller->variables(), SLOT(slotEvaluateExpression(QString)));
}

void DebugSession::_gdbStateChanged(DBGStateFlags oldState, DBGStateFlags newState)
{
    QString message;

    DebuggerState oldSessionState = state();
    DebuggerState newSessionState = oldSessionState;
    DBGStateFlags changedState = oldState ^ newState;

    if (newState & s_dbgNotStarted) {
        if (changedState & s_dbgNotStarted) {
            message = i18n("Debugger stopped");
            emit finished();
        }
        if (oldSessionState != NotStartedState) {
            newSessionState = EndedState;
        }
    } else {
        if (newState & s_appNotStarted) {
            if (oldSessionState == NotStartedState || oldSessionState == StartingState) {
                newSessionState = StartingState;
            } else {
                newSessionState = StoppedState;
            }
        } else if (newState & s_programExited) {
            if (changedState & s_programExited) {
                message = i18n("Process exited");
            }
            newSessionState = StoppedState;
        } else if (newState & s_appRunning) {
            if (changedState & s_appRunning) {
                message = i18n("Application is running");
            }
            newSessionState = ActiveState;
        } else {
            if (changedState & s_appRunning) {
                message = i18n("Application is paused");
            }
            newSessionState = PausedState;
        }
    }

    // And now? :-)
    qCDebug(DEBUGGERGDB) << "state: " << newState << message;

    if (!message.isEmpty())
        emit showMessage(message, 3000);

    emit gdbStateChanged(oldState, newState);

    // must be last, since it can lead to deletion of the DebugSession
    if (newSessionState != oldSessionState) {
        setSessionState(newSessionState);
    }
}

void DebugSession::examineCoreFile(const QUrl& debugee, const QUrl& coreFile)
{
    if (stateIsOn(s_dbgNotStarted))
      startDebugger(0);

    // TODO support non-local URLs
    queueCmd(new GDBCommand(GDBMI::FileExecAndSymbols, debugee.toLocalFile()));
    queueCmd(new GDBCommand(GDBMI::NonMI, "core " + coreFile.toLocalFile(), this, &DebugSession::handleCoreFile, CmdHandlesError));

    raiseEvent(connected_to_program);
    raiseEvent(program_state_changed);
}

void DebugSession::handleCoreFile(const GDBMI::ResultRecord& r)
{
    if (r.reason != "error") {
        setStateOn(s_programExited|s_core);
    } else {
        KMessageBox::information(
            qApp->activeWindow(),
            i18n("<b>Failed to load core file</b>"
                "<p>Debugger reported the following error:"
                "<p><tt>%1", r["msg"].literal()),
            i18n("Debugger error"));

        // How should we proceed at this point? Stop the debugger?
    }
}

void DebugSession::attachToProcess(int pid)
{
    qCDebug(DEBUGGERGDB) << pid;

    if (stateIsOn(s_dbgNotStarted))
      startDebugger(0);

    setStateOn(s_attached);

    //set current state to running, after attaching we will get *stopped response
    setStateOn(s_appRunning);

    // Currently, we always start debugger with a name of binary,
    // we might be connecting to a different binary completely,
    // so cancel all symbol tables gdb has.
    // We can't omit application name from gdb invocation
    // because for libtool binaries, we have no way to guess
    // real binary name.
    queueCmd(new GDBCommand(GDBMI::FileExecAndSymbols));

    queueCmd(new GDBCommand(GDBMI::TargetAttach, QString::number(pid), this, &DebugSession::handleTargetAttach, CmdHandlesError));

    queueCmd(new SentinelCommand(breakpointController(), &BreakpointController::initSendBreakpoints));

    raiseEvent(connected_to_program);

    emit raiseFramestackViews();
}

void DebugSession::run()
{
    if (stateIsOn(s_appNotStarted|s_dbgNotStarted|s_shuttingDown))
        return;

    queueCmd(new GDBCommand(GDBMI::ExecContinue, QString(), CmdMaybeStartsRunning));
}

void DebugSession::stepOut()
{
    if (stateIsOn(s_appNotStarted|s_shuttingDown))
        return;

    queueCmd(new GDBCommand(GDBMI::ExecFinish, QString(), CmdMaybeStartsRunning | CmdTemporaryRun));
}

void DebugSession::restartDebugger()
{
    // We implement restart as kill + slotRun, as opposed as plain "run"
    // command because kill + slotRun allows any special logic in slotRun
    // to apply for restart.
    //
    // That includes:
    // - checking for out-of-date project
    // - special setup for remote debugging.
    //
    // Had we used plain 'run' command, restart for remote debugging simply
    // would not work.
    slotKill();
    run();
}

void DebugSession::stopDebugger()
{
    commandQueue_->clear();

    qCDebug(DEBUGGERGDB) << "DebugSession::slotStopDebugger() called";
    if (stateIsOn(s_shuttingDown) || !m_gdb)
        return;

    setStateOn(s_shuttingDown);
    qCDebug(DEBUGGERGDB) << "DebugSession::slotStopDebugger() executing";

    // Get gdb's attention if it's busy. We need gdb to be at the
    // command line so we can stop it.
    if (!m_gdb.data()->isReady())
    {
        qCDebug(DEBUGGERGDB) << "gdb busy on shutdown - interruping";
        m_gdb.data()->interrupt();
    }

    // If the app is attached then we release it here. This doesn't stop
    // the app running.
    if (stateIsOn(s_attached))
    {
        queueCmd(new GDBCommand(GDBMI::TargetDetach));
        emit gdbUserCommandStdout("(gdb) detach\n");
    }

    // Now try to stop gdb running.
    queueCmd(new GDBCommand(GDBMI::GdbExit));
    emit gdbUserCommandStdout("(gdb) quit");

    // We cannot wait forever, kill gdb after 5 seconds if it's not yet quit
    QTimer::singleShot(5000, this, SLOT(slotKillGdb()));

    emit reset();
}

// Pausing an app removes any pending run commands so that the app doesn't
// start again. If we want to be silent then we remove any pending info
// commands as well.
void DebugSession::interruptDebugger()
{
    Q_ASSERT(m_gdb);

    // Explicitly send the interrupt in case something went wrong with the usual
    // ensureGdbListening logic.
    m_gdb->interrupt();
    queueCmd(new GDBCommand(GDBMI::ExecInterrupt, QString(), CmdInterrupt));
}

void DebugSession::runToCursor()
{
    if (KDevelop::IDocument* doc = KDevelop::ICore::self()->documentController()->activeDocument()) {
        KTextEditor::Cursor cursor = doc->cursorPosition();
        if (cursor.isValid())
            runUntil(doc->url(), cursor.line() + 1);
    }
}

void DebugSession::jumpToCursor()
{
    if (KDevelop::IDocument* doc = KDevelop::ICore::self()->documentController()->activeDocument()) {
        KTextEditor::Cursor cursor = doc->cursorPosition();
        if (cursor.isValid())
            jumpTo(doc->url(), cursor.line() + 1);
    }
}

void DebugSession::stepOver()
{
    if (stateIsOn(s_appNotStarted|s_shuttingDown))
        return;

    queueCmd(new GDBCommand(GDBMI::ExecNext, QString(), CmdMaybeStartsRunning | CmdTemporaryRun));
}

void DebugSession::stepOverInstruction()
{
    if (stateIsOn(s_appNotStarted|s_shuttingDown))
        return;

    queueCmd(new GDBCommand(GDBMI::ExecNextInstruction, QString(), CmdMaybeStartsRunning | CmdTemporaryRun));
}

void DebugSession::stepInto()
{
    if (stateIsOn(s_appNotStarted|s_shuttingDown))
        return;

    queueCmd(new GDBCommand(GDBMI::ExecStep, QString(), CmdMaybeStartsRunning | CmdTemporaryRun));
}

void DebugSession::stepIntoInstruction()
{
    if (stateIsOn(s_appNotStarted|s_shuttingDown))
        return;

    queueCmd(new GDBCommand(GDBMI::ExecStepInstruction, QString(), CmdMaybeStartsRunning | CmdTemporaryRun));
}

void DebugSession::slotDebuggerAbnormalExit()
{
    KMessageBox::information(
        KDevelop::ICore::self()->uiController()->activeMainWindow(),
        i18n("<b>GDB exited abnormally</b>"
             "<p>This is likely a bug in GDB. "
             "Examine the gdb output window and then stop the debugger"),
        i18n("GDB exited abnormally"));

    // Note: we don't stop the debugger here, becuse that will hide gdb
    // window and prevent the user from finding the exact reason of the
    // problem.
}

bool DebugSession::restartAvaliable() const
{
    if (stateIsOn(s_attached) || stateIsOn(s_core)) {
        return false;
    } else {
        return true;
    }
}
void DebugSession::configure()
{
//     KConfigGroup config(KSharedConfig::openConfig(), "GDB Debugger");
//
//     // A a configure.gdb script will prevent these from uncontrolled growth...
//     config_configGdbScript_       = config.readEntry("Remote GDB Configure Script", "");
//     config_runShellScript_        = config.readEntry("Remote GDB Shell Script", "");
//     config_runGdbScript_          = config.readEntry("Remote GDB Run Script", "");
//
//     // PORTING TODO: where is this in the ui?
//     config_forceBPSet_            = config.readEntry("Allow Forced Breakpoint Set", true);
//
//     config_dbgTerminal_           = config.readEntry("Separate Terminal For Application IO", false);
//
//     bool old_displayStatic        = config_displayStaticMembers_;
//     config_displayStaticMembers_  = config.readEntry("Display Static Members",false);
//
//     bool old_asmDemangle  = config_asmDemangle_;
//     config_asmDemangle_   = config.readEntry("Display Demangle Names",true);
//
//     bool old_breakOnLoadingLibrary_ = config_breakOnLoadingLibrary_;
//     config_breakOnLoadingLibrary_ = config.readEntry("Try Setting Breakpoints On Loading Libraries",true);
//
//     // FIXME: should move this into debugger part or variable widget.
//     int old_outputRadix  = config_outputRadix_;
// #if 0
//     config_outputRadix_   = DomUtil::readIntEntry("Output Radix", 10);
//     varTree_->setRadix(config_outputRadix_);
// #endif
//
//
//     if (( old_displayStatic             != config_displayStaticMembers_   ||
//             old_asmDemangle             != config_asmDemangle_            ||
//             old_breakOnLoadingLibrary_  != config_breakOnLoadingLibrary_  ||
//             old_outputRadix             != config_outputRadix_)           &&
//             m_gdb)
//     {
//         bool restart = false;
//         if (stateIsOn(s_dbgBusy))
//         {
//             slotPauseApp();
//             restart = true;
//         }
//
//         if (old_displayStatic != config_displayStaticMembers_)
//         {
//             if (config_displayStaticMembers_)
//                 queueCmd(new GDBCommand(GDBMI::GdbSet, "print static-members on"));
//             else
//                 queueCmd(new GDBCommand(GDBMI::GdbSet, "print static-members off"));
//         }
//         if (old_asmDemangle != config_asmDemangle_)
//         {
//             if (config_asmDemangle_)
//                 queueCmd(new GDBCommand(GDBMI::GdbSet, "print asm-demangle on"));
//             else
//                 queueCmd(new GDBCommand(GDBMI::GdbSet, "print asm-demangle off"));
//         }
//
//         // Disabled for MI port.
//         if (old_outputRadix != config_outputRadix_)
//         {
//             queueCmd(new GDBCommand(GDBMI::GdbSet, QString().sprintf("output-radix %d",
//                                 config_outputRadix_)));
//
//             // FIXME: should do this in variable widget anyway.
//             // After changing output radix, need to refresh variables view.
//             raiseEvent(program_state_changed);
//
//         }
//
//         if (config_configGdbScript_.isValid())
//           queueCmd(new GDBCommand(GDBMI::NonMI, "source " + config_configGdbScript_.toLocalFile()));
//
//
//         if (restart)
//             queueCmd(new GDBCommand(GDBMI::ExecContinue));
//     }
}

// **************************************************************************

void DebugSession::addCommand(GDBCommand* cmd)
{
    queueCmd(cmd);
}

void DebugSession::addCommand(GDBMI::CommandType type, const QString& str)
{
    queueCmd(new GDBCommand(type, str));
}

// Fairly obvious that we'll add whatever command you give me to a queue
// Not quite so obvious though is that if we are going to run again. then any
// information requests become redundent and must be removed.
// We also try and run whatever command happens to be at the head of
// the queue.
void DebugSession::queueCmd(GDBCommand *cmd)
{
    if (stateIsOn(s_dbgNotStarted))
    {
        KMessageBox::information(
            qApp->activeWindow(),
            i18n("<b>Gdb command sent when debugger is not running</b><br>"
            "The command was:<br> %1", cmd->initialString()),
            i18n("Internal error"));
        return;
    }

    if (stateReloadInProgress_)
        cmd->setStateReloading(true);

    commandQueue_->enqueue(cmd);

    qCDebug(DEBUGGERGDB) << "QUEUE: " << cmd->initialString() << (stateReloadInProgress_ ? "(state reloading)" : "") << commandQueue_->count() << "pending";

    bool varCommandWithContext= (cmd->type() >= GDBMI::VarAssign
                                 && cmd->type() <= GDBMI::VarUpdate
                                 && cmd->type() != GDBMI::VarDelete);

    bool stackCommandWithContext = (cmd->type() >= GDBMI::StackInfoDepth
                                    && cmd->type() <= GDBMI::StackListLocals);

    if (varCommandWithContext || stackCommandWithContext)
    {
        if (cmd->thread() == -1)
            qCDebug(DEBUGGERGDB) << "\t--thread will be added on execution";

        if (cmd->frame() == -1)
            qCDebug(DEBUGGERGDB) << "\t--frame will be added on execution";
    }

    setStateOn(s_dbgBusy);
    raiseEvent(debugger_busy);

    executeCmd();
}

void DebugSession::executeCmd()
{
    Q_ASSERT(m_gdb);

    if (stateIsOn(s_dbgNotListening) && commandQueue_->haveImmediateCommand()) {
        // We may have to call this even while a command is currently executing, because
        // Gdb can get into a state where a command such as ExecRun does not send a response
        // while the inferior is running.
        ensureGdbListening();
    }

    if (!m_gdb.data()->isReady())
        return;

    GDBCommand* currentCmd = commandQueue_->nextCommand();
    if (!currentCmd)
        return;

    if (currentCmd->flags() & (CmdMaybeStartsRunning | CmdInterrupt)) {
        setStateOff(s_automaticContinue);
    }

    if (currentCmd->flags() & CmdMaybeStartsRunning) {
        // GDB can be in a state where it is listening for commands while the program is running.
        // However, when we send a command such as ExecContinue in this state, GDB may return to
        // the non-listening state without acknowledging that the ExecContinue command has even
        // finished, let alone sending a new notification about the program's running state.
        // So let's be extra cautious about ensuring that we will wake GDB up again if required.
        setStateOn(s_dbgNotListening);
    }

    bool varCommandWithContext= (currentCmd->type() >= GDBMI::VarAssign
                                 && currentCmd->type() <= GDBMI::VarUpdate
                                 && currentCmd->type() != GDBMI::VarDelete);

    bool stackCommandWithContext = (currentCmd->type() >= GDBMI::StackInfoDepth
                                    && currentCmd->type() <= GDBMI::StackListLocals);

    if (varCommandWithContext || stackCommandWithContext)
    {
        // Most var commands should be executed in the context
        // of the selected thread and frame.
        if (currentCmd->thread() == -1)
            currentCmd->setThread(frameStackModel()->currentThread());

        if (currentCmd->frame() == -1)
            currentCmd->setFrame(frameStackModel()->currentFrame());
    }

    QString commandText = currentCmd->cmdToSend();
    bool bad_command = false;
    QString message;

    int length = commandText.length();
    // No i18n for message since it's mainly for debugging.
    if (length == 0)
    {
        // The command might decide it's no longer necessary to send
        // it.
        if (SentinelCommand* sc = dynamic_cast<SentinelCommand*>(currentCmd))
        {
            qCDebug(DEBUGGERGDB) << "SEND: sentinel command, not sending";
            sc->invokeHandler();
        }
        else
        {
            qCDebug(DEBUGGERGDB) << "SEND: command " << currentCmd->initialString()
                          << "changed its mind, not sending";
        }

        delete currentCmd;
        executeCmd();
        return;
    }
    else
    {
        if (commandText[length-1] != '\n')
        {
            bad_command = true;
            message = "Debugger command does not end with newline";
        }
    }
    if (bad_command)
    {
        KMessageBox::information(qApp->activeWindow(),
                                 i18n("<b>Invalid debugger command</b><br>%1", message),
                                 i18n("Invalid debugger command"));
        executeCmd();
        return;
    }

    m_gdb.data()->execute(currentCmd);
}

// **************************************************************************

void DebugSession::destroyCmds()
{
    commandQueue_->clear();
}


void DebugSession::slotProgramStopped(const GDBMI::AsyncRecord& r)
{
    /* By default, reload all state on program stop.  */
    state_reload_needed = true;
    setStateOff(s_appRunning);
    setStateOff(s_dbgNotListening);

    QString reason;
    if (r.hasField("reason")) reason = r["reason"].literal();

    if (reason == "exited-normally" || reason == "exited")
    {
        if (r.hasField("exit-code")) {
            programNoApp(i18n("Exited with return code: %1", r["exit-code"].literal()));
        } else {
            programNoApp(i18n("Exited normally"));
        }
        state_reload_needed = false;
        return;
    }

    if (reason == "exited-signalled")
    {
        programNoApp(i18n("Exited on signal %1", r["signal-name"].literal()));
        state_reload_needed = false;
        return;
    }

    if (reason == "watchpoint-scope")
    {
        QString number = r["wpnum"].literal();

        // FIXME: shuld remove this watchpoint
        // But first, we should consider if removing all
        // watchpoinst on program exit is the right thing to
        // do.

        queueCmd(new GDBCommand(GDBMI::ExecContinue, QString(), CmdMaybeStartsRunning));

        state_reload_needed = false;
        return;
    }

    bool wasInterrupt = false;

    if (reason == "signal-received")
    {
        QString name = r["signal-name"].literal();
        QString user_name = r["signal-meaning"].literal();

        // SIGINT is a "break into running program".
        // We do this when the user set/mod/clears a breakpoint but the
        // application is running.
        // And the user does this to stop the program also.
        if (name == "SIGINT" && stateIsOn(s_interruptSent)) {
            wasInterrupt = true;
        } else {
            // Whenever we have a signal raised then tell the user, but don't
            // end the program as we want to allow the user to look at why the
            // program has a signal that's caused the prog to stop.
            // Continuing from SIG FPE/SEGV will cause a "Cannot ..." and
            // that'll end the program.
            programFinished(i18n("Program received signal %1 (%2)", name, user_name));
        }
    }

    if (!reason.contains("exited"))
    {
        // FIXME: we should immediately update the current thread and
        // frame in the framestackmodel, so that any user actions
        // are in that thread. However, the way current framestack model
        // is implemented, we can't change thread id until we refresh
        // the entire list of threads -- otherwise we might set a thread
        // id that is not already in the list, and it will be upset.

        //Indicates if program state should be reloaded immediately.
        bool updateState = false;

        if (r.hasField("frame")) {
            const GDBMI::Value& frame = r["frame"];
            QString file, line, addr;

            if (frame.hasField("fullname")) file = frame["fullname"].literal();;
            if (frame.hasField("line"))     line = frame["line"].literal();
            if (frame.hasField("addr"))     addr = frame["addr"].literal();

            // gdb counts lines from 1 and we don't
            setCurrentPosition(QUrl::fromLocalFile(file), line.toInt() - 1, addr);

            updateState = true;
        }

        if (updateState) {
            reloadProgramState();
        }
    }

    setStateOff(s_interruptSent);
    if (!wasInterrupt)
        setStateOff(s_automaticContinue);
}


void DebugSession::processNotification(const GDBMI::AsyncRecord & async)
{
    if (async.reason == "thread-group-started") {
        setStateOff(s_appNotStarted | s_programExited);
    } else if (async.reason == "thread-group-exited") {
        setStateOn(s_programExited);
    } else if (async.reason == "library-loaded") {
        // do nothing
    } else if (async.reason == "breakpoint-created") {
        breakpointController()->notifyBreakpointCreated(async);
    } else if (async.reason == "breakpoint-modified") {
        breakpointController()->notifyBreakpointModified(async);
    } else if (async.reason == "breakpoint-deleted") {
        breakpointController()->notifyBreakpointDeleted(async);
    } else {
        qCDebug(DEBUGGERGDB) << "Unhandled notification: " << async.reason;
    }
}

void DebugSession::reloadProgramState()
{
    raiseEvent(program_state_changed);
    state_reload_needed = false;
}


// **************************************************************************

// There is no app anymore. This can be caused by program exiting
// an invalid program specified or ...
// gdb is still running though, but only the run command (may) make sense
// all other commands are disabled.
void DebugSession::programNoApp(const QString& msg)
{
    qCDebug(DEBUGGERGDB) << msg;

    setState(s_appNotStarted|s_programExited|(state_&s_shuttingDown));

    destroyCmds();

    // The application has existed, but it's possible that
    // some of application output is still in the pipe. We use
    // different pipes to communicate with gdb and to get application
    // output, so "exited" message from gdb might have arrived before
    // last application output. Get this last bit.

    // Note: this method can be called when we open an invalid
    // core file. In that case, tty_ won't be set.
    if (m_tty){
        m_tty->readRemaining();
        // Tty is no longer usable, delete it. Without this, QSocketNotifier
        // will continiously bomd STTY with signals, so we need to either disable
        // QSocketNotifier, or delete STTY. The latter is simpler, since we can't
        // reuse it for future debug sessions anyway.
        m_tty.reset(0);
    }

    stopDebugger();

    raiseEvent(program_exited);
    raiseEvent(debugger_exited);

    emit showMessage(msg, 0);

    programFinished(msg);
}


void DebugSession::programFinished(const QString& msg)
{
    QString m = QString("*** %0 ***").arg(msg.trimmed());
    emit applicationStandardErrorLines(QStringList(m));

    /* Also show message in gdb window, so that users who
       prefer to look at gdb window know what's up.  */
    emit gdbUserCommandStdout(m);
}


bool DebugSession::startDebugger(KDevelop::ILaunchConfiguration* cfg)
{
    qCDebug(DEBUGGERGDB) << "Starting debugger controller";

    if(m_gdb) {
        qWarning() << "m_gdb object still existed";
        delete m_gdb.data();
        m_gdb.clear();
    }

    GDB* gdb = new GDB(this);
    m_gdb = gdb;

    // FIXME: here, we should wait until GDB is up and waiting for input.
    // Then, clear s_dbgNotStarted
    // It's better to do this right away so that the state bit is always
    // correct.

    /** FIXME: connect ttyStdout. It takes QByteArray, so
        I'm not sure what to do.  */
#if 0
    connect(gdb, SIGNAL(applicationOutput(QString)),
            this, SIGNAL(ttyStdout(QString)));
#endif
    connect(gdb, &GDB::userCommandOutput, this,
            &DebugSession::gdbUserCommandStdout);
    connect(gdb, &GDB::internalCommandOutput, this,
            &DebugSession::gdbInternalCommandStdout);

    connect(gdb, &GDB::ready, this, &DebugSession::gdbReady);
    connect(gdb, &GDB::gdbExited, this, &DebugSession::gdbExited);
    connect(gdb, &GDB::programStopped,
            this, &DebugSession::slotProgramStopped);
    connect(gdb, &GDB::programStopped,
            this, &DebugSession::programStopped);
    connect(gdb, &GDB::programRunning,
            this, &DebugSession::programRunning);
    connect(gdb, &GDB::notification,
            this, &DebugSession::processNotification);

    // Start gdb. Do this after connecting all signals so that initial
    // GDB output, and important events like "GDB died" are reported.

    {
        QStringList extraArguments;
        if (m_testing)
            extraArguments << "--nx"; // do not load any .gdbinit files

        if (cfg)
        {
            KConfigGroup config = cfg->config();
            m_gdb.data()->start(config, extraArguments);
        }
        else
        {
            // FIXME: this is hack, I am not sure there's any way presently
            // to edit this via GUI.
            KConfigGroup config(KSharedConfig::openConfig(), "GDB Debugger");
            m_gdb.data()->start(config, extraArguments);
        }
    }

    setStateOff(s_dbgNotStarted);

    // Initialise gdb. At this stage gdb is sitting wondering what to do,
    // and to whom. Organise a few things, then set up the tty for the application,
    // and the application itself
    //queueCmd(new GDBCommand(GDBMI::EnableTimings, "yes"));

    queueCmd(new CliCommand(GDBMI::GdbShow, "version", this, &DebugSession::handleVersion));

    // This makes gdb pump a variable out on one line.
    queueCmd(new GDBCommand(GDBMI::GdbSet, "width 0"));
    queueCmd(new GDBCommand(GDBMI::GdbSet, "height 0"));

    queueCmd(new GDBCommand(GDBMI::SignalHandle, "SIG32 pass nostop noprint"));
    queueCmd(new GDBCommand(GDBMI::SignalHandle, "SIG41 pass nostop noprint"));
    queueCmd(new GDBCommand(GDBMI::SignalHandle, "SIG42 pass nostop noprint"));
    queueCmd(new GDBCommand(GDBMI::SignalHandle, "SIG43 pass nostop noprint"));

    queueCmd(new GDBCommand(GDBMI::EnablePrettyPrinting));

    queueCmd(new GDBCommand(GDBMI::GdbSet, "charset UTF-8"));
    queueCmd(new GDBCommand(GDBMI::GdbSet, "print sevenbit-strings off"));

    QString fileName = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "kdevgdb/printers/gdbinit");
    if (!fileName.isEmpty()) {
        QFileInfo fileInfo(fileName);
        QString quotedPrintersPath = fileInfo.dir().path().replace('\\', "\\\\").replace('"', "\\\"");
        queueCmd(new GDBCommand(GDBMI::NonMI,
            QString("python sys.path.insert(0, \"%0\")").arg(quotedPrintersPath)));
        queueCmd(new GDBCommand(GDBMI::NonMI, "source " + fileName));
    }

    return true;
}

bool DebugSession::startProgram(KDevelop::ILaunchConfiguration* cfg, IExecutePlugin* iface)
{
    if (stateIsOn( s_appNotStarted ) )
    {
        emit showMessage(i18n("Running program"), 1000);
    }

    if (stateIsOn(s_dbgNotStarted)) {
        if (!startDebugger(cfg))
            return false;
    }

    if (stateIsOn(s_shuttingDown)) {
        qCDebug(DEBUGGERGDB) << "Tried to run when debugger shutting down";
        return false;
    }



    KConfigGroup grp = cfg->config();
    KDevelop::EnvironmentGroupList l(KSharedConfig::openConfig());

    QString envgrp = iface->environmentGroup( cfg );
    if( envgrp.isEmpty() )
    {
        qWarning() << i18n("No environment group specified, looks like a broken "
            "configuration, please check run configuration '%1'. "
            "Using default environment group.", cfg->name() );
        envgrp = l.defaultGroup();
    }


    if (grp.readEntry("Break on Start", false)) {
        BreakpointModel* m = KDevelop::ICore::self()->debugController()->breakpointModel();
        bool found = false;
        foreach (KDevelop::Breakpoint *b, m->breakpoints()) {
            if (b->location() == "main") {
                found = true;
                break;
            }
        }
        if (!found) {
            KDevelop::ICore::self()->debugController()->breakpointModel()->addCodeBreakpoint("main");
        }
    }


    // Configuration values
    bool    config_displayStaticMembers_ = grp.readEntry( GDBDebugger::staticMembersEntry, false );
    bool    config_asmDemangle_ = grp.readEntry( GDBDebugger::demangleNamesEntry, true );
    QUrl config_configGdbScript_ = grp.readEntry( GDBDebugger::remoteGdbConfigEntry, QUrl() );
    QUrl config_runShellScript_ = grp.readEntry( GDBDebugger::remoteGdbShellEntry, QUrl() );
    QUrl config_runGdbScript_ = grp.readEntry( GDBDebugger::remoteGdbRunEntry, QUrl() );

    Q_ASSERT(iface);
    bool config_useExternalTerminal = iface->useTerminal( cfg );
    QString config_externalTerminal = iface->terminal( cfg );
    if (!config_externalTerminal.isEmpty()) {
        // the external terminal cmd contains additional arguments, just get the terminal name
        config_externalTerminal = KShell::splitArgs(config_externalTerminal).first();
    }

    m_tty.reset(new STTY(config_useExternalTerminal, config_externalTerminal));
    if (!config_useExternalTerminal)
    {
        connect( m_tty.data(), &STTY::OutOutput, this, &DebugSession::ttyStdout );
        connect( m_tty.data(), &STTY::ErrOutput, this, &DebugSession::ttyStderr );
    }

    QString tty(m_tty->getSlave());
    if (tty.isEmpty())
    {
        KMessageBox::information(qApp->activeWindow(), m_tty->lastError(), i18n("Warning"));

        m_tty.reset(0);
        return false;
    }

    queueCmd(new GDBCommand(GDBMI::InferiorTtySet, tty));

    // Only dummy err here, actual erros have been checked already in the job and we don't get here if there were any
    QString err;
    QString executable = iface->executable(cfg, err).toLocalFile();

    QStringList arguments = iface->arguments(cfg, err);
    // Change the "Working directory" to the correct one
    QString dir = iface->workingDirectory(cfg).toLocalFile();
    if (dir.isEmpty()) {
        dir = QFileInfo(executable).absolutePath();
    }

    queueCmd(new GDBCommand(GDBMI::EnvironmentCd, '"' + dir + '"'));

    // Set the run arguments
    if (!arguments.isEmpty())
        queueCmd(
            new GDBCommand(GDBMI::ExecArguments, KShell::joinArgs( arguments )));

    foreach (const QString& envvar, l.createEnvironment(envgrp, QStringList()))
        queueCmd(new GDBCommand(GDBMI::GdbSet, "environment " + envvar));

    // Needed so that breakpoint widget has a chance to insert breakpoints.
    // FIXME: a bit hacky, as we're really not ready for new commands.
    setStateOn(s_dbgBusy);
    raiseEvent(debugger_ready);


    if (config_displayStaticMembers_)
        queueCmd(new GDBCommand(GDBMI::GdbSet, "print static-members on"));
    else
        queueCmd(new GDBCommand(GDBMI::GdbSet, "print static-members off"));
    if (config_asmDemangle_)
        queueCmd(new GDBCommand(GDBMI::GdbSet, "print asm-demangle on"));
    else
        queueCmd(new GDBCommand(GDBMI::GdbSet, "print asm-demangle off"));

    if (config_configGdbScript_.isValid())
        queueCmd(new GDBCommand(GDBMI::NonMI, "source " + KShell::quoteArg(config_configGdbScript_.toLocalFile())));


    if (!config_runShellScript_.isEmpty()) {
        // Special for remote debug...
        QByteArray tty(m_tty->getSlave().toLatin1());
        QByteArray options = QByteArray(">") + tty + QByteArray("  2>&1 <") + tty;

        QProcess *proc = new QProcess;
        QStringList arguments;
        arguments << "-c" << KShell::quoteArg(config_runShellScript_.toLocalFile()) +
            ' ' + KShell::quoteArg(executable) + QString::fromLatin1( options );

        qCDebug(DEBUGGERGDB) << "starting sh" << arguments;
        proc->start("sh", arguments);
        //PORTING TODO QProcess::DontCare);
    }

    if (!config_runGdbScript_.isEmpty()) {// gdb script at run is requested

        // Race notice: wait for the remote gdbserver/executable
        // - but that might be an issue for this script to handle...

        // Future: the shell script should be able to pass info (like pid)
        // to the gdb script...

        queueCmd(new SentinelCommand([this, config_runGdbScript_]() {
            breakpointController()->initSendBreakpoints();

            breakpointController()->setDeleteDuplicateBreakpoints(true);
            qCDebug(DEBUGGERGDB) << "Running gdb script " << KShell::quoteArg(config_runGdbScript_.toLocalFile());
            queueCmd(new GDBCommand(GDBMI::NonMI, "source " + KShell::quoteArg(config_runGdbScript_.toLocalFile()),
                                    [this](const GDBMI::ResultRecord&) {breakpointController()->setDeleteDuplicateBreakpoints(false);},
                                    CmdMaybeStartsRunning));
            raiseEvent(connected_to_program);
            // Note: script could contain "run" or "continue"
        }, CmdMaybeStartsRunning));
    }
    else
    {
        queueCmd(new GDBCommand(GDBMI::FileExecAndSymbols, KShell::quoteArg(executable), this, &DebugSession::handleFileExecAndSymbols, CmdHandlesError));
        raiseEvent(connected_to_program);

        queueCmd(new SentinelCommand([this]() {
            breakpointController()->initSendBreakpoints();
            queueCmd(new GDBCommand(GDBMI::ExecRun, QString(), CmdMaybeStartsRunning));
        }, CmdMaybeStartsRunning));
    }

    {
        QString startWith = grp.readEntry(GDBDebugger::startWithEntry, QString("ApplicationOutput"));
        if (startWith == "GdbConsole") {
            emit raiseGdbConsoleViews();
        } else if (startWith == "FrameStack") {
            emit raiseFramestackViews();
        } else {
            //ApplicationOutput is raised in DebugJob (by setting job to Verbose/Silent)
        }
    }

    return true;
}


// **************************************************************************
//                                SLOTS
//                                *****
// For most of these slots data can only be sent to gdb when it
// isn't busy and it is running.

// **************************************************************************

void DebugSession::slotKillGdb()
{
    if (!stateIsOn(s_programExited) && stateIsOn(s_shuttingDown))
    {
        qCDebug(DEBUGGERGDB) << "gdb not shutdown - killing";
        m_gdb.data()->kill();

        setState(s_dbgNotStarted | s_appNotStarted);

        raiseEvent(debugger_exited);
    }
}

// **************************************************************************

void DebugSession::slotKill()
{
    if (stateIsOn(s_dbgNotStarted|s_shuttingDown))
        return;

    if (stateIsOn(s_dbgBusy))
    {
        interruptDebugger();
    }

    // The -exec-abort is not implemented in gdb
    // queueCmd(new GDBCommand(GDBMI::ExecAbort));
    queueCmd(new GDBCommand(GDBMI::NonMI, "kill"));
}

// **************************************************************************

void DebugSession::runUntil(const QUrl& url, int line)
{
    if (stateIsOn(s_dbgNotStarted|s_shuttingDown))
        return;

    if (!url.isValid())
        queueCmd(new GDBCommand(GDBMI::ExecUntil, QString::number(line),
                                CmdMaybeStartsRunning | CmdTemporaryRun));
    else
        queueCmd(new GDBCommand(GDBMI::ExecUntil,
                QString("%1:%2").arg(url.toLocalFile()).arg(line),
                CmdMaybeStartsRunning | CmdTemporaryRun));
}

void DebugSession::runUntil(QString& address){
    if (stateIsOn(s_dbgNotStarted|s_shuttingDown))
        return;

    if (!address.isEmpty()) {
        queueCmd(new GDBCommand(GDBMI::ExecUntil, QString("*%1").arg(address),
                                CmdMaybeStartsRunning | CmdTemporaryRun));
    }
}
// **************************************************************************

void DebugSession::jumpToMemoryAddress(QString& address){
    if (stateIsOn(s_dbgNotStarted|s_shuttingDown))
        return;

    if (!address.isEmpty()) {
        queueCmd(new GDBCommand(GDBMI::NonMI, QString("tbreak *%1").arg(address)));
        queueCmd(new GDBCommand(GDBMI::NonMI, QString("jump *%1").arg(address)));
    }
}

void DebugSession::jumpTo(const QUrl& url, int line)
{
    if (stateIsOn(s_dbgNotStarted|s_shuttingDown))
        return;

    if (url.isValid()) {
        queueCmd(new GDBCommand(GDBMI::NonMI, QString("tbreak %1:%2").arg(url.toLocalFile()).arg(line)));
        queueCmd(new GDBCommand(GDBMI::NonMI, QString("jump %1:%2").arg(url.toLocalFile()).arg(line)));
    }
}

// **************************************************************************

// FIXME: connect to GDB's slot.
void DebugSession::defaultErrorHandler(const GDBMI::ResultRecord& result)
{
    QString msg = result["msg"].literal();

    if (msg.contains("No such process"))
    {
        setState(s_appNotStarted|s_programExited);
        raiseEvent(program_exited);
        return;
    }

    KMessageBox::information(
        qApp->activeWindow(),
        i18n("<b>Debugger error</b>"
             "<p>Debugger reported the following error:"
             "<p><tt>%1", result["msg"].literal()),
        i18n("Debugger error"));

    // Error most likely means that some change made in GUI
    // was not communicated to the gdb, so GUI is now not
    // in sync with gdb. Resync it.
    //
    // Another approach is to make each widget reload it content
    // on errors from commands that it sent, but that's too complex.
    // Errors are supposed to happen rarely, so full reload on error
    // is not a big deal. Well, maybe except for memory view, but
    // it's no auto-reloaded anyway.
    //
    // Also, don't reload state on errors appeared during state
    // reloading!
    if (!m_gdb.data()->currentCommand()->stateReloading())
        raiseEvent(program_state_changed);
}

void DebugSession::gdbReady()
{
    stateReloadInProgress_ = false;

    executeCmd();
    if (m_gdb->isReady())
    {
        /* There is nothing in the command queue and no command is currently executing. */

        if (stateIsOn(s_automaticContinue)) {
            if (!stateIsOn(s_appRunning)) {
                qCDebug(DEBUGGERGDB) << "Posting automatic continue";
                queueCmd(new GDBCommand(GDBMI::ExecContinue, QString(), CmdMaybeStartsRunning));
            }
            setStateOff(s_automaticContinue);
            return;
        }

        if (state_reload_needed && !stateIsOn(s_appRunning))
        {
            qCDebug(DEBUGGERGDB) << "Finishing program stop";
            // Set to false right now, so that if 'actOnProgramPauseMI_part2'
            // sends some commands, we won't call it again when handling replies
            // from that commands.
            state_reload_needed = false;
            reloadProgramState();
        }

        qCDebug(DEBUGGERGDB) << "No more commands";
        setStateOff(s_dbgBusy);
        raiseEvent(debugger_ready);
    }
}

void DebugSession::gdbExited()
{
    qCDebug(DEBUGGERGDB);
    /* Technically speaking, GDB is likely not to kill the application, and
       we should have some backup mechanism to make sure the application is
       killed by KDevelop.  But even if application stays around, we no longer
       can control it in any way, so mark it as exited.  */
    setStateOn(s_appNotStarted);
    setStateOn(s_dbgNotStarted);
    setStateOff(s_shuttingDown);
}

void DebugSession::ensureGdbListening()
{
    Q_ASSERT(m_gdb);
    m_gdb->interrupt();
    setStateOn(s_interruptSent);
    if (stateIsOn(s_appRunning))
        setStateOn(s_automaticContinue);
    setStateOff(s_dbgNotListening);
}

// FIXME: I don't fully remember what is the business with
// stateReloadInProgress_ and whether we can lift it to the
// generic level.
void DebugSession::raiseEvent(event_t e)
{
    if (e == program_exited || e == debugger_exited)
    {
        stateReloadInProgress_ = false;
    }

    if (e == program_state_changed)
    {
        stateReloadInProgress_ = true;
        qCDebug(DEBUGGERGDB) << "State reload in progress\n";
    }

    IDebugSession::raiseEvent(e);

    if (e == program_state_changed)
    {
        stateReloadInProgress_ = false;
    }
}

// **************************************************************************

void DebugSession::slotUserGDBCmd(const QString& cmd)
{
    queueCmd(new UserCommand(GDBMI::NonMI, cmd));

    // User command can theoreticall modify absolutely everything,
    // so need to force a reload.

    // We can do it right now, and don't wait for user command to finish
    // since commands used to reload all view will be executed after
    // user command anyway.
    if (!stateIsOn(s_appNotStarted) && !stateIsOn(s_programExited))
        raiseEvent(program_state_changed);
}

void DebugSession::explainDebuggerStatus()
{
    GDBCommand* currentCmd_ = m_gdb.data()->currentCommand();
    QString information =
        i18np("1 command in queue\n", "%1 commands in queue\n", commandQueue_->count()) +
        i18ncp("Only the 0 and 1 cases need to be translated", "1 command being processed by gdb\n", "%1 commands being processed by gdb\n", (currentCmd_ ? 1 : 0)) +
        i18n("Debugger state: %1\n", state_);

    if (currentCmd_)
    {
        QString extra = i18n("Current command class: '%1'\n"
                             "Current command text: '%2'\n"
                             "Current command original text: '%3'\n",
                             typeid(*currentCmd_).name(),
                             currentCmd_->cmdToSend(),
                             currentCmd_->initialString());

        information += extra;
    }

    KMessageBox::information(qApp->activeWindow(), information,
                             i18n("Debugger status"));
}

bool DebugSession::stateIsOn(DBGStateFlags state) const
{
    return state_ & state;
}

DBGStateFlags DebugSession::debuggerState() const
{
    return state_;
}

void DebugSession::setStateOn(DBGStateFlags stateOn)
{
    DBGStateFlags oldState = state_;

    debugStateChange(state_, state_ | stateOn);
    state_ |= stateOn;

    _gdbStateChanged(oldState, state_);
}

void DebugSession::setStateOff(DBGStateFlags stateOff)
{
    DBGStateFlags oldState = state_;

    debugStateChange(state_, state_ & ~stateOff);
    state_ &= ~stateOff;

    _gdbStateChanged(oldState, state_);
}

void DebugSession::setState(DBGStateFlags newState)
{
    DBGStateFlags oldState = state_;

    debugStateChange(state_, newState);
    state_ = newState;

    _gdbStateChanged(oldState, state_);
}

void DebugSession::debugStateChange(DBGStateFlags oldState, DBGStateFlags newState)
{
    int delta = oldState ^ newState;
    if (delta)
    {
        QString out = "STATE:";
#define STATE_CHECK(name) \
    do { \
        if (delta & name) { \
            out += ((newState & name) ? " +" : " -"); \
            out += #name; \
            delta &= ~name; \
        } \
    } while (0)
        STATE_CHECK(s_dbgNotStarted);
        STATE_CHECK(s_appNotStarted);
        STATE_CHECK(s_programExited);
        STATE_CHECK(s_attached);
        STATE_CHECK(s_core);
        STATE_CHECK(s_shuttingDown);
        STATE_CHECK(s_dbgBusy);
        STATE_CHECK(s_appRunning);
        STATE_CHECK(s_dbgNotListening);
        STATE_CHECK(s_automaticContinue);
#undef STATE_CHECK

        for (unsigned int i = 0; delta != 0 && i < 32; ++i) {
            if (delta & (1 << i))  {
                delta &= ~(1 << i);
                out += ((1 << i) & newState) ? " +" : " -";
                out += QString::number(i);
            }
        }
        qCDebug(DEBUGGERGDB) << out;
    }
}

void DebugSession::programRunning()
{
    setStateOn(s_appRunning);
    raiseEvent(program_running);

    if (commandQueue_->haveImmediateCommand() ||
        (m_gdb->currentCommand() && (m_gdb->currentCommand()->flags() & (CmdImmediately | CmdInterrupt)))) {
        ensureGdbListening();
    } else {
        setStateOn(s_dbgNotListening);
    }
}

void DebugSession::handleVersion(const QStringList& s)
{
    qCDebug(DEBUGGERGDB) << s.first();
    // minimal version is 7.0,0
    QRegExp rx("([7-9]+)\\.([0-9]+)(\\.([0-9]+))?");
    int idx = rx.indexIn(s.first());
    if (idx == -1)
    {
        if (qobject_cast<QGuiApplication*>(qApp))  {
            //for unittest
            qFatal("You need a graphical application.");
        }
        KMessageBox::error(
            qApp->activeWindow(),
            i18n("<b>You need gdb 7.0.0 or higher.</b><br />"
            "You are using: %1", s.first()),
            i18n("gdb error"));
        stopDebugger();
    }
}


void DebugSession::handleFileExecAndSymbols(const GDBMI::ResultRecord& r)
{
    if (r.reason == "error") {
        KMessageBox::error(
            qApp->activeWindow(),
            i18n("<b>Could not start debugger:</b><br />")+
            r["msg"].literal(),
            i18n("Startup error"));
        stopDebugger();
    }
}

void DebugSession::handleTargetAttach(const GDBMI::ResultRecord& r)
{
    if (r.reason == "error") {
        KMessageBox::error(
            qApp->activeWindow(),
            i18n("<b>Could not attach debugger:</b><br />")+
            r["msg"].literal(),
            i18n("Startup error"));
        stopDebugger();
    }
}

}


