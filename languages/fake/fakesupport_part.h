/*
 * KDevelop Fake Language Support
 *
 * Copyright (c) 2006 Adam Treat <treat@kde.org>
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

#ifndef FAKESUPPORT_PART_H
#define FAKESUPPORT_PART_H

#include <kdevlanguagesupport.h>

class FakeLanguageSupport: public Koncrete::LanguageSupport
{
    Q_OBJECT
public:
    FakeLanguageSupport( QObject *parent, const char *name,
                         const QStringList &args = QStringList() );
    virtual ~FakeLanguageSupport();

    virtual Koncrete::CodeModel *codeModel( const KUrl &url = KUrl() ) const;
    virtual Koncrete::CodeProxy *codeProxy() const;
    virtual Koncrete::CodeDelegate *codeDelegate() const;
    virtual Koncrete::CodeRepository *codeRepository() const;
    virtual Koncrete::ParseJob *createParseJob( const KUrl &url );
    virtual Koncrete::ParseJob *createParseJob( Koncrete::Document *document,
                                          KTextEditor::SmartRange *highlight );

    virtual QStringList mimeTypes() const;
};

#endif // FAKESUPPORT_PART_H

// kate: space-indent on; indent-width 4; tab-width 4; replace-tabs on
