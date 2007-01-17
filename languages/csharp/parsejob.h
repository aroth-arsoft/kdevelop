/*
 * This file is part of KDevelop
 *
 * Copyright (c) 2006 Adam Treat <treat@kde.org>
 * Copyright (c) 2006 Jakob Petsovits <jpetso@gmx.at>
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

#ifndef CSHARP_PARSEJOB_H
#define CSHARP_PARSEJOB_H

#include <kurl.h>
#include <kdevparsejob.h>

// from the parser subdirectory
#include <csharp_ast.h>
#include <csharp_codemodel.h>

class Koncrete::CodeModel;
class CSharpLanguageSupport;

namespace csharp
{

class ParseSession;


class ParseJob : public Koncrete::ParseJob
{
    Q_OBJECT

public:
    ParseJob( const KUrl &url, CSharpLanguageSupport* parent );
    ParseJob( Koncrete::Document* document, CSharpLanguageSupport* parent );

    virtual ~ParseJob();

    CSharpLanguageSupport* csharp() const;

    ParseSession* parseSession() const;

    bool wasReadFromDisk() const;

    virtual Koncrete::AST *AST() const;
    virtual Koncrete::CodeModel *codeModel() const;

protected:
    virtual void run();

private:
    ParseSession *m_session;
    compilation_unit_ast *m_AST;
    CodeModel *m_model;
    bool m_readFromDisk;
};

} // end of namespace csharp

#endif

// kate: space-indent on; indent-width 4; tab-width 4; replace-tabs on
