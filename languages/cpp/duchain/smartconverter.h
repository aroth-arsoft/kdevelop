/*
 * Copyright (c) 2006 Hamish Rodda <rodda@kde.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
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

#ifndef DUCHAIN_SMARTCONVERTER_H
#define DUCHAIN_SMARTCONVERTER_H

#include <kdevexport.h>
class DUContext;
namespace Koncrete
{
class EditorIntegrator;
class CodeHighlighting;
}

class KDEVCPPLANGUAGE_EXPORT SmartConverter
{
public:
  SmartConverter(Koncrete::EditorIntegrator* editor, Koncrete::CodeHighlighting* hl);

  void convertDUChain(DUContext* context) const;

private:
  void convertDUChainInternal(DUContext* context, bool first = false) const;

  Koncrete::EditorIntegrator* m_editor;
  Koncrete::CodeHighlighting* m_hl;
};

#endif

// kate: space-indent on; indent-width 2; replace-tabs on
