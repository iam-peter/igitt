#include "core/MessageFormat.h"

#include <QTest>

using namespace Qt::StringLiterals;
using Trailer = MessageFormat::Trailer;

class TestMessageFormat : public QObject
{
    Q_OBJECT

private slots:
    void splitsTitleBodyAndTrailers()
    {
        const MessageFormat::Parts parts = MessageFormat::split(
            u"Fix the thing\n\nIt was broken.\n\nReally.\n\nTask-number: QTBUG-1\nChange-Id: I1234\n"_s);
        QCOMPARE(parts.title, u"Fix the thing"_s);
        QCOMPARE(parts.body, u"It was broken.\n\nReally."_s);
        QCOMPARE(parts.trailers, (QList<Trailer>{{u"Task-number"_s, u"QTBUG-1"_s}, {u"Change-Id"_s, u"I1234"_s}}));
    }

    void lastParagraphIsOnlyTrailersIfAllLinesAre()
    {
        const MessageFormat::Parts parts = MessageFormat::split(u"Title\n\nNote: this is prose\nnot a trailer"_s);
        QVERIFY(parts.trailers.isEmpty());
        QCOMPARE(parts.body, u"Note: this is prose\nnot a trailer"_s);
    }

    void titleAloneHasNoTrailers()
    {
        const MessageFormat::Parts parts = MessageFormat::split(u"Fixes: everything"_s);
        QCOMPARE(parts.title, u"Fixes: everything"_s);
        QVERIFY(parts.trailers.isEmpty());

        const MessageFormat::Parts onlyTrailers = MessageFormat::split(u"Title\n\nChange-Id: I1"_s);
        QVERIFY(onlyTrailers.body.isEmpty());
        QCOMPARE(onlyTrailers.trailers.size(), 1);
    }

    void joinsWithOneTrailerBlock()
    {
        MessageFormat::Parts parts;
        parts.title = u"  Add a feature  "_s;
        parts.body = u"\nWhy it matters.\n\n"_s;
        parts.trailers = {{u"Task-number"_s, u"QTBUG-2"_s}, {u"Fixes"_s, u""_s}, {u"Pick-to"_s, u"6.8"_s}};
        QCOMPARE(MessageFormat::join(parts),
                 u"Add a feature\n\nWhy it matters.\n\nTask-number: QTBUG-2\nPick-to: 6.8\n"_s);

        parts.body.clear();
        parts.trailers.clear();
        QCOMPARE(MessageFormat::join(parts), u"Add a feature\n"_s);
    }

    void roundTrips()
    {
        const QString message = u"Title\n\nBody line\n\n- a bullet\n\nChange-Id: Iabc\n"_s;
        QCOMPARE(MessageFormat::join(MessageFormat::split(message)), message);
    }

    void wrapsParagraphs()
    {
        QCOMPARE(MessageFormat::wrap(u"one two three four five six"_s, 10), u"one two\nthree four\nfive six"_s);
        // Rewrapping joins the lines of a paragraph first.
        QCOMPARE(MessageFormat::wrap(u"one\ntwo three\n\nfour"_s, 20), u"one two three\n\nfour"_s);
    }

    void wrapsBulletsWithHangingIndent()
    {
        QCOMPARE(MessageFormat::wrap(u"- alpha beta gamma delta\n- epsilon"_s, 14),
                 u"- alpha beta\n  gamma delta\n- epsilon"_s);
        QCOMPARE(MessageFormat::wrap(u"1. alpha beta\n   gamma"_s, 40), u"1. alpha beta gamma"_s);
    }

    void leavesCodeAndLongWordsAlone()
    {
        const QString code = u"    int  x = 1;   // keep spacing"_s;
        QCOMPARE(MessageFormat::wrap(code, 10), code);
        QCOMPARE(MessageFormat::wrap(u"see https://example.com/a/very/long/url ok"_s, 12),
                 u"see\nhttps://example.com/a/very/long/url\nok"_s);
    }

    void countsOverlongLines()
    {
        QCOMPARE(MessageFormat::overlongLines(u"12345\n123456\n1234567"_s, 5), 2);
    }
};

QTEST_GUILESS_MAIN(TestMessageFormat)
#include "tst_messageformat.moc"
