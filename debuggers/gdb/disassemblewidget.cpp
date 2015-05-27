/*
 * GDB Debugger Support
 *
 * Copyright 2000 John Birch <jbb@kdevelop.org>
 * Copyright 2006 Vladimir Prus  <ghost@cs.msu.su>
 * Copyright 2007 Hamish Rodda <rodda@kde.org>
 * Copyright 2013 Vlas Puhov <vlas.puhov@mail.ru>
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

#include "disassemblewidget.h"
#include "gdbcommand.h"
#include "debuggerplugin.h"

#include <ktextedit.h>
#include <kglobalsettings.h>

#include <QShowEvent>
#include <QHideEvent>
#include <QAction>
#include <QMenu>
#include <QBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QSplitter>
#include <QHeaderView>

#include <KLocalizedString>

#include <util/autoorientedsplitter.h>

#include <interfaces/icore.h>
#include <interfaces/idebugcontroller.h>
#include <debugger/interfaces/idebugsession.h>
#include "debugsession.h"

#include "registers/registersmanager.h"
#include "debug.h"

using namespace GDBMI;

namespace GDBDebugger
{

SelectAddressDialog::SelectAddressDialog(QWidget* parent)
    : QDialog(parent)
{
    m_ui.setupUi(this);
    setWindowTitle(i18n("Address Selector"));

    connect(m_ui.comboBox, &KHistoryComboBox::editTextChanged,
            this, &SelectAddressDialog::validateInput);
    connect(m_ui.comboBox, static_cast<void(KHistoryComboBox::*)()>(&KHistoryComboBox::returnPressed),
            this, &SelectAddressDialog::itemSelected);
}

QString SelectAddressDialog::address() const
{
    return hasValidAddress() ? m_ui.comboBox->currentText() : QString();
}

bool SelectAddressDialog::hasValidAddress() const
{
    bool ok;
    m_ui.comboBox->currentText().toLongLong(&ok, 16);

    return ok;
}

void SelectAddressDialog::updateOkState()
{
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(hasValidAddress());
}

void SelectAddressDialog::validateInput()
{
    updateOkState();
}

void SelectAddressDialog::itemSelected()
{
    QString text = m_ui.comboBox->currentText();
    if( hasValidAddress() && m_ui.comboBox->findText(text) < 0 )
        m_ui.comboBox->addItem(text);
}



DisassembleWindow::DisassembleWindow(QWidget *parent, DisassembleWidget* widget)
    : QTreeWidget(parent)
{
    /*context menu commands */{
    m_selectAddrAction = new QAction(i18n("Change &address"), this);
    m_selectAddrAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_selectAddrAction, &QAction::triggered, widget, &DisassembleWidget::slotChangeAddress);

    m_jumpToLocation = new QAction(QIcon::fromTheme("debug-execute-to-cursor"), i18n("&Jump to Cursor"), this);
    m_jumpToLocation->setWhatsThis(i18n("Sets the execution pointer to the current cursor position."));
    connect(m_jumpToLocation,&QAction::triggered, widget, &DisassembleWidget::jumpToCursor);

    m_runUntilCursor = new QAction(QIcon::fromTheme("debug-run-cursor"), i18n("&Run to Cursor"), this);
    m_runUntilCursor->setWhatsThis(i18n("Continues execution until the cursor position is reached."));
    connect(m_runUntilCursor,&QAction::triggered, widget, &DisassembleWidget::runToCursor);

    m_disassemblyFlavorAtt = new QAction(i18n("&AT&&T"), this);
    m_disassemblyFlavorAtt->setToolTip(i18n("GDB will use the AT&T disassembly flavor (e.g. mov 0xc(%ebp),%eax)."));
    m_disassemblyFlavorAtt->setData(DisassemblyFlavorATT);
    m_disassemblyFlavorAtt->setCheckable(true);

    m_disassemblyFlavorIntel = new QAction(i18n("&Intel"), this);
    m_disassemblyFlavorIntel->setToolTip(i18n("GDB will use the Intel disassembly flavor (e.g. mov eax, DWORD PTR [ebp+0xc])."));
    m_disassemblyFlavorIntel->setData(DisassemblyFlavorIntel);
    m_disassemblyFlavorIntel->setCheckable(true);

    m_disassemblyFlavorActionGroup = new QActionGroup(this);
    m_disassemblyFlavorActionGroup->setExclusive(true);
    m_disassemblyFlavorActionGroup->addAction(m_disassemblyFlavorAtt);
    m_disassemblyFlavorActionGroup->addAction(m_disassemblyFlavorIntel);
    connect(m_disassemblyFlavorActionGroup, &QActionGroup::triggered, widget, &DisassembleWidget::setDisassemblyFlavor);
    
    }
}

void DisassembleWindow::setDisassemblyFlavor(DisassemblyFlavor flavor)
{
    switch(flavor)
    {
    default:
    case DisassemblyFlavorUnknown:
        m_disassemblyFlavorAtt->setChecked(false);
        m_disassemblyFlavorIntel->setChecked(false);
        break;
    case DisassemblyFlavorATT:
        m_disassemblyFlavorAtt->setChecked(true);
        m_disassemblyFlavorIntel->setChecked(false);
        break;
    case DisassemblyFlavorIntel:
        m_disassemblyFlavorAtt->setChecked(false);
        m_disassemblyFlavorIntel->setChecked(true);
        break;
    }
}

void DisassembleWindow::contextMenuEvent(QContextMenuEvent *e)
{
        QMenu popup(this);
        popup.addAction(m_selectAddrAction);
        popup.addAction(m_jumpToLocation);
        popup.addAction(m_runUntilCursor);
        QMenu* disassemblyFlavorMenu = popup.addMenu(i18n("Disassembly flavor"));
        disassemblyFlavorMenu->addAction(m_disassemblyFlavorAtt);
        disassemblyFlavorMenu->addAction(m_disassemblyFlavorIntel);
        popup.exec(e->globalPos());
}
/***************************************************************************/
/***************************************************************************/
/***************************************************************************/
const QIcon DisassembleWidget::icon_ = QIcon::fromTheme("go-next");

DisassembleWidget::DisassembleWidget(CppDebuggerPlugin* plugin, QWidget *parent)
        : QWidget(parent),
        active_(false),
        lower_(0),
        upper_(0),
        address_(0),
        m_splitter(new KDevelop::AutoOrientedSplitter(this))
{
        QVBoxLayout* topLayout = new QVBoxLayout(this);

        QHBoxLayout* controlsLayout = new QHBoxLayout;

        topLayout->addLayout(controlsLayout);


    {   // initialize disasm/registers views
        topLayout->addWidget(m_splitter);

        //topLayout->setMargin(0);

        m_disassembleWindow = new DisassembleWindow(m_splitter, this);

        m_disassembleWindow->setWhatsThis(i18n("<b>Machine code display</b><p>"
                        "A machine code view into your running "
                        "executable with the current instruction "
                        "highlighted. You can step instruction by "
                        "instruction using the debuggers toolbar "
                        "buttons of \"step over\" instruction and "
                        "\"step into\" instruction."));

        m_disassembleWindow->setFont(KGlobalSettings::fixedFont());
        m_disassembleWindow->setSelectionMode(QTreeWidget::SingleSelection);
        m_disassembleWindow->setColumnCount(ColumnCount);
        m_disassembleWindow->setUniformRowHeights(true);
        m_disassembleWindow->setRootIsDecorated(false);

        m_disassembleWindow->setHeaderLabels(QStringList() << "" << i18n("Address") << i18n("Function") << i18n("Instruction"));

        m_splitter->setStretchFactor(0, 1);
        m_splitter->setContentsMargins(0, 0, 0, 0);

        m_registersManager = new RegistersManager(m_splitter);

        m_config = KSharedConfig::openConfig()->group("Disassemble/Registers View");

        QByteArray state = m_config.readEntry<QByteArray>("splitterState", QByteArray());
        if (!state.isEmpty()) {
            m_splitter->restoreState(state);
        }

    }

    setLayout(topLayout);

    setWindowIcon( QIcon::fromTheme("system-run") );
    setWindowTitle(i18n("Disassemble/Registers View"));

    KDevelop::IDebugController* pDC=KDevelop::ICore::self()->debugController();
    Q_ASSERT(pDC);

    connect(pDC,
            &KDevelop::IDebugController::currentSessionChanged,
            this, &DisassembleWidget::currentSessionChanged);

    connect(plugin, &CppDebuggerPlugin::reset, this, &DisassembleWidget::slotDeactivate);

    m_dlg = new SelectAddressDialog(this);

    // show the data if debug session is active
    KDevelop::IDebugSession* pS = pDC->currentSession();

    currentSessionChanged(pS);

    if(pS && !pS->currentAddr().isEmpty())
        slotShowStepInSource(pS->currentUrl(), pS->currentLine(), pS->currentAddr());
}

void DisassembleWidget::jumpToCursor() {
    DebugSession *s = qobject_cast<DebugSession*>(KDevelop::ICore::
            self()->debugController()->currentSession());
    if (s && s->isRunning()) {
        QString address = m_disassembleWindow->selectedItems().at(0)->text(Address);
        s->jumpToMemoryAddress(address);
    }
}

void DisassembleWidget::runToCursor(){
    DebugSession *s = qobject_cast<DebugSession*>(KDevelop::ICore::
            self()->debugController()->currentSession());
    if (s && s->isRunning()) {
        QString address = m_disassembleWindow->selectedItems().at(0)->text(Address);
        s->runUntil(address);
    }
}

void DisassembleWidget::currentSessionChanged(KDevelop::IDebugSession* s)
{
    DebugSession *session = qobject_cast<DebugSession*>(s);

    enableControls( session != NULL ); // disable if session closed

    m_registersManager->setSession(session);

    if (session) {
        connect(session, &DebugSession::showStepInSource,
                this, &DisassembleWidget::slotShowStepInSource);
        connect(session,&DebugSession::showStepInDisassemble,this, &DisassembleWidget::update);
    }
}


/***************************************************************************/

DisassembleWidget::~DisassembleWidget()
{
   m_config.writeEntry("splitterState", m_splitter->saveState());
}

/***************************************************************************/

bool DisassembleWidget::displayCurrent()
{
    if(address_ < lower_ || address_ > upper_) return false;

    bool bFound=false;
    for (int line=0; line < m_disassembleWindow->topLevelItemCount(); line++)
    {
        QTreeWidgetItem* item = m_disassembleWindow->topLevelItem(line);
        unsigned long address = item->text(Address).toULong(&ok,16);

        if (address == address_)
        {
            // put cursor at start of line and highlight the line
            m_disassembleWindow->setCurrentItem(item);
            item->setIcon(Icon, icon_);
            bFound = true;  // need to process all items to clear icons
        }
        else if(!item->icon(Icon).isNull()) item->setIcon(Icon, QIcon());
    }

    return bFound;
}

/***************************************************************************/

void DisassembleWidget::slotActivate(bool activate)
{
    qCDebug(DEBUGGERGDB) << "Disassemble widget active: " << activate;

    if (active_ != activate)
    {
        active_ = activate;
        if (active_)
        {
            updateDisassemblyFlavor();
            m_registersManager->updateRegisters();
            if (!displayCurrent())
                disassembleMemoryRegion();
        }
    }
}

/***************************************************************************/

void DisassembleWidget::slotShowStepInSource(const QUrl&, int,
        const QString& currentAddress)
{
    update(currentAddress);
}

void DisassembleWidget::updateExecutionAddressHandler(const GDBMI::ResultRecord& r)
{
    const GDBMI::Value& content = r["asm_insns"];
    const GDBMI::Value& pc = content[0];
    if( pc.hasField("address") ){
        QString addr = pc["address"].literal();
        address_ = addr.toULong(&ok,16);

        disassembleMemoryRegion(addr);
    }
}

/***************************************************************************/

void DisassembleWidget::disassembleMemoryRegion(const QString& from, const QString& to)
{
    DebugSession *s = qobject_cast<DebugSession*>(KDevelop::ICore::
            self()->debugController()->currentSession());
    if(!s || !s->isRunning()) return;

    //only get $pc
    if (from.isEmpty()){
        s->addCommand(
                    new GDBCommand(DataDisassemble, "-s \"$pc\" -e \"$pc+1\" -- 0", this, &DisassembleWidget::updateExecutionAddressHandler ) );
    }else{

        QString cmd = (to.isEmpty())?
        QString("-s %1 -e \"%1 + 256\" -- 0").arg(from ):
        QString("-s %1 -e %2+1 -- 0").arg(from).arg(to); // if both addr set

        s->addCommand(
            new GDBCommand(DataDisassemble, cmd, this, &DisassembleWidget::disassembleMemoryHandler ) );
   }
}

/***************************************************************************/

void DisassembleWidget::disassembleMemoryHandler(const GDBMI::ResultRecord& r)
{
    const GDBMI::Value& content = r["asm_insns"];
    QString currentFunction;

    m_disassembleWindow->clear();

    for(int i = 0; i < content.size(); ++i)
    {
        const GDBMI::Value& line = content[i];

        QString addr, fct, offs, inst;

        if( line.hasField("address") )   addr = line["address"].literal();
        if( line.hasField("func-name") ) fct  = line["func-name"].literal();
        if( line.hasField("offset") )    offs = line["offset"].literal();
        if( line.hasField("inst") )      inst = line["inst"].literal();

        //We use offset at the same column where function is.
        if(currentFunction == fct){
            if(!fct.isEmpty()){
                fct = QString("+") + offs;
            }
        }else { currentFunction = fct; }

        m_disassembleWindow->addTopLevelItem(new QTreeWidgetItem(m_disassembleWindow,
                                                                 QStringList() << QString() << addr << fct << inst));

        if (i == 0) {
            lower_ = addr.toULong(&ok,16);
        } else  if (i == content.size()-1) {
            upper_ = addr.toULong(&ok,16);
        }
    }

  displayCurrent();

  m_disassembleWindow->resizeColumnToContents(Icon);       // make Icon always visible
  m_disassembleWindow->resizeColumnToContents(Address);    // make entire address always visible
}


void DisassembleWidget::showEvent(QShowEvent*)
{
    slotActivate(true);

    //it doesn't work for large names of functions
//    for (int i = 0; i < m_disassembleWindow->model()->columnCount(); ++i)
//        m_disassembleWindow->resizeColumnToContents(i);
}

void DisassembleWidget::hideEvent(QHideEvent*)
{
    slotActivate(false);
}

void DisassembleWidget::slotDeactivate()
{
    slotActivate(false);
}

void DisassembleWidget::enableControls(bool enabled)
{
    m_disassembleWindow->setEnabled(enabled);
}

void DisassembleWidget::slotChangeAddress()
{
    if(!m_dlg) return;
    m_dlg->updateOkState();

    if (!m_disassembleWindow->selectedItems().isEmpty()) {
        m_dlg->setAddress(m_disassembleWindow->selectedItems().first()->text(Address));
    }

    if (m_dlg->exec() == QDialog::Rejected)
        return;

    unsigned long addr = m_dlg->address().toULong(&ok,16);

    if (addr < lower_ || addr > upper_ || !displayCurrent())
        disassembleMemoryRegion(m_dlg->address());
}

void SelectAddressDialog::setAddress ( const QString& address )
{
     m_ui.comboBox->setCurrentItem ( address, true );
}

void DisassembleWidget::update(const QString &address)
{
    if (!active_) {
        return;
    }

    address_ = address.toULong(&ok, 16);
    if (!displayCurrent()) {
        disassembleMemoryRegion();
    }
    m_registersManager->updateRegisters();
}

void DisassembleWidget::setDisassemblyFlavor(QAction* action)
{
    DebugSession* s = qobject_cast<DebugSession*>(KDevelop::ICore::
            self()->debugController()->currentSession());
    if(!s || !s->isRunning()) {
        return;
    }

    DisassemblyFlavor disassemblyFlavor = static_cast<DisassemblyFlavor>(action->data().toInt());
    QString cmd;
    switch(disassemblyFlavor)
    {
    default:
        // unknown flavor, do not build a GDB command
        break;
    case DisassemblyFlavorATT:
        cmd = QStringLiteral("disassembly-flavor att");
        break;
    case DisassemblyFlavorIntel:
        cmd = QStringLiteral("disassembly-flavor intel");
        break;
    }
    qCDebug(DEBUGGERGDB) << "Disassemble widget set " << cmd;

    if (!cmd.isEmpty()) {
        s->addCommand(
                new GDBCommand(GdbSet, cmd, this, &DisassembleWidget::setDisassemblyFlavorHandler));
    }
}

void DisassembleWidget::setDisassemblyFlavorHandler(const GDBMI::ResultRecord& r)
{
    if (r.reason == "done" && active_) {
        disassembleMemoryRegion();
    }
}

void DisassembleWidget::updateDisassemblyFlavor()
{
    DebugSession* s = qobject_cast<DebugSession*>(KDevelop::ICore::
            self()->debugController()->currentSession());
    if(!s || !s->isRunning()) {
        return;
    }

    s->addCommand(
                new GDBCommand(GdbShow, QStringLiteral("disassembly-flavor"), this, &DisassembleWidget::showDisassemblyFlavorHandler));
}

void DisassembleWidget::showDisassemblyFlavorHandler(const GDBMI::ResultRecord& r)
{
    const GDBMI::Value& value = r["value"];
    qCDebug(DEBUGGERGDB) << "Disassemble widget disassembly flavor" << value.literal();

    DisassemblyFlavor disassemblyFlavor = DisassemblyFlavorUnknown;
    if (value.literal() == "att") {
        disassemblyFlavor = DisassemblyFlavorATT;
    } else if (value.literal() == "intel") {
        disassemblyFlavor = DisassemblyFlavorIntel;
    }
    m_disassembleWindow->setDisassemblyFlavor(disassemblyFlavor);
}


}

