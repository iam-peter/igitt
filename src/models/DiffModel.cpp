#include "DiffModel.h"

#include <algorithm>

namespace {

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

void DiffModel::setFiles(const QList<FileDiff> &files, const QList<FileMarkup> &markup)
{
    beginResetModel();
    m_content = false;
    m_files = files;
    m_bodies.clear();
    m_collapsed = QList<bool>(files.size(), false);
    m_added = QList<int>(files.size(), 0);
    m_removed = QList<int>(files.size(), 0);
    m_selection.clear();
    m_selectedCount = 0;
    m_maxLineLength = 0;

    for (int f = 0; f < files.size(); ++f) {
        const FileDiff &file = files[f];
        const FileMarkup fileMarkup = markup.size() == files.size() ? markup[f] : FileMarkup();
        QList<Row> body;

        LineSelection selection;
        if (file.isBinary)
            body.append({Message, f, -1, -1, tr("Binary file")});
        else if (file.hunks.isEmpty())
            body.append({Message, f, -1, -1, tr("No content changes (mode or rename only)")});

        for (int h = 0; h < file.hunks.size(); ++h) {
            const DiffHunk &hunk = file.hunks[h];
            body.append({HunkHeader, f, h, -1, QString::fromUtf8(hunk.header())});
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
                Row row{kind, f, h, l, expandTabs(QString::fromUtf8(line.content)), line.oldLine, line.newLine,
                        fileMarkup.hunks.value(h).value(l)};
                m_maxLineLength = std::max(m_maxLineLength, int(row.text.size()));
                m_added[f] += kind == Added ? 1 : 0;
                m_removed[f] += kind == Removed ? 1 : 0;
                body.append(row);
            }
        }
        m_bodies.append(body);
        m_selection.append(selection);
    }
    rebuildRows();
    endResetModel();
    emit contentChanged();
    emit collapsedChanged();
    emit selectionChanged();
}

void DiffModel::rebuildRows()
{
    m_rows.clear();
    if (m_content) {
        m_rows = m_bodies.value(0);
        return;
    }
    for (int f = 0; f < m_files.size(); ++f) {
        m_rows.append({FileHeader, f, -1, -1, fileTitle(m_files[f])});
        if (!m_collapsed[f])
            m_rows += m_bodies[f];
    }
}

void DiffModel::toggleFile(int row)
{
    if (row < 0 || row >= m_rows.size() || m_rows[row].file < 0)
        return;
    const int file = m_rows[row].file;
    int header = row;
    while (m_rows[header].kind != FileHeader)
        --header;
    const int lines = int(m_bodies[file].size());
    if (lines > 0) {
        // Insert or remove just this file's rows, so the view keeps its place.
        if (m_collapsed[file]) {
            beginInsertRows({}, header + 1, header + lines);
            m_collapsed[file] = false;
            for (int i = 0; i < lines; ++i)
                m_rows.insert(header + 1 + i, m_bodies[file][i]);
            endInsertRows();
        } else {
            beginRemoveRows({}, header + 1, header + lines);
            m_collapsed[file] = true;
            m_rows.remove(header + 1, lines);
            endRemoveRows();
        }
    } else {
        m_collapsed[file] = !m_collapsed[file];
    }
    emit dataChanged(index(header), index(header), {CollapsedRole});
    emit collapsedChanged();
}

void DiffModel::setAllCollapsed(bool collapsed)
{
    beginResetModel();
    std::fill(m_collapsed.begin(), m_collapsed.end(), collapsed);
    rebuildRows();
    endResetModel();
    emit collapsedChanged();
}

void DiffModel::setMessage(const QString &message)
{
    beginResetModel();
    m_content = false;
    m_files.clear();
    m_bodies.clear();
    m_collapsed.clear();
    m_added.clear();
    m_removed.clear();
    m_selection.clear();
    m_rows = {Row{Message, -1, -1, -1, message}};
    m_selectedCount = 0;
    m_maxLineLength = int(message.size());
    endResetModel();
    emit contentChanged();
    emit selectionChanged();
}

void DiffModel::setContent(const QString &path, const QStringList &lines, bool truncated,
                           const QList<LineMarkup> &markup)
{
    beginResetModel();
    m_content = true;
    FileDiff file;
    file.oldPath = file.newPath = path;
    m_files = {file};
    m_collapsed = {false};
    m_added = {0};
    m_removed = {0};
    m_selection = {LineSelection()};
    m_selectedCount = 0;
    m_maxLineLength = 0;

    QList<Row> body;
    body.reserve(lines.size() + 1);
    for (int i = 0; i < lines.size(); ++i) {
        Row row{Context, 0, -1, -1, expandTabs(lines[i]), -1, i + 1, markup.value(i)};
        m_maxLineLength = std::max(m_maxLineLength, int(row.text.size()));
        body.append(row);
    }
    if (truncated)
        body.append({Message, 0, -1, -1, tr("The file is too large to show in full.")});
    m_bodies = {body};
    rebuildRows();
    endResetModel();
    emit contentChanged();
    emit collapsedChanged();
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
    case PathRole: return row.file >= 0 ? m_files[row.file].path() : QString();
    case CollapsedRole: return row.file >= 0 && m_collapsed[row.file];
    case AddedRole: return row.file >= 0 ? m_added[row.file] : 0;
    case RemovedRole: return row.file >= 0 ? m_removed[row.file] : 0;
    case MarkupRole: return QVariant::fromValue(row.markup);
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
        {PathRole, "path"},
        {CollapsedRole, "collapsed"},
        {AddedRole, "added"},
        {RemovedRole, "removed"},
        {MarkupRole, "markup"},
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

std::pair<int, int> DiffModel::hunkRows(int row) const
{
    const int file = m_rows[row].file;
    const int hunk = m_rows[row].hunk;
    int first = row;
    while (first > 0 && m_rows[first - 1].file == file && m_rows[first - 1].hunk == hunk)
        --first;
    int last = row;
    while (last + 1 < m_rows.size() && m_rows[last + 1].file == file && m_rows[last + 1].hunk == hunk)
        ++last;
    return {first, last};
}

bool DiffModel::inHunk(int row) const
{
    return row >= 0 && row < m_rows.size() && m_rows[row].hunk >= 0;
}

bool DiffModel::isRowSelected(int row) const
{
    return row >= 0 && row < m_rows.size() && isSelected(m_rows[row]);
}

void DiffModel::selectHunk(int row)
{
    if (!inHunk(row))
        return;
    const auto [first, last] = hunkRows(row);
    for (int r = 0; r < m_rows.size(); ++r)
        setSelected(r, r >= first && r <= last);
    emit selectionChanged();
}

void DiffModel::toggleHunk(int row)
{
    if (!inHunk(row))
        return;
    const auto [first, last] = hunkRows(row);

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
