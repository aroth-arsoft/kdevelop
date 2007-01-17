/* This file is part of KDevelop
    Copyright (C) 2004 Roberto Raggi <roberto@kdevelop.org>
    Copyright (C) 2006 Hamish Rodda <rodda@kde.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include <kio/netaccess.h>
#include <kio/job.h>

#include "kdevfilemanager.h"
#include "kdevproject.h"
#include "kdevprojectmodel.h"

namespace Koncrete
{
    
class FileManager::Private
{
public:
	Project* parent;
};

FileManager::FileManager(KInstance* instance, QObject *parent)
    : Plugin(instance, parent), d(new Private)
{
    Project* project = qobject_cast<Project*>( parent );
    d->parent = project;
}

FileManager::~FileManager()
{
}

Project * FileManager::project( ) const
{
    return d->parent;
}

ProjectFolderItem * FileManager::top( )
{
/*    return dynamic_cast<ProjectFolderItem*>(root());*/
    return 0;
}

bool FileManager::renameFile( ProjectFileItem* oldFile,
                                  const KUrl& newFile )
{
    KIO::Job* renameJob = KIO::rename( oldFile->url(), newFile, 0 );
    if ( !KIO::NetAccess::synchronousRun( renameJob, 0 /* window */) )
        return false;

    oldFile->setUrl( newFile );
    return true;
}

ProjectFolderItem * FileManager::addFolder( const KUrl & folder, ProjectFolderItem * parent )
{
    Q_UNUSED( folder );
    Q_UNUSED( parent );
    return 0;
}

ProjectFileItem * FileManager::addFile( const KUrl & file, ProjectFolderItem * parent )
{
    Q_UNUSED( file );
    Q_UNUSED( parent );
    return 0;
}

bool FileManager::removeFolder( ProjectFolderItem * folder )
{
    Q_UNUSED( folder ); return false;
}

bool FileManager::removeFile( ProjectFileItem * file )
{
    Q_UNUSED( file ); return false;
}

QList<ProjectFolderItem*> FileManager::parse( ProjectFolderItem * dom )
{
    Q_UNUSED( dom );
    return QList<ProjectFolderItem*>();
}

ProjectItem* FileManager::import( ProjectModel * model, const KUrl &fileName )
{
    Q_UNUSED( model ); Q_UNUSED( fileName ); return 0;
}

bool FileManager::renameFolder( ProjectFolderItem* oldFolder,
                                    const KUrl& newFolder )
{
    KIO::Job* renameJob = KIO::rename( oldFolder->url(), newFolder, 0 );
    if ( !KIO::NetAccess::synchronousRun( renameJob, 0 /* window */ ) )
        return false;

    oldFolder->setUrl( newFolder );
    return true;
}

}
#include "kdevfilemanager.moc"
//kate: space-indent on; indent-width 4; replace-tabs on; indent-mode cstyle;





