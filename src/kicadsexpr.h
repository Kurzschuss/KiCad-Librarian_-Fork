/*
 * Small, format-preserving S-expression scanner for modern KiCad files.
 *
 * The scanner deliberately does not build a semantic KiCad model.  It only
 * records source ranges and immediate atoms, allowing callers to edit the
 * minimum possible part of a file while retaining fields introduced by newer
 * KiCad versions.
 */
#ifndef KICADSEXPR_H
#define KICADSEXPR_H

#include <wx/arrstr.h>
#include <wx/string.h>
#include <vector>

struct KiCadSexprAtom {
    size_t start;
    size_t end;       /* one past the last source character */
    wxString value;   /* unquoted value */
    bool quoted;
};

struct KiCadSexprNode {
    size_t start;
    size_t end;       /* one past the closing parenthesis */
    int parent;
    wxString head;
    std::vector<KiCadSexprAtom> atoms;
};

bool ParseKiCadSexpr(const wxString& text, std::vector<KiCadSexprNode>* nodes);
int FindKiCadSexprRoot(const std::vector<KiCadSexprNode>& nodes, const wxString& head);
wxString QuoteKiCadString(const wxString& value);
void ReplaceKiCadRange(wxString* text, size_t start, size_t end, const wxString& replacement);
void KiCadTextToLines(const wxString& text, wxArrayString* lines);
wxString KiCadLinesToText(const wxArrayString& lines);
bool ReadKiCadTextFile(const wxString& path, wxString* text);
bool WriteKiCadTextFile(const wxString& path, const wxString& text);

#endif /* KICADSEXPR_H */
