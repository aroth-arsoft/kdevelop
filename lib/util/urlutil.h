/***************************************************************************
 *   Copyright (C) 2003 by Julian Rockey                                   *
 *   linux@jrockey.com                                                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef _URLUTIL_H_
#define _URLUTIL_H_

#include <qstring.h>
#include <kurl.h>

class URLUtil {

public:
  /**
   * Returns the filename part of a path (i.e. everything past the last slash)
   */
  static QString filename(const QString & name);
  /**
   * Returns the directory part of a path (i.e. everything up to but not including the last slash)
   */
  static QString directory(const QString & name);
  /**
   * Returns the relative path between a parent and child URL, or blank if the specified child is not a child of parent
   */
  static QString relativePath(const KURL & parent, const KURL & child, bool slashPrefix = true, bool slashSuffix = false);
  /**
   *Returns the path 'up one level' - the opposite of what filename returns
   */
  static QString upDir(const QString & path, bool slashSuffix = false);
  /**
   * 'Merges' URLs - changes a URL that starts with dest to start with source instead
   *   Example:
   *     source is /home/me/
   *     dest is /home/you/
   *     child is /home/you/dir1/file1
   *   returns /home/me/dir1/fil1
   */
  static KURL mergeURL(const KURL & source, const KURL & dest, const KURL & child);
};

#endif
