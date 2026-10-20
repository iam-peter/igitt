#include "DiffModel.h"

#include <algorithm>

namespace {

constexpr int TabWidth = 4;

QString displayText(const QByteArray &raw)
{
    const QString text = QString::fromUtf8(raw);
    if (!text.contains(QLatin1Char('\t')))
        return text;
    QString out;
    out.reserve(text.size() + 16);
    for (const QChar ch : text) {
        if (ch == QLatin1Char('\t'))
            out.append(QString(TabWidth - out.size() % TabWidth, QLatin1Char(' ')));
        else
            out.append(ch);
    }
    return out;
}

QString fileTitle(const FileDiff &file)
{
    if (file.isNew)
        return file.newPath + QStringLiteral("  (new file)");
    if (file.isDeleted)
        return file.oldPath + QStringLiteral("  (deleted)");
    if (!file.oldPath.isEmpty() && !file.newPath.isEmpty() && file.oldPath != file.newPath)
        return file.oldPath + QStringLiteral(" → ") + file.newPath;
    return file.path();
}

} // namespace

void DiffModel::setFiles(const QList<FileDiff> &files)
{
    beginResetModel();
    m_files = files;
    m_rows.clear();
    m_selection.clear();
    m_selectedCount = 0;
    m_maxLineLength = 0;

    for (int f = 0; f < files.size(); ++f) {
        const FileDiff &file = files[f];
        m_rows.append({FileHeader, f, -1, -1, fileTitle(file)});

        LineSelection selection;
        if (file.isBinary)
            m_rows.append({Message, f, -1, -1, tr("Binary file")});
        else if (file.hunks.isEmpty())
            m_rows.append({Message, f, -1, -1, tr("No content changes (mode or rename only)")});

        for (int h = 0; h < file.hunks.size(); ++h) {
            const DiffHunk &hunk = file.hunks[h];
            m_rows.append({HunkHeader, f, h, -1, QString::fromUtf8(hunk.header())});
            selection.append(QList<bool>(hunk.lines.size(), false));
            for (int l = 0; l < hunk.lines.size(); ++l) {
                const DiffLine &line = hunk.lines[l];
                Kind kind = Context;
                switch (line.kind) {
                case DiffLine::Added: kind = Added; break;
                case DiffLine::Removed: kind = Removed; break;
                case DiffLine::NoNewline: kind = NoNewline; break;
                case DiffLine::Context: kind = Context; break;
                }
                Row row{kind, f, h, l, displayText(line.content), line.oldLine, line.newLine};
                m_maxLineLength = std::max(m_maxLineLength, int(row.text.size()));
                m_rows.append(row);
            }
        }
        m_selection.append(selection);
    }
    endResetModel();
    emit contentChanged();
    emit selectionChanged();
}

void DiffModel::setMessage(const QString &message)
{
    beginResetModel();
    m_files.clear();
    m_selection.clear();
    m_rows = {Row{Message, -1, -1, -1, message}};
    m_selectedCount = 0;
    m_maxLineLength = int(message.size());
    endResetModel();
    emit contentChanged();
    emit selectionChanged();
}

void DiffModel::clear()
{
    setFiles({});
}

int DiffModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant DiffModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const Row &row = m_rows[index.row()];
    switch (role) {
    case KindRole: return row.kind;
    case TextRole: return row.text;
    case OldLineRole: return row.oldLine;
    case NewLineRole: return row.newLine;
    case SelectableRole: return row.selectable();
    case SelectedRole: return isSelected(row);
    }
    return {};
}

QHash<int, QByteArray> DiffModel::roleNames() const
{
    return {
        {KindRole, "kind"},
        {TextRole, "text"},
        {OldLineRole, "oldLine"},
        {NewLineRole, "newLine"},
        {SelectableRole, "selectable"},
        {SelectedRole, "selected"},
    };
}

bool DiffModel::isSelected(const Row &row) const
{
    return row.selectable() && m_selection[row.file][row.hunk][row.line];
}

void DiffModel::setSelected(int row, bool selected)
{
    const Row &r = m_rows[row];
    if (!r.selectable())
        return;
    bool &flag = m_selection[r.file][r.hunk][r.line];
    if (flag == selected)
        return;
    flag = selected;
    m_selectedCount += selected ? 1 : -1;
    emit dataChanged(index(row), index(row), {SelectedRole});
}

void DiffModel::toggleLine(int row)
{
    if (row < 0 || row >= m_rows.size() || !m_rows[row].selectable())
        return;
    setSelected(row, !isSelected(m_rows[row]));
    emit selectionChanged();
}

void DiffModel::selectRange(int from, int to)
{
    if (from > to)
        std::swap(from, to);
    from = std::max(from, 0);
    to = std::min(to, int(m_rows.size()) - 1);
    for (int row = from; row <= to; ++row)
        setSelected(row, true);
    emit selectionChanged();
}

void DiffModel::toggleHunk(int row)
{
    if (row < 0 || row >= m_rows.size() || m_rows[row].hunk < 0)
        return;
    const int file = m_rows[row].file;
    const int hunk = m_rows[row].hunk;

    int first = row;
    while (first > 0 && m_rows[first - 1].file == file && m_rows[first - 1].hunk == hunk)
        --first;
    int last = row;
    while (last + 1 < m_rows.size() && m_rows[last + 1].file == file && m_rows[last + 1].hunk == hunk)
        ++last;

    bool allSelected = true;
    for (int r = first; r <= last; ++r) {
        if (m_rows[r].selectable() && !isSelected(m_rows[r]))
            allSelected = false;
    }
    for (int r = first; r <= last; ++r)
        setSelected(r, !allSelected);
    emit selectionChanged();
}

void DiffModel::clearSelection()
{
    for (int row = 0; row < m_rows.size(); ++row)
        setSelected(row, false);
    emit selectionChanged();
}
