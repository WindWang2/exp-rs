/***************************************************************************
                            qgsarchive.cpp
                           ----------------

    begin                : July 07, 2017
    copyright            : (C) 2017 by Paul Blottiere
    email                : paul.blottiere@oslandia.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsarchive.h"

#include "qgsauxiliarystorage.h"
#include "qgsmessagelog.h"
#include "qgsziputils.h"

#include <QString>
#include <QSaveFile>

using namespace Qt::StringLiterals;

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QStandardPaths>
#include <QUuid>
#include <memory>

QgsArchive::QgsArchive()
  : mDir( new QTemporaryDir() )
{}

QgsArchive::QgsArchive( const QgsArchive &other )
  : mFiles( other.mFiles )
  , mDir( new QTemporaryDir() )
{}

QgsArchive &QgsArchive::operator=( const QgsArchive &other )
{
  if ( this != &other )
  {
    mFiles = other.mFiles;
    mDir = std::make_unique<QTemporaryDir>();
  }

  return *this;
}

QString QgsArchive::dir() const
{
  return mDir->path();
}

void QgsArchive::clear()
{
  mDir = std::make_unique<QTemporaryDir>();
  mFiles.clear();
}

bool QgsArchive::zip( const QString &filename )
{
  QTemporaryDir stagingDir;
  if ( !stagingDir.isValid() )
  {
    QgsMessageLog::logMessage( QObject::tr( "Unable to create temporary archive directory" ), u"QgsArchive"_s );
    return false;
  }
  const QString tempPath( stagingDir.filePath( u"project.zip"_s ) );

  // zip content
  if ( !QgsZipUtils::zip( tempPath, mFiles, true ) )
  {
    const QString err = QObject::tr( "Unable to zip content" );
    QgsMessageLog::logMessage( err, u"QgsArchive"_s );
    return false;
  }

  QString target { filename };

  // Preserve the existing symbolic-link target semantics without removing the
  // previous archive. The final replacement must succeed before it is retired.
  if ( QFile::exists( target ) )
  {
    const QFileInfo targetFileInfo( target );
    target = targetFileInfo.canonicalFilePath();
  }

#ifdef Q_OS_WIN
  // Clear temporary flag (see GH #32118)
  DWORD dwAttrs;
#ifdef UNICODE
  dwAttrs = GetFileAttributes( qUtf16Printable( tempPath ) );
  SetFileAttributes( qUtf16Printable( tempPath ), dwAttrs & ~FILE_ATTRIBUTE_TEMPORARY );
#else
  dwAttrs = GetFileAttributes( tempPath.toLocal8Bit().data() );
  SetFileAttributes( tempPath.toLocal8Bit().data(), dwAttrs & ~FILE_ATTRIBUTE_TEMPORARY );
#endif

#endif // Q_OS_WIN

  // Stage beside the destination and replace it atomically, including when the
  // system temporary directory is on another volume. Do not fall back to
  // truncating the live archive if a temporary destination cannot be created.
  QFile source( tempPath );
  QSaveFile destination( target );
  destination.setDirectWriteFallback( false );
  bool saved = source.open( QIODevice::ReadOnly ) && destination.open( QIODevice::WriteOnly );
  QByteArray buffer( 64 * 1024, Qt::Uninitialized );
  while ( saved )
  {
    const qint64 bytes = source.read( buffer.data(), buffer.size() );
    if ( bytes == 0 )
      break;
    saved = bytes > 0 && destination.write( buffer.constData(), bytes ) == bytes;
  }
  if ( saved )
    saved = destination.commit();
  if ( !saved )
  {
    destination.cancelWriting();
    const QString err = QObject::tr( "Unable to save zip file '%1'" ).arg( target );
    QgsMessageLog::logMessage( err, u"QgsArchive"_s );
    return false;
  }

  return true;
}

bool QgsArchive::unzip( const QString &filename )
{
  clear();
  return QgsZipUtils::unzip( filename, mDir->path(), mFiles );
}

void QgsArchive::addFile( const QString &file )
{
  mFiles.append( file );
}

bool QgsArchive::removeFile( const QString &file )
{
  bool rc = false;

  if ( !file.isEmpty() && mFiles.contains( file ) && QFile::exists( file ) )
    rc = QFile::remove( file );

  mFiles.removeOne( file );

  return rc;
}

QStringList QgsArchive::files() const
{
  return mFiles;
}

bool QgsArchive::exists() const
{
  return QFileInfo::exists( mDir->path() );
}

QString QgsProjectArchive::projectFile() const
{
  const auto constFiles = files();
  for ( const QString &file : constFiles )
  {
    const QFileInfo fileInfo( file );
    if ( fileInfo.suffix().compare( "qgs"_L1, Qt::CaseInsensitive ) == 0 )
      return file;
  }

  return QString();
}

bool QgsProjectArchive::unzip( const QString &filename )
{
  if ( QgsArchive::unzip( filename ) )
    return !projectFile().isEmpty();
  else
    return false;
}

bool QgsProjectArchive::clearProjectFile()
{
  return removeFile( projectFile() );
}

QString QgsProjectArchive::auxiliaryStorageFile() const
{
  const QString extension = QgsAuxiliaryStorage::extension();

  const QStringList fileList = files();
  for ( const QString &file : fileList )
  {
    const QFileInfo fileInfo( file );
    if ( fileInfo.suffix().compare( extension, Qt::CaseInsensitive ) == 0 )
      return file;
  }

  return QString();
}
