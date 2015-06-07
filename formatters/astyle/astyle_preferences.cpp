/* This file is part of KDevelop
*  Copyright (C) 2008 Cédric Pasteur <cedric.pasteur@free.fr>
Copyright (C) 2001 Matthias Hölzer-Klüpfel <mhk@caldera.de>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.

*/
#include "astyle_preferences.h"

#include "astyle_formatter.h"
#include "astyle_plugin.h"

using namespace KDevelop;

namespace {

const int INDENT_BLOCK = 0;
const int INDENT_BRACKET = 1;
const int INDENT_CASE = 2;
const int INDENT_CLASS = 3;
const int INDENT_LABEL = 4;
const int INDENT_NAMESPACE = 5;
const int INDENT_PREPROCESSOR = 6;
const int INDENT_SWITCH = 7;

const int PADDING_NOCHANGE = 0;
const int PADDING_NO = 1;
const int PADDING_IN = 2;
const int PADDING_OUT = 3;
const int PADDING_INOUT = 4;

const int INDENT_TABS = 0;
const int INDENT_TABSFORCE = 1;
const int INDENT_SPACES = 2;

const int BRACKET_NOCHANGE = 0;
const int BRACKET_ATTACH = 1;
const int BRACKET_BREAK = 2;
const int BRACKET_LINUX = 3;
const int BRACKET_STROUSTRUP = 4;
const int BRACKET_RUNINMODE = 5;

const int POINTERALIGN_NOCHANGE = 0;
const int POINTERALIGN_NAME = 1;
const int POINTERALIGN_MIDDLE = 2;
const int POINTERALIGN_TYPE = 3;

}

AStylePreferences::AStylePreferences(Language lang, QWidget *parent)
    : SettingsWidget(parent)
    , m_formatter(new AStyleFormatter)
{
    setupUi(this);
    switch(lang) {
        case AStylePreferences::CPP:
            m_formatter->setCStyle();
            break;
        case AStylePreferences::Java:
            m_formatter->setJavaStyle();
            break;
        case AStylePreferences::CSharp:
            m_formatter->setSharpStyle();
            break;
    }
    m_enableWidgetSignals = true;
    init();
}

AStylePreferences::~AStylePreferences( )
{
}

void AStylePreferences::init()
{
    // setup list widget to have checked items
    for(int i = 0; i < listIdentObjects->count(); i++) {
        QListWidgetItem *item = listIdentObjects->item(i);
        item->setFlags(Qt::ItemIsUserCheckable|Qt::ItemIsEnabled);
        item->setCheckState(Qt::Checked);
    }

    connect(tabWidget, &QTabWidget::currentChanged, this, &AStylePreferences::currentTabChanged);

    connect(cbIndentType, static_cast<void(KComboBox::*)(int)>(&KComboBox::currentIndexChanged), this, &AStylePreferences::indentChanged);
    connect(inpNuberSpaces, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &AStylePreferences::indentChanged);
    connect(chkConvertTabs, &QCheckBox::stateChanged, this, &AStylePreferences::indentChanged);
    connect(chkFillEmptyLines, &QCheckBox::stateChanged, this, &AStylePreferences::indentChanged);

    connect(listIdentObjects, &QListWidget::itemChanged,
             this, &AStylePreferences::indentObjectsChanged);

    connect(inpMaxStatement, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &AStylePreferences::minMaxValuesChanged);
    connect(inpMinConditional, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &AStylePreferences::minMaxValuesChanged);

    connect(cbBrackets, static_cast<void(KComboBox::*)(int)>(&KComboBox::currentIndexChanged), this, &AStylePreferences::bracketsChanged);
    connect(chkBracketsCloseHeaders, &QCheckBox::stateChanged, this, &AStylePreferences::bracketsChanged);

    connect(chkBlockBreak, &QCheckBox::stateChanged, this, &AStylePreferences::blocksChanged);
    connect(chkBlockBreakAll, &QCheckBox::stateChanged, this, &AStylePreferences::blocksChanged);
    connect(chkBlockIfElse, &QCheckBox::stateChanged, this, &AStylePreferences::blocksChanged);

    connect(cbParenthesisPadding, static_cast<void(KComboBox::*)(int)>(&KComboBox::currentIndexChanged), this, &AStylePreferences::paddingChanged);
    connect(chkPadParenthesisHeader, &QCheckBox::stateChanged, this, &AStylePreferences::paddingChanged);
    connect(chkPadOperators, &QCheckBox::stateChanged, this, &AStylePreferences::paddingChanged);

    connect(chkKeepStatements, &QCheckBox::stateChanged, this, &AStylePreferences::onelinersChanged);
    connect(chkKeepBlocks, &QCheckBox::stateChanged, this, &AStylePreferences::onelinersChanged);

    connect(cbPointerAlign, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &AStylePreferences::pointerAlignChanged);
}

void AStylePreferences::load(const SourceFormatterStyle &style)
{
    if(!style.content().isEmpty())
        m_formatter->loadStyle(style.content());
    else
        m_formatter->predefinedStyle(style.name());

    updateWidgets();
    updatePreviewText();
}

QString AStylePreferences::save()
{
    return m_formatter->saveStyle();
}

void AStylePreferences::updateWidgets()
{
    // block signals to avoid writing stuff to m_formatter
    m_enableWidgetSignals = false;
    //indent
    if(m_formatter->option("Fill").toString() == "Tabs") {
        chkConvertTabs->setEnabled(false);
        chkConvertTabs->setChecked(false);
        if(m_formatter->option("FillForce").toBool()) {
            cbIndentType->setCurrentIndex(INDENT_TABSFORCE);
        } else {
            cbIndentType->setCurrentIndex(INDENT_TABS);
        }
    } else {
        cbIndentType->setCurrentIndex(INDENT_SPACES);
        chkConvertTabs->setEnabled(true);
        chkConvertTabs->setChecked(m_formatter->option("FillForce").toBool());
    }
    inpNuberSpaces->setValue(m_formatter->option("FillCount").toInt());
    chkFillEmptyLines->setChecked(m_formatter->option("FillEmptyLines").toBool());

    // indent objects
    setItemChecked(INDENT_BLOCK, m_formatter->option("IndentBlocks").toBool());
    setItemChecked(INDENT_BRACKET, m_formatter->option("IndentBrackets").toBool());
    setItemChecked(INDENT_CASE, m_formatter->option("IndentCases").toBool());
    setItemChecked(INDENT_CLASS, m_formatter->option("IndentClasses").toBool());
    setItemChecked(INDENT_LABEL, m_formatter->option("IndentLabels").toBool());
    setItemChecked(INDENT_NAMESPACE, m_formatter->option("IndentNamespaces").toBool());
    setItemChecked(INDENT_PREPROCESSOR, m_formatter->option("IndentPreprocessors").toBool());
    setItemChecked(INDENT_SWITCH, m_formatter->option("IndentSwitches").toBool());

    inpMaxStatement->setValue(m_formatter->option("MaxStatement").toInt());
    inpMinConditional->setValue(m_formatter->option("MinConditional").toInt());

    // brackets
    QString s = m_formatter->option("Brackets").toString();
    if(s == "Attach")
        cbBrackets->setCurrentIndex(BRACKET_ATTACH);
    else if(s == "Break")
        cbBrackets->setCurrentIndex(BRACKET_BREAK);
    else if(s == "Linux")
        cbBrackets->setCurrentIndex(BRACKET_LINUX);
    else
        cbBrackets->setCurrentIndex(BRACKET_NOCHANGE);
    chkBracketsCloseHeaders->setChecked(
        m_formatter->option("BracketsCloseHeaders").toBool());

    // blocks
    chkBlockBreak->setChecked(m_formatter->option("BlockBreak").toBool());
    chkBlockBreakAll->setChecked(m_formatter->option("BlockBreakAll").toBool());
    chkBlockIfElse->setChecked(m_formatter->option("BlockIfElse").toBool());
    // enable or not chkBlockBreakAll
    chkBlockBreakAll->setEnabled(chkBlockBreak->isChecked());

    // padding
    bool padin = m_formatter->option("PadParenthesesIn").toBool();
    bool padout = m_formatter->option("PadParenthesesOut").toBool();
    bool unpad = m_formatter->option("PadParenthesesUn").toBool();
    if(unpad) {
        if(padin) {
            if(padout)
                cbParenthesisPadding->setCurrentIndex(PADDING_INOUT);
            else
                cbParenthesisPadding->setCurrentIndex(PADDING_IN);
        } else if(padout)
            cbParenthesisPadding->setCurrentIndex(PADDING_OUT);
        else
            cbParenthesisPadding->setCurrentIndex(PADDING_NO);
    } else
        cbParenthesisPadding->setCurrentIndex(PADDING_NOCHANGE);

    // padding header has no influence with padding out
    if (padout)
        chkPadParenthesisHeader->setDisabled(true);

    chkPadParenthesisHeader->setChecked(m_formatter->option("PadParenthesesHeader").toBool());
    chkPadOperators->setChecked(m_formatter->option("PadOperators").toBool());
    // oneliner
    chkKeepStatements->setChecked(m_formatter->option("KeepStatements").toBool());
    chkKeepBlocks->setChecked(m_formatter->option("KeepBlocks").toBool());

    // pointer align
    s = m_formatter->option("PointerAlign").toString();
    if (s == "Name")
        cbPointerAlign->setCurrentIndex(POINTERALIGN_NAME);
    else if (s == "Type")
        cbPointerAlign->setCurrentIndex(POINTERALIGN_TYPE);
    else if (s == "Middle")
        cbPointerAlign->setCurrentIndex(POINTERALIGN_MIDDLE);
    else
        cbPointerAlign->setCurrentIndex(POINTERALIGN_NOCHANGE);

    m_enableWidgetSignals = true; // re enable signals
}

void AStylePreferences::setItemChecked(int idx, bool checked)
{
    QListWidgetItem *item = listIdentObjects->item(idx);
    if(!item)
        return;

    if(checked)
        item->setCheckState(Qt::Checked);
    else
        item->setCheckState(Qt::Unchecked);
}

void AStylePreferences::updatePreviewText(bool emitChangedSignal)
{
    Q_UNUSED(emitChangedSignal);
    if(tabWidget->currentIndex() == 0)
        emit previewTextChanged(AStylePlugin::indentingSample());
    else
        emit previewTextChanged(AStylePlugin::formattingSample());
}

void AStylePreferences::currentTabChanged()
{
    updatePreviewText(false);
}

void AStylePreferences::indentChanged()
{
    if(!m_enableWidgetSignals)
        return;


    switch(cbIndentType->currentIndex()) {
        case INDENT_TABS:
            m_formatter->setTabSpaceConversionMode( false );
            m_formatter->setTabIndentation(inpNuberSpaces->value(), false);
            chkConvertTabs->setEnabled(false);
            break;
        case INDENT_TABSFORCE:
            m_formatter->setTabSpaceConversionMode( false );
            m_formatter->setTabIndentation(inpNuberSpaces->value(), true);
            chkConvertTabs->setEnabled(false);
            break;
        case INDENT_SPACES:
            m_formatter->setSpaceIndentation(inpNuberSpaces->value());
            chkConvertTabs->setEnabled(true);
            m_formatter->setTabSpaceConversionMode( chkConvertTabs->isEnabled() & chkConvertTabs->isChecked() );
            break;
    }

    m_formatter->setFillEmptyLines( chkFillEmptyLines->isChecked() );

    updatePreviewText();
}

void AStylePreferences::indentObjectsChanged(QListWidgetItem *item)
{
    if(!m_enableWidgetSignals)
        return;
    if(!item)
        return;

    bool checked = (item->checkState() == Qt::Checked);
    switch(listIdentObjects->row(item)) {
        case INDENT_BLOCK: m_formatter->setBlockIndent(checked); break;
        case INDENT_BRACKET: m_formatter->setBracketIndent(checked); break;
        case INDENT_CASE: m_formatter->setCaseIndent(checked); break;
        case INDENT_CLASS: m_formatter->setClassIndent(checked); break;
        case INDENT_LABEL: m_formatter->setLabelIndent(checked); break;
        case INDENT_NAMESPACE: m_formatter->setNamespaceIndent(checked); break;
        case INDENT_PREPROCESSOR: m_formatter->setPreprocessorIndent(checked); break;
        case INDENT_SWITCH: m_formatter->setSwitchIndent(checked); break;
    }

    updatePreviewText();
}

void AStylePreferences::minMaxValuesChanged()
{
    if(!m_enableWidgetSignals)
        return;
    m_formatter->setMaxInStatementIndentLength(inpMaxStatement->value());
    m_formatter->setMinConditionalIndentLength(inpMinConditional->value());

    updatePreviewText();
}

void AStylePreferences::bracketsChanged()
{
    if(!m_enableWidgetSignals)
        return;
    switch(cbBrackets->currentIndex()) {
        case BRACKET_NOCHANGE: m_formatter->setBracketFormatMode(astyle::NONE_MODE); break;
        case BRACKET_ATTACH: m_formatter->setBracketFormatMode(astyle::ATTACH_MODE); break;
        case BRACKET_BREAK: m_formatter->setBracketFormatMode(astyle::BREAK_MODE); break;
        case BRACKET_LINUX: m_formatter->setBracketFormatMode(astyle::LINUX_MODE); break;
        case BRACKET_STROUSTRUP: m_formatter->setBracketFormatMode(astyle::STROUSTRUP_MODE); break;
        case BRACKET_RUNINMODE: m_formatter->setBracketFormatMode(astyle::RUN_IN_MODE); break;
    }

    m_formatter->setBreakClosingHeaderBracketsMode(chkBracketsCloseHeaders->isChecked());

    updatePreviewText();
}

void AStylePreferences::blocksChanged()
{
    if(!m_enableWidgetSignals)
        return;
    m_formatter->setBreakBlocksMode(chkBlockBreak->isChecked());
    m_formatter->setBreakClosingHeaderBlocksMode(chkBlockBreakAll->isChecked());
    m_formatter->setBreakElseIfsMode(chkBlockIfElse->isChecked());

    chkBlockBreakAll->setEnabled(chkBlockBreak->isChecked());

    updatePreviewText();
}

void AStylePreferences::paddingChanged()
{
    if(!m_enableWidgetSignals)
        return;
    switch(cbParenthesisPadding->currentIndex()) {
        case PADDING_NOCHANGE:
            m_formatter->setParensUnPaddingMode(false);
            m_formatter->setParensInsidePaddingMode(false);
            m_formatter->setParensOutsidePaddingMode(false);
            chkPadParenthesisHeader->setDisabled(false);
            break;
        case PADDING_NO:
            m_formatter->setParensUnPaddingMode(true);
            m_formatter->setParensInsidePaddingMode(false);
            m_formatter->setParensOutsidePaddingMode(false);
            chkPadParenthesisHeader->setDisabled(false);
            break;
        case PADDING_IN:
            m_formatter->setParensUnPaddingMode(true);
            m_formatter->setParensInsidePaddingMode(true);
            m_formatter->setParensOutsidePaddingMode(false);
            chkPadParenthesisHeader->setDisabled(false);
            break;
        case PADDING_OUT:
            m_formatter->setParensUnPaddingMode(true);
            m_formatter->setParensInsidePaddingMode(false);
            m_formatter->setParensOutsidePaddingMode(true);
            // padding header has no influence with padding out
            chkPadParenthesisHeader->setDisabled(true);
            break;
        case PADDING_INOUT:
            m_formatter->setParensUnPaddingMode(true);
            m_formatter->setParensInsidePaddingMode(true);
            m_formatter->setParensOutsidePaddingMode(true);
            // padding header has no influence with padding out
            chkPadParenthesisHeader->setDisabled(true);
            break;
    }

    m_formatter->setParensHeaderPaddingMode(chkPadParenthesisHeader->isChecked());
    m_formatter->setOperatorPaddingMode(chkPadOperators->isChecked());

    updatePreviewText();
}

void AStylePreferences::onelinersChanged()
{
    if(!m_enableWidgetSignals)
        return;
    m_formatter->setSingleStatementsMode(!chkKeepStatements->isChecked());
    m_formatter->setBreakOneLineBlocksMode(!chkKeepBlocks->isChecked());

    updatePreviewText();
}

void AStylePreferences::pointerAlignChanged()
{
    if(!m_enableWidgetSignals)
        return;
    switch (cbPointerAlign->currentIndex()) {
        case POINTERALIGN_NAME:
            m_formatter->setPointerAlignment(astyle::PTR_ALIGN_NAME);
            break;
        case POINTERALIGN_TYPE:
            m_formatter->setPointerAlignment(astyle::PTR_ALIGN_TYPE);
            break;
        case POINTERALIGN_MIDDLE:
            m_formatter->setPointerAlignment(astyle::PTR_ALIGN_MIDDLE);
            break;
        default:
        case POINTERALIGN_NOCHANGE:
            m_formatter->setPointerAlignment(astyle::PTR_ALIGN_NONE);
            break;
    }

    updatePreviewText();
}

