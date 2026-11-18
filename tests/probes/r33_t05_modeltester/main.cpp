// Standalone Qt-only reproduction; deliberately invalid modes must abort.
#include <QAbstractItemModelTester>
#include <QAbstractTableModel>
#include <QCoreApplication>
#include <QString>

class PagedTable : public QAbstractTableModel
{
public:
    int visible = 0;
    int total = 0;
    bool badDelta = false;
    int rowCount( const QModelIndex &parent = {} ) const override
    {
        return parent.isValid() ? 0 : visible;
    }
    int columnCount( const QModelIndex &parent = {} ) const override
    {
        return parent.isValid() ? 0 : 1;
    }
    QVariant data( const QModelIndex &index, int role ) const override
    {
        return index.isValid() && role == Qt::DisplayRole ? QVariant( index.row() ) : QVariant();
    }
    bool canFetchMore( const QModelIndex &parent ) const override
    {
        return !parent.isValid() && visible < total;
    }
    void fetchMore( const QModelIndex &parent ) override
    {
        if ( !canFetchMore( parent ) )
            return;
        beginInsertRows( {}, visible, visible );
        visible += badDelta ? 2 : 1;
        endInsertRows();
    }
    void populate( bool insert )
    {
        if ( insert )
            beginInsertRows( {}, 0, 0 );
        else
            beginResetModel();
        total = 4;
        visible = 1;
        if ( insert )
            endInsertRows();
        else
            endResetModel();
    }
};

int main( int argc, char **argv )
{
    QCoreApplication app( argc, argv );
    const QString mode = argc > 1 ? QString::fromLocal8Bit( argv[1] ) : QStringLiteral( "manual" );
    PagedTable model;
    QAbstractItemModelTester tester( &model, QAbstractItemModelTester::FailureReportingMode::Fatal );
    tester.setUseFetchMore( mode.startsWith( QStringLiteral( "auto" ) ) );
    model.populate( mode == QStringLiteral( "auto-insert" ) );
    model.badDelta = mode == QStringLiteral( "bad-delta" );
    while ( model.canFetchMore( {} ) )
        model.fetchMore( {} );
    return model.visible == 4 ? 0 : 1;
}
