// *************************************************************************
//                          gdbcommandqueue.cpp
//                             -------------------
//    begin                : Wed Dec 5, 2007
//    copyright            : (C) 2007 by Hamish Rodda
//    email                : rodda@kde.org
// **************************************************************************
//
// **************************************************************************
// *                                                                        *
// *   This program is free software; you can redistribute it and/or modify *
// *   it under the terms of the GNU General Public License as published by *
// *   the Free Software Foundation; either version 2 of the License, or    *
// *   (at your option) any later version.                                  *
// *                                                                        *
// **************************************************************************
#include <kdebug.h>

#include "gdbcommandqueue.h"

#include "mi/gdbmi.h"
#include "gdbcommand.h"

using namespace GDBDebugger;
using namespace GDBMI;

CommandQueue::CommandQueue()
{
}

CommandQueue::~CommandQueue()
{
    qDeleteAll(m_commandList);
}

void GDBDebugger::CommandQueue::enqueue(GDBCommand * command, QueuePosition insertPosition)
{
    switch (insertPosition) {
        case QueueAtFront:
            m_commandList.prepend(command);
            break;
        case QueueAtEnd:
            m_commandList.append(command);
            break;

        case QueueWhileInterrupted: {
            int i;
            for (i = 0; i < m_commandList.count(); ++i)
                if (m_commandList.at(i)->isRun())
                    break;

            m_commandList.insert(i, command);
        }
    }

    rationalizeQueue(command);
    dumpQueue();
}

void CommandQueue::dumpQueue()
{
    kDebug(9012) << "Pending commands" << m_commandList.count();
    unsigned commandNum = 0;
    foreach(const GDBCommand* command, m_commandList) {
        kDebug(9012) << "Command" << commandNum << command->initialString();
        ++commandNum;
    }
}

void CommandQueue::rationalizeQueue(GDBCommand * command)
{
    if (command->type() >= ExecAbort && command->type() <= ExecUntil) {
      removeObsoleteExecCommands(command);
      // Changing execution location, abort any variable updates
      removeVariableUpdates();
      // ... and stack list updates
      removeStackListUpdates();
    }
    else if (command->type() >= StackListArguments && command->type() <= StackListLocals) {
    }
    removeDuplicates(command);
}

void GDBDebugger::CommandQueue::removeObsoleteExecCommands(GDBCommand* command)
{
    if(command->type() == ExecContinue || command->type() == ExecUntil)
    {
        // Remove all exec commands up the latest ExecContinue or ExecUntil
        QMutableListIterator<GDBCommand*> it = m_commandList;
        while (it.hasNext()) {
            GDBCommand* currentCmd = it.next();
            if (currentCmd != command && currentCmd->type() >= ExecAbort && currentCmd->type() <= ExecUntil)
                it.remove();
        }
    }
}

void GDBDebugger::CommandQueue::removeDuplicates(GDBCommand* command)
{
    QMutableListIterator<GDBCommand*> it = m_commandList;

    while (it.hasNext()) {
        GDBCommand* currentCmd = it.next();
        CommandType type = currentCmd->type();
        if (command != currentCmd && type == command->type() && command->initialString() == currentCmd->initialString()) {
            if( (command->frame() == -1 || command->frame() == currentCmd->frame()) &&
                (command->thread() == -1 || command->thread() == currentCmd->thread()) ) {
                it.remove();
            }
        }
    }
}

void GDBDebugger::CommandQueue::removeVariableUpdates()
{
    QMutableListIterator<GDBCommand*> it = m_commandList;

    while (it.hasNext()) {
        CommandType type = it.next()->type();
        if ((type >= VarEvaluateExpression && type <= VarListChildren) || type == VarUpdate)
            it.remove();
    }
}

void GDBDebugger::CommandQueue::removeStackListUpdates()
{
    QMutableListIterator<GDBCommand*> it = m_commandList;

    while (it.hasNext()) {
        CommandType type = it.next()->type();
        if (type >= StackListArguments && type <= StackListLocals)
            it.remove();
    }
}

void GDBDebugger::CommandQueue::clear()
{
    qDeleteAll(m_commandList);
    m_commandList.clear();
}

int GDBDebugger::CommandQueue::count() const
{
    return m_commandList.count();
}

bool GDBDebugger::CommandQueue::isEmpty() const
{
    return m_commandList.isEmpty();
}

GDBCommand * GDBDebugger::CommandQueue::nextCommand()
{
    if (!m_commandList.isEmpty())
        return m_commandList.takeAt(0);

    return 0;
}

