#pragma once

#include "core/DiffParser.h"
#include "core/PatchBuilder.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

// A diff flattened into one row per displayed line, with a line selection
// that can be turned back into a patch.
class DiffModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Repository")
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(int maxLineLength READ maxLineLength NOTIFY contentChanged)

public:
    enum Kind {
        FileHeader,
        HunkHeader,
        Context,
        Added,
        Removed,
        NoNewline,
        Message,
    };
    Q_ENUM(Kind)

    enum Roles {
        KindRole = Qt::UserRole + 1,
        TextRole,
        OldLineRole,
        NewLineRole,
        SelectableRole,
        SelectedRole,
    };

    using QAbstractListModel::QAbstractListModel;

    void setFiles(const QList<FileDiff> &files);
    void setMessage(const QString &message);
    void clear();

    const QList<FileDiff> &files() const { return m_files; }
    const LineSelection &selection(int file) const { return m_selection[file]; }
    int selectedCount() const { return m_selectedCount; }
    int maxLineLength() const { return m_maxLineLength; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void toggleLine(int row);
    // Selects every changed line from `from` to `to`, inclusive.
    Q_INVOKABLE void selectRange(int from, int to);
    // Selects all changed lines of the hunk containing `row`, or deselects
    // them if they already are all selected.
    Q_INVOKABLE void toggleHunk(int row);
    // Selects exactly the changed lines of the hunk containing `row`.
    Q_INVOKABLE void selectHunk(int row);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool isRowSelected(int row) const;
    // Whether `row` belongs to a hunk, i.e. has lines one could stage.
    Q_INVOKABLE bool inHunk(int row) const;

signals:
    void selectionChanged();
    void contentChanged();

private:
    struct Row
    {
        Kind kind = Message;
        int file = -1;
        int hunk = -1;
        int line = -1;
        QString text;
        int oldLine = -1;
        int newLine = -1;

        bool selectable() const { return kind == Added || kind == Removed; }
    };

    void setSelected(int row, bool selected);
    std::pair<int, int> hunkRows(int row) const;
    bool isSelected(const Row &row) const;

    QList<FileDiff> m_files;
    QList<Row> m_rows;
    QList<LineSelection> m_selection;
    int m_selectedCount = 0;
    int m_maxLineLength = 0;
};
