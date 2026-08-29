#pragma once
// ANTIGRAVITY: QScintilla stub — minimal surface for QgsCodeEditor to compile
#include "qsciscintillabase.h"
#include "qscilexer.h"
#include <QColor>
#include <QFont>
#include <QString>

class QsciAPIs;
class QSCINTILLA_EXPORT QsciScintilla : public QsciScintillaBase
{
    Q_OBJECT
public:
    explicit QsciScintilla(QWidget *parent = nullptr) : QsciScintillaBase(parent) {}
    virtual ~QsciScintilla();

    // Named enums (used as qualified types: QsciScintilla::WrapMode etc.)
    enum WrapMode { WrapNone = 0, WrapWord = 1, WrapCharacter = 2, WrapWhitespace = 3 };
    enum WrapVisualFlag { WrapFlagNone = 0, WrapFlagByText = 1, WrapFlagByBorder = 2, WrapFlagInMargin = 3 };
    enum EdgeMode { EdgeNone = 0, EdgeLine = 1, EdgeBackground = 2 };
    enum EolMode { EolWindows = 0, EolUnix = 1, EolMac = 2 };
    enum WhitespaceVisibility { WsInvisible = 0, WsVisible = 1, WsVisibleAfterIndent = 2 };
    enum FoldStyle { NoFoldStyle = -1, PlainFoldStyle = 1, CircledFoldStyle = 2,
                     BoxedFoldStyle = 0, CircledTreeFoldStyle = 4, BoxedTreeFoldStyle = 3,
                     ArrowFoldStyle = 5, ArrowTreeFoldStyle = 6, FlatTreeFoldStyle = 7,
                     RightArrowFoldStyle = 8 };
    enum BraceMatch { NoBraceMatch = 0, StrictBraceMatch = 1, SloppyBraceMatch = 2 };
    enum AnnotationDisplay { AnnotationHidden = 0, AnnotationStandard = 1, AnnotationBoxed = 2 };
    enum AutoCompletionSource { AcsNone = 0, AcsAll = 1, AcsDocument = 2, AcsAPIs = 3 };
    enum AutoCompletionUseSingle { AcusNever = 0, AcusExplicit = 1, AcusAlways = 2 };
    enum CallTipsStyle { CallTipsNone = 0, CallTipsNoContext = 1,
                         CallTipsNoAutoCompletionContext = 2, CallTipsContext = 3 };
    enum CallTipsPosition { CallTipsBelowText = 0, CallTipsAboveText = 1 };
    enum MarkerSymbol { Circle = 0, Rectangle = 1, RightTriangle = 2,
                        SmallRectangle = 3, RightArrow = 4, Invisible = 5,
                        DownTriangle = 6, Minus = 7, Plus = 8,
                        SC_MARK_CIRCLE = Circle };
    enum IndicatorStyle {
        PlainIndicator = 0, SquiggleIndicator = 1, TTIndicator = 2,
        DiagonalIndicator = 3, StrikeIndicator = 4, HiddenIndicator = 5,
        BoxIndicator = 6, RoundBoxIndicator = 7, StraightBoxIndicator = 8,
        DashIndicator = 9, DotsIndicator = 10, SquiggleLowIndicator = 11,
        DotBoxIndicator = 12, SquigglePixmapIndicator = 13,
        ThickCompositionIndicator = 14, ThinCompositionIndicator = 15,
        FullBoxIndicator = 16, TextColorIndicator = 17,
        TriangleIndicator = 18, TriangleCharacterIndicator = 19
    };

    enum {
        AiMaintain = 0x01, AiOpening = 0x02, AiClosing = 0x04,
        FoldFlagNone = 0x00,
        INDIC_PLAIN = 0, INDIC_SQUIGGLE = 1, INDIC_TT = 2, INDIC_DIAGONAL = 3,
        INDIC_STRIKE = 4, INDIC_HIDDEN = 5, INDIC_BOX = 6, INDIC_ROUNDBOX = 7,
        INDIC_STRAIGHTBOX = 8, INDIC_FULLBOX = 16, INDIC_DASH = 9, INDIC_DOTS = 10,
        INDIC_SQUIGGLELOW = 11, INDIC_DOTBOX = 12, INDIC_SQUIGGLEPIXMAP = 13,
        INDIC_COMPOSITIONTHICK = 14, INDIC_COMPOSITIONTHIN = 15, INDIC_TEXTFORE = 17,
        INDIC_POINT = 18, INDIC_POINTCHARACTER = 19,
        SC_MARK_ROUNDRECT = 1, SC_MARK_ARROW = 2,
        SC_MARK_SMALLRECT = 3, SC_MARK_SHORTARROW = 4, SC_MARK_EMPTY = 5,
        SC_MARK_ARROWDOWN = 6, SC_MARK_MINUS = 7, SC_MARK_PLUS = 8,
        SC_MARK_VLINE = 9, SC_MARK_LCORNER = 10, SC_MARK_TCORNER = 11,
        SC_MARK_BOXPLUS = 12, SC_MARK_BOXPLUSCONNECTED = 13, SC_MARK_BOXMINUS = 14,
        SC_MARK_BOXMINUSCONNECTED = 15, SC_MARK_LCORNERCURVE = 16,
        SC_MARK_TCORNERCURVE = 17, SC_MARK_CIRCLEPLUS = 18,
        SC_MARK_CIRCLEPLUSCONNECTED = 19, SC_MARK_CIRCLEMINUS = 20,
        SC_MARK_CIRCLEMINUSCONNECTED = 21, SC_MARK_BACKGROUND = 22,
        SC_MARK_DOTDOTDOT = 23, SC_MARK_ARROWS = 24, SC_MARK_PIXMAP = 25,
        SC_MARK_FULLRECT = 26, SC_MARK_LEFTRECT = 27, SC_MARK_AVAILABLE = 28,
        SC_MARK_UNDERLINE = 29, SC_MARK_RGBAIMAGE = 30, SC_MARK_BOOKMARK = 31,
        SC_MARK_CHARACTER = 10000,
        MARKER_MAX = 31,
    };

    // Text/edit
    virtual void setText(const QString &text) { m_text = text; emit textChanged(); }
    virtual QString text() const { return m_text; }
    virtual QString text(int line) const {
        QStringList lines = m_text.split(QLatin1Char('\n'));
        if (line >= 0 && line < lines.size()) return lines[line];
        return {};
    }
    virtual bool isModified() const { return m_modified; }
    virtual void setModified(bool m) { m_modified = m; emit modificationChanged(m); }
    virtual void append(const QString &s) { m_text += s; emit textChanged(); }
    virtual int length() const { return m_text.length(); }
    virtual int lines() const { return m_text.isEmpty() ? 0 : m_text.count(QLatin1Char('\n')) + 1; }
    virtual int lineLength(int line) const { return text(line).length(); }
    // Position-accurate editing (#651): the stub models text as a QString and
    // keeps a linear selection range over it, implementing the contracts the
    // live QgsCodeEditor features rely on (toggleComment's
    // setSelection + removeSelectedText, insertText's replaceSelectedText and
    // insertAt). Lines and columns are 0-based, matching the QScintilla
    // line/index convention used at those call sites. The previous stub
    // appended at the end / no-op'd / clobbered the buffer, so
    // ToggleComment corrupted editor content.
    virtual void insertAt(const QString &s, int line, int index)
    {
        const int pos = positionFromLineIndex( line, index );
        if ( pos < 0 )
            m_text += s; // out-of-range target: keep the legacy append fallback
        else
            m_text.insert( pos, s );
        emit textChanged();
    }
    virtual void remove(int pos, int length)
    {
        if ( pos < 0 || length <= 0 || pos >= m_text.length() )
            return;
        m_text.remove( pos, qMin( length, m_text.length() - pos ) );
        emit textChanged();
    }
    virtual void replaceSelectedText(const QString &s)
    {
        if ( !hasSelectedText() )
            return;
        m_text.replace( m_selStart, m_selEnd - m_selStart, s );
        clearSelections();
        emit textChanged();
    }
    virtual bool hasSelectedText() const
    {
        return m_selStart < m_selEnd && m_selEnd <= m_text.length();
    }
    virtual QString selectedText() const
    {
        if ( !hasSelectedText() )
            return {};
        return m_text.mid( m_selStart, m_selEnd - m_selStart );
    }

    // Selection / cursor
    virtual void getCursorPosition(int *line, int *index) const
    {
        // Always defined: the previous stub left caller outputs uninitialized.
        if ( line ) *line = -1;
        if ( index ) *index = -1;
    }
    virtual void setCursorPosition(int, int) {}
    virtual void getSelection(int *lineFrom, int *indexFrom, int *lineTo, int *indexTo) const
    {
        if ( !hasSelectedText() )
        {
            if ( lineFrom ) *lineFrom = -1;
            if ( indexFrom ) *indexFrom = -1;
            if ( lineTo ) *lineTo = -1;
            if ( indexTo ) *indexTo = -1;
            return;
        }
        lineIndexFromPosition( m_selStart, lineFrom, indexFrom );
        lineIndexFromPosition( m_selEnd, lineTo, indexTo );
    }
    virtual void setSelection(int lineFrom, int indexFrom, int lineTo, int indexTo)
    {
        const int start = positionFromLineIndex( lineFrom, indexFrom );
        const int end = positionFromLineIndex( lineTo, indexTo );
        if ( start < 0 || end < 0 )
        {
            clearSelections();
            return;
        }
        m_selStart = qMin( start, end );
        m_selEnd = qMax( start, end );
    }
    virtual void selectAll(bool = true) {}
    virtual void clearSelections() { m_selStart = 0; m_selEnd = 0; }

    // Removal/undo contracts exercised by QgsCodeEditor's comment/replace
    // flows. The stub keeps no cursor model: insert() appends (the sole live
    // call path empties the buffer via selectAll()+removeSelectedText()
    // first, so position is irrelevant there).
    virtual void removeSelectedText()
    {
        if ( !hasSelectedText() )
            return;
        m_text.remove( m_selStart, m_selEnd - m_selStart );
        clearSelections();
        emit textChanged();
    }
    virtual void insert(const QString &s) { m_text += s; emit textChanged(); }
    virtual void beginUndoAction() {}
    virtual void endUndoAction() {}

    // Margins
    virtual void setMarginsFont(const QFont &) {}
    virtual void setMarginsBackgroundColor(const QColor &) {}
    virtual void setMarginsForegroundColor(const QColor &) {}
    virtual void setMarginWidth(int, int) {}
    virtual void setMarginWidth(int, const QString &) {}
    virtual void setMarginType(int, int) {}
    virtual void setMarginLineNumbers(int, bool) {}
    virtual void setMarginMarkerMask(int, int) {}
    virtual void setMarginSensitivity(int, bool) {}
    virtual int marginWidth(int) const { return 0; }

    // Markers
    virtual int markerDefine(MarkerSymbol, int = -1) { return 0; }
    virtual int markerDefine(const QPixmap &, int = -1) { return 0; }
    virtual void markerSetBackground(int, const QColor &) {}
    virtual void markerSetForeground(int, const QColor &) {}
    virtual int markerAdd(int, int) { return 0; }
    virtual void markerDelete(int, int = -1) {}
    virtual void markerDeleteAll(int = -1) {}
    virtual unsigned int markers(int) const { return 0; }
    virtual int markerLine(int) const { return 0; }
    virtual int markerFindNext(int, unsigned int) const { return 0; }
    virtual int markerFindPrevious(int, unsigned int) const { return 0; }

    // Indicators
    virtual void indicatorDefine(IndicatorStyle, int = -1) {}
    virtual void setIndicatorForegroundColor(const QColor &, int = -1) {}
    virtual void setIndicatorOutlineColor(const QColor &, int = -1) {}
    virtual void setIndicatorDrawUnder(bool, int = -1) {}
    virtual void setIndicatorAlpha(int, int = -1) {}
    virtual void setIndicatorOutlineAlpha(int, int = -1) {}
    virtual void fillIndicatorRange(int, int, int, int, int) {}
    virtual void clearIndicatorRange(int, int, int, int, int) {}

    // Lexer / syntax
    virtual QsciLexer *lexer() const { return nullptr; }
    virtual void setLexer(QsciLexer * = nullptr) {}
    virtual void recolor(int = 0, int = -1) {}

    // Fold
    virtual void setFolding(FoldStyle, int = 2) {}
    virtual void foldLine(int) {}
    virtual bool foldCaseSensitive() const { return false; }
    virtual void setFoldCaseSensitive(bool) {}

    // Auto indent/complete
    virtual void setAutoIndent(bool) {}
    virtual bool autoIndent() const { return false; }
    virtual void setIndentationsUseTabs(bool) {}
    virtual bool indentationsUseTabs() const { return false; }
    virtual void setTabWidth(int) {}
    virtual int tabWidth() const { return 4; }
    virtual void setIndentationWidth(int) {}
    virtual int indentationWidth() const { return 0; }
    virtual void setAutoCompletionSource(AutoCompletionSource) {}
    virtual void setAutoCompletionThreshold(int) {}
    virtual void setAutoCompletionCaseSensitivity(bool) {}
    virtual void setAutoCompletionReplaceWord(bool) {}
    virtual void setAutoCompletionUseSingle(AutoCompletionUseSingle) {}
    virtual void setAutoCompletionWordSeparators(const QStringList &) {}
    virtual void setCallTipsStyle(CallTipsStyle) {}
    virtual void setCallTipsVisible(int) {}
    virtual void setCallTipsPosition(CallTipsPosition) {}
    virtual void callTip() {}

    // Colors/fonts
    virtual void setColor(const QColor &) {}
    virtual QColor color() const { return {}; }
    virtual void setPaper(const QColor &) {}
    virtual QColor paper() const { return {}; }
    virtual void setFont(const QFont &) {}
    virtual QFont font() const { return {}; }
    virtual void setDefaultFont(const QFont &) {}
    virtual void setSelectionForegroundColor(const QColor &) {}
    virtual void setSelectionBackgroundColor(const QColor &) {}
    virtual void setCaretForegroundColor(const QColor &) {}
    virtual void setCaretLineBackgroundColor(const QColor &) {}
    virtual void setCaretLineVisible(bool) {}
    virtual void setCaretWidth(int) {}
    virtual void setMatchedBraceBackgroundColor(const QColor &) {}
    virtual void setMatchedBraceForegroundColor(const QColor &) {}
    virtual void setUnmatchedBraceBackgroundColor(const QColor &) {}
    virtual void setUnmatchedBraceForegroundColor(const QColor &) {}

    // Wrapping
    virtual void setWrapMode(WrapMode) {}
    virtual WrapMode wrapMode() const { return WrapNone; }
    virtual void setWrapVisualFlags(WrapVisualFlag, WrapVisualFlag = WrapFlagNone, int = 0) {}

    // Whitespace/eol
    virtual void setWhitespaceVisibility(WhitespaceVisibility) {}
    virtual void setWhitespaceSize(int) {}
    virtual void setWhitespaceForegroundColor(const QColor &) {}
    virtual void setWhitespaceBackgroundColor(const QColor &) {}
    virtual void setEolMode(EolMode) {}
    virtual EolMode eolMode() const { return EolUnix; }
    virtual void setEolVisibility(bool) {}
    virtual void convertEols(EolMode) {}

    // Find/Replace
    virtual bool findFirst(const QString &, bool, bool, bool, bool, bool = true, int = -1, int = -1, bool = true, bool = false, int = -1) { return false; }
    virtual bool findFirstInSelection(const QString &, bool, bool, bool, bool, bool = true, bool = false, int = -1) { return false; }
    virtual bool findNext() { return false; }
    virtual void replace(const QString &) {}

    // Misc
    virtual void setReadOnly(bool) {}
    virtual bool isReadOnly() const { return false; }
    virtual void undo() {}
    virtual void redo() {}
    virtual void copy() {}
    virtual void cut() {}
    virtual void paste() {}
    virtual void clear() { m_text.clear(); emit textChanged(); }
    virtual void zoomIn(int = 1) {}
    virtual void zoomOut(int = 1) {}
    virtual void zoomTo(int) {}
    virtual int zoom() const { return 0; }
    virtual void setAnnotationDisplay(AnnotationDisplay) {}
    virtual AnnotationDisplay annotationDisplay() const { return AnnotationHidden; }
    virtual void annotate(int, const QString &, int) {}
    virtual void clearAnnotations(int = -1) {}
    virtual void setExtraAscent(int) {}
    virtual void setExtraDescent(int) {}
    virtual void setScrollWidth(int) {}
    virtual void setScrollWidthTracking(bool) {}
    virtual void setBraceMatching(BraceMatch) {}
    virtual void setEdgeMode(EdgeMode) {}
    virtual void setEdgeColumn(int) {}
    virtual void setEdgeColor(const QColor &) {}
    virtual void setAutoCompletionFillupsEnabled(bool) {}
    virtual void setHotspotBackgroundColor(const QColor &) {}
    virtual void setHotspotForegroundColor(const QColor &) {}
    virtual void setHotspotUnderline(bool) {}
    virtual void setHotspotWrap(bool) {}
    virtual void setOverwriteMode(bool) {}
    virtual bool overwriteMode() const { return false; }
    virtual void setMultipleSelectionEnabled(bool) {}
    virtual void setAdditionalSelectionTyping(bool) {}
    virtual void setMultiPaste(int) {}
    virtual void setVirtualSpaceOptions(int) {}
    virtual void setCursorStyle(int) {}
    virtual void setFirstVisibleLine(int) {}
    virtual int firstVisibleLine() const { return 0; }
    virtual int lineAt(const QPoint &) const { return -1; }
    virtual void lineIndexFromPosition(int, int *, int *) const {}
    virtual int positionFromLineIndex(int, int) const { return 0; }
    virtual int lineEndPosition(int) const { return 0; }

    virtual QsciAPIs *apis() const { return nullptr; }

Q_SIGNALS:
    void textChanged();
    void modificationChanged(bool);
    void modificationAttempted();
    void cursorPositionChanged(int, int);
    void copyAvailable(bool);
    void selectionChanged();
    void linesChanged();
    void SCN_CHARADDED(int);
    void SCN_MODIFIED(int, int, const char *, int, int, int, int, int, int, int);
    void indicatorClicked(int, int, Qt::KeyboardModifiers);
    void indicatorReleased(int, int, Qt::KeyboardModifiers);
    void marginClicked(int, int, Qt::KeyboardModifiers);
    void marginRightClicked(int, int, Qt::KeyboardModifiers);
    void userListActivated(int, const QString &);
    void macroRecordChanged(unsigned int, int, const char *);

private:
    QString m_text;
    bool m_modified = false;
    int m_selStart = 0;
    int m_selEnd = 0;

    // Linear QString position of (line, index), or -1 when out of range.
    int positionFromLineIndex(int line, int index) const
    {
        if ( line < 0 || index < 0 )
            return -1;
        const QStringList lines = m_text.split( QLatin1Char('\n') );
        if ( line >= lines.size() || index > lines[line].length() )
            return -1;
        int pos = 0;
        for ( int i = 0; i < line; ++i )
            pos += lines[i].length() + 1; // + the newline
        return pos + index;
    }
    void lineIndexFromPosition(int pos, int *line, int *index) const
    {
        if ( line ) *line = -1;
        if ( index ) *index = -1;
        if ( pos < 0 || pos > m_text.length() )
            return;
        int l = 0;
        int lineStart = 0;
        for ( int i = 0; i < pos; ++i )
        {
            if ( m_text.at( i ) == QLatin1Char('\n') )
            {
                ++l;
                lineStart = i + 1;
            }
        }
        if ( line ) *line = l;
        if ( index ) *index = pos - lineStart;
    }
};
