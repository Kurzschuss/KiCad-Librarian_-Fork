#include "kicadsexpr.h"

#include <wx/ffile.h>
#include <wx/file.h>

static bool IsSpace(wxChar ch)
{
    return ch == wxT(' ') || ch == wxT('\t') || ch == wxT('\r') || ch == wxT('\n');
}

static wxString DecodeAtom(const wxString& text, size_t start, size_t end, bool quoted)
{
    if (!quoted)
        return text.Mid(start, end - start);

    wxString value;
    for (size_t pos = start + 1; pos + 1 < end; pos++) {
        wxChar ch = text[pos];
        if (ch == wxT('\\') && pos + 2 < end) {
            wxChar escaped = text[++pos];
            if (escaped == wxT('n'))
                value += wxT('\n');
            else if (escaped == wxT('r'))
                value += wxT('\r');
            else if (escaped == wxT('t'))
                value += wxT('\t');
            else
                value += escaped;
        } else {
            value += ch;
        }
    }
    return value;
}

bool ParseKiCadSexpr(const wxString& text, std::vector<KiCadSexprNode>* nodes)
{
    if (!nodes)
        return false;
    nodes->clear();
    std::vector<int> stack;
    size_t pos = 0;

    while (pos < text.length()) {
        wxChar ch = text[pos];
        if (IsSpace(ch)) {
            pos++;
            continue;
        }
        if (ch == wxT(';')) {
            while (pos < text.length() && text[pos] != wxT('\n'))
                pos++;
            continue;
        }
        if (ch == wxT('(')) {
            KiCadSexprNode node;
            node.start = pos;
            node.end = 0;
            node.parent = stack.empty() ? -1 : stack.back();
            nodes->push_back(node);
            stack.push_back((int)nodes->size() - 1);
            pos++;
            continue;
        }
        if (ch == wxT(')')) {
            if (stack.empty())
                return false;
            (*nodes)[stack.back()].end = pos + 1;
            stack.pop_back();
            pos++;
            continue;
        }
        if (stack.empty())
            return false;

        const size_t start = pos;
        bool quoted = (ch == wxT('"'));
        if (quoted) {
            pos++;
            bool escaped = false;
            while (pos < text.length()) {
                wxChar current = text[pos++];
                if (escaped) {
                    escaped = false;
                } else if (current == wxT('\\')) {
                    escaped = true;
                } else if (current == wxT('"')) {
                    break;
                }
            }
            if (pos > text.length() || text[pos - 1] != wxT('"'))
                return false;
        } else {
            while (pos < text.length() && !IsSpace(text[pos])
                   && text[pos] != wxT('(') && text[pos] != wxT(')') && text[pos] != wxT(';'))
            {
                pos++;
            }
        }

        KiCadSexprNode& node = (*nodes)[stack.back()];
        wxString value = DecodeAtom(text, start, pos, quoted);
        if (node.head.IsEmpty()) {
            node.head = value;
        } else {
            KiCadSexprAtom atom;
            atom.start = start;
            atom.end = pos;
            atom.value = value;
            atom.quoted = quoted;
            node.atoms.push_back(atom);
        }
    }

    return stack.empty() && !nodes->empty();
}

int FindKiCadSexprRoot(const std::vector<KiCadSexprNode>& nodes, const wxString& head)
{
    for (size_t idx = 0; idx < nodes.size(); idx++) {
        if (nodes[idx].parent < 0 && nodes[idx].head.CmpNoCase(head) == 0)
            return (int)idx;
    }
    return -1;
}

wxString QuoteKiCadString(const wxString& value)
{
    wxString quoted = value;
    quoted.Replace(wxT("\\"), wxT("\\\\"));
    quoted.Replace(wxT("\""), wxT("\\\""));
    quoted.Replace(wxT("\n"), wxT("\\n"));
    quoted.Replace(wxT("\r"), wxT("\\r"));
    quoted.Replace(wxT("\t"), wxT("\\t"));
    return wxT("\"") + quoted + wxT("\"");
}

void ReplaceKiCadRange(wxString* text, size_t start, size_t end, const wxString& replacement)
{
    if (!text || start > end || end > text->length())
        return;
    text->replace(start, end - start, replacement);
}

void KiCadTextToLines(const wxString& text, wxArrayString* lines)
{
    if (!lines)
        return;
    lines->Clear();
    size_t start = 0;
    while (start < text.length()) {
        size_t end = text.find(wxT('\n'), start);
        if (end == wxString::npos)
            end = text.length();
        wxString line = text.Mid(start, end - start);
        if (!line.IsEmpty() && line.Last() == wxT('\r'))
            line.RemoveLast();
        lines->Add(line);
        start = end + 1;
    }
    if (text.IsEmpty())
        lines->Add(wxEmptyString);
}

wxString KiCadLinesToText(const wxArrayString& lines)
{
    wxString text;
    for (size_t idx = 0; idx < lines.Count(); idx++) {
        if (idx > 0)
            text += wxT('\n');
        text += lines[idx];
    }
    return text;
}

bool ReadKiCadTextFile(const wxString& path, wxString* text)
{
    if (!text)
        return false;
    wxFFile file(path, wxT("rb"));
    return file.IsOpened() && file.ReadAll(text, wxConvUTF8);
}

bool WriteKiCadTextFile(const wxString& path, const wxString& text)
{
    wxTempFile file(path);
    if (!file.IsOpened() || !file.Write(text, wxConvUTF8))
        return false;
    return file.Commit();
}
