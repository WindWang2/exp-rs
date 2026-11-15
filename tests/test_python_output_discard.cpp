#include <catch2/catch_test_macros.hpp>

#include "processing/framework/task_center.h"
#include "processing/framework/tool_call_dispatcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <filesystem>
#include <algorithm>
#include <iterator>

#if defined( Q_OS_WIN )
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

using sicnu::processing::ToolCallDispatcher;

namespace
{
void application()
{
  if ( QCoreApplication::instance() ) return;
  static int argc = 1;
  static char name[] = "test_python_output_discard";
  static char *argv[] = { name, nullptr };
  new QCoreApplication( argc, argv );
}

class WorkspacePolicy
{
  public:
    explicit WorkspacePolicy( const QString &path )
      : m_previous( qgetenv( "SICNU_MCP_WORKSPACE" ) ), m_wasSet( qEnvironmentVariableIsSet( "SICNU_MCP_WORKSPACE" ) )
    { qputenv( "SICNU_MCP_WORKSPACE", path.toUtf8() ); }
    ~WorkspacePolicy()
    {
      if ( m_wasSet ) qputenv( "SICNU_MCP_WORKSPACE", m_previous );
      else qunsetenv( "SICNU_MCP_WORKSPACE" );
    }
  private:
    QByteArray m_previous;
    bool m_wasSet;
};

void writeFile( const QString &path, const QByteArray &bytes = "owned staging data" )
{
  QFile file( path );
  REQUIRE( file.open( QIODevice::WriteOnly ) );
  REQUIRE( file.write( bytes ) == bytes.size() );
}

QByteArray readFile( const QString &path )
{
  QFile file( path );
  REQUIRE( file.open( QIODevice::ReadOnly ) );
  return file.readAll();
}

sicnu::AlgorithmTaskInfo nativeResult( const QString &output, const QString &stage )
{
  sicnu::AlgorithmTaskInfo info;
  info.taskId = 42;
  info.algorithmId = QStringLiteral( "python:run_script" );
  info.status = sicnu::TaskStatus::Completed;
  info.parameterMap.insert( QStringLiteral( "output" ), output );
  info.outputLayerPath = output;
  info.resultPayload["output"] = output.toStdString();
  info.resultPayload["staging_directory"] = stage.toStdString();
  info.resultPayload["staged_output"] = QDir( stage ).filePath( QStringLiteral( "output.tif" ) ).toStdString();
  return info;
}

// Windows junctions do not require the symbolic-link privilege. Exercise
// actual reparse directories even on machines without Developer Mode.
bool directoryLink( const QString &target, const QString &link )
{
#if defined( Q_OS_WIN )
  if ( !QDir().mkdir( link ) ) return false;
  const std::wstring destination = QDir::toNativeSeparators( target ).toStdWString();
  const std::wstring substitute = L"\\??\\" + destination;
  struct MountPoint
  {
    DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    WORD length = 0, reserved = 0;
    WORD substituteOffset = 0, substituteLength = 0, printOffset = 0, printLength = 0;
    WCHAR paths[2048]{};
  } buffer;
  if ( substitute.size() + destination.size() + 2 > std::size( buffer.paths ) ) return false;
  buffer.substituteLength = static_cast<WORD>( substitute.size() * sizeof( WCHAR ) );
  buffer.printOffset = static_cast<WORD>( buffer.substituteLength + sizeof( WCHAR ) );
  buffer.printLength = static_cast<WORD>( destination.size() * sizeof( WCHAR ) );
  buffer.length = static_cast<WORD>( 8 + buffer.printOffset + buffer.printLength + sizeof( WCHAR ) );
  std::copy( substitute.begin(), substitute.end(), buffer.paths );
  std::copy( destination.begin(), destination.end(), buffer.paths + substitute.size() + 1 );
  const HANDLE handle = ::CreateFileW( reinterpret_cast<LPCWSTR>( link.utf16() ), GENERIC_WRITE, 0, nullptr,
    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr );
  if ( handle == INVALID_HANDLE_VALUE ) return false;
  DWORD returned = 0;
  const bool created = ::DeviceIoControl( handle, FSCTL_SET_REPARSE_POINT, &buffer,
    static_cast<DWORD>( buffer.length + 8 ), nullptr, 0, &returned, nullptr ) != FALSE;
  ::CloseHandle( handle );
  return created;
#else
  std::error_code error;
  std::filesystem::create_directory_symlink( target.toStdString(), link.toStdString(), error );
  return !error;
#endif
}

class DirectoryLinks
{
  public:
    bool create( const QString &target, const QString &link )
    {
      if ( !directoryLink( target, link ) ) return false;
      m_paths.append( link );
      return true;
    }
    ~DirectoryLinks()
    {
      for ( const QString &path : m_paths )
      {
#if defined( Q_OS_WIN )
        ::RemoveDirectoryW( reinterpret_cast<LPCWSTR>( path.utf16() ) );
#else
        QFile::remove( path );
#endif
      }
    }
  private:
    QStringList m_paths;
};
} // namespace

TEST_CASE( "Uncommitted Python output discard removes only its native staging directory", "[agent][python][discard]" )
{
  application();
  QTemporaryDir workspace, outside;
  REQUIRE( workspace.isValid() );
  REQUIRE( outside.isValid() );
  const WorkspacePolicy policy( workspace.path() );
  QTemporaryDir stage( workspace.filePath( QStringLiteral( ".exprs-python-XXXXXX" ) ) );
  REQUIRE( stage.isValid() );
  writeFile( stage.filePath( QStringLiteral( "output.tif" ) ) );
  REQUIRE( QDir().mkpath( stage.filePath( QStringLiteral( "nested" ) ) ) );
  writeFile( stage.filePath( QStringLiteral( "nested/request.json" ) ) );
  const QString sentinel = outside.filePath( QStringLiteral( "sentinel" ) );
  writeFile( sentinel, "external sentinel" );
  const QString output = workspace.filePath( QStringLiteral( "result.tif" ) );
  const auto info = nativeResult( output, stage.path() );

  REQUIRE( ToolCallDispatcher::discardUncommittedPythonOutput( info ) );
  CHECK_FALSE( QFileInfo::exists( stage.path() ) );
  CHECK_FALSE( QFileInfo::exists( output ) );
  CHECK( readFile( sentinel ) == "external sentinel" );
  CHECK_FALSE( ToolCallDispatcher::discardUncommittedPythonOutput( info ) );
}

TEST_CASE( "Python output discard refuses mismatched ownership and final outputs", "[agent][python][discard][paths]" )
{
  application();
  QTemporaryDir workspace, outside;
  REQUIRE( workspace.isValid() );
  REQUIRE( outside.isValid() );
  const WorkspacePolicy policy( workspace.path() );
  QTemporaryDir stage( workspace.filePath( QStringLiteral( ".exprs-python-XXXXXX" ) ) );
  REQUIRE( stage.isValid() );
  const QString staged = stage.filePath( QStringLiteral( "output.tif" ) );
  writeFile( staged );
  const QString sentinel = outside.filePath( QStringLiteral( "sentinel.tif" ) );
  writeFile( sentinel, "external sentinel" );
  const QString output = workspace.filePath( QStringLiteral( "result.tif" ) );
  auto info = nativeResult( output, stage.path() );

  SECTION( "Another algorithm cannot claim a Python staging path" )
  { info.algorithmId = QStringLiteral( "rs:ndvi" ); }
  SECTION( "A running worker retains its files" )
  { info.status = sicnu::TaskStatus::Running; }
  SECTION( "The declared destination must be absolute" )
  { info.parameterMap[QStringLiteral( "output" )] = QStringLiteral( "result.tif" ); }
  SECTION( "Native output must match the declared destination" )
  { info.resultPayload["output"] = sentinel.toStdString(); }
  SECTION( "An existing final output and its contents survive" )
  { writeFile( output, "committed output" ); }
  SECTION( "The staged path cannot name an external file" )
  { info.resultPayload["staged_output"] = sentinel.toStdString(); }
  SECTION( "The staged path cannot name the final output" )
  { info.resultPayload["staged_output"] = output.toStdString(); }
  SECTION( "A stage name cannot authorize traversal through a different path" )
  {
    info.resultPayload["staging_directory"] = ( stage.path() + QStringLiteral( "/../" )
      + QFileInfo( stage.path() ).fileName() ).toStdString();
  }
  SECTION( "The stage cannot name the destination's parent" )
  { info.resultPayload["staging_directory"] = workspace.path().toStdString(); }
  SECTION( "An identically named stage outside the workspace is refused" )
  {
    const QString unrelated = outside.filePath( QStringLiteral( ".exprs-python-unrelated" ) );
    REQUIRE( QDir().mkdir( unrelated ) );
    writeFile( QDir( unrelated ).filePath( QStringLiteral( "output.tif" ) ) );
    info.resultPayload["staging_directory"] = unrelated.toStdString();
    info.resultPayload["staged_output"] = QDir( unrelated ).filePath( QStringLiteral( "output.tif" ) ).toStdString();
  }
  SECTION( "Changing the workspace root invalidates an old stage" )
  {
    const WorkspacePolicy changed( outside.path() );
    CHECK_FALSE( ToolCallDispatcher::discardUncommittedPythonOutput( info ) );
    CHECK( QFileInfo::exists( staged ) );
    CHECK( readFile( sentinel ) == "external sentinel" );
    return;
  }

  CHECK_FALSE( ToolCallDispatcher::discardUncommittedPythonOutput( info ) );
  CHECK( QFileInfo::exists( staged ) );
  CHECK( readFile( sentinel ) == "external sentinel" );
  if ( QFileInfo::exists( output ) ) CHECK( readFile( output ) == "committed output" );
}

TEST_CASE( "Python output discard refuses linked staging and descendants", "[agent][python][discard][links]" )
{
  application();
  QTemporaryDir workspace, outside;
  REQUIRE( workspace.isValid() );
  REQUIRE( outside.isValid() );
  const WorkspacePolicy policy( workspace.path() );
  QTemporaryDir stage( workspace.filePath( QStringLiteral( ".exprs-python-XXXXXX" ) ) );
  REQUIRE( stage.isValid() );
  writeFile( stage.filePath( QStringLiteral( "output.tif" ) ) );
  const QString sentinel = outside.filePath( QStringLiteral( "sentinel" ) );
  writeFile( sentinel, "external sentinel" );
  const QString output = workspace.filePath( QStringLiteral( "result.tif" ) );
  auto info = nativeResult( output, stage.path() );
  DirectoryLinks links;

  SECTION( "A directory link anywhere in the owned tree is refused" )
  {
    const QString nested = stage.filePath( QStringLiteral( "nested" ) );
    REQUIRE( QDir().mkdir( nested ) );
    REQUIRE( links.create( outside.path(), QDir( nested ).filePath( QStringLiteral( "escape" ) ) ) );
  }
  SECTION( "A file link anywhere in the owned tree is refused" )
  {
    const QString alias = stage.filePath( QStringLiteral( "external-file.lnk" ) );
    REQUIRE( QFile::link( sentinel, alias ) );
    REQUIRE( QFileInfo( alias ).isSymLink() );
  }
  SECTION( "The staging directory itself cannot be a directory link" )
  {
    const QString alias = workspace.filePath( QStringLiteral( ".exprs-python-alias" ) );
    REQUIRE( links.create( stage.path(), alias ) );
    info = nativeResult( output, alias );
  }
  SECTION( "The output parent cannot be replaced by a directory link" )
  {
    const QString alias = workspace.filePath( QStringLiteral( "parent-alias" ) );
    REQUIRE( links.create( workspace.path(), alias ) );
    info.parameterMap[QStringLiteral( "output" )] = QDir( alias ).filePath( QStringLiteral( "result.tif" ) );
    info.resultPayload["output"] = QDir( alias ).filePath( QStringLiteral( "result.tif" ) ).toStdString();
    // Use the alias for staging too: equal canonical parents cannot authorize it.
    info.resultPayload["staging_directory"] = QDir( alias ).filePath( QFileInfo( stage.path() ).fileName() ).toStdString();
    info.resultPayload["staged_output"] = QDir( alias ).filePath( QFileInfo( stage.path() ).fileName() + QStringLiteral( "/output.tif" ) ).toStdString();
  }
  SECTION( "A linked declared parent cannot borrow a regular stage's ownership" )
  {
    const QString alias = workspace.filePath( QStringLiteral( "destination-alias" ) );
    REQUIRE( links.create( workspace.path(), alias ) );
    info.parameterMap[QStringLiteral( "output" )] = QDir( alias ).filePath( QStringLiteral( "result.tif" ) );
    info.resultPayload["output"] = QDir( alias ).filePath( QStringLiteral( "result.tif" ) ).toStdString();
  }

  CHECK_FALSE( ToolCallDispatcher::discardUncommittedPythonOutput( info ) );
  CHECK( QFileInfo::exists( stage.filePath( QStringLiteral( "output.tif" ) ) ) );
  CHECK_FALSE( QFileInfo::exists( output ) );
  CHECK( readFile( sentinel ) == "external sentinel" );
}
