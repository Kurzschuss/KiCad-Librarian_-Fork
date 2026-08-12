#include "kicadsexpr.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>

static int FindChild(const std::vector<KiCadSexprNode>& nodes, int parent,
                     const wxString& head, const wxString& firstAtom = wxEmptyString)
{
    for (size_t idx = 0; idx < nodes.size(); idx++) {
        if (nodes[idx].parent == parent && nodes[idx].head == head
            && (firstAtom.IsEmpty()
                || (!nodes[idx].atoms.empty() && nodes[idx].atoms[0].value == firstAtom)))
        {
            return (int)idx;
        }
    }
    return -1;
}

int main()
{
    wxString symbolLibrary =
        wxString(wxT("(kicad_symbol_lib\n"))
        + wxT("  (version 20251024)\n")
        + wxT("  (future_header (opaque \"keep me\"))\n")
        + wxT("  (symbol \"Parent\" (future_symbol_field 42))\n")
        + wxT("  (symbol \"Child\"\n")
        + wxT("    (extends \"Parent\")\n")
        + wxT("    (property \"Value\" \"Child\" (future_property yes))\n")
        + wxT("  )\n")
        + wxT(")\n");

    std::vector<KiCadSexprNode> nodes;
    assert(ParseKiCadSexpr(symbolLibrary, &nodes));
    int root = FindKiCadSexprRoot(nodes, wxT("kicad_symbol_lib"));
    assert(root >= 0);
    int child = FindChild(nodes, root, wxT("symbol"), wxT("Child"));
    assert(child >= 0);
    int parent = FindChild(nodes, root, wxT("symbol"), wxT("Parent"));
    assert(parent >= 0);
    int extends = FindChild(nodes, child, wxT("extends"));
    assert(extends >= 0 && nodes[extends].atoms[0].value == wxT("Parent"));

    /* A targeted rename must not rewrite unknown surrounding expressions. */
    ReplaceKiCadRange(&symbolLibrary, nodes[child].atoms[0].start,
                      nodes[child].atoms[0].end, QuoteKiCadString(wxT("Renamed")));
    assert(symbolLibrary.Find(wxT("future_header (opaque \"keep me\")")) >= 0);
    assert(symbolLibrary.Find(wxT("future_symbol_field 42")) >= 0);
    assert(symbolLibrary.Find(wxT("future_property yes")) >= 0);

    wxString footprint =
        wxString(wxT("(footprint \"Modern\"\n"))
        + wxT("  (version 20260206)\n")
        + wxT("  (unknown_kicad10_field (nested true))\n")
        + wxT("  (model \"${KICAD10_3DMODEL_DIR}/Package.3dshapes/Body.step\")\n")
        + wxT(")\n");
    assert(ParseKiCadSexpr(footprint, &nodes));
    root = FindKiCadSexprRoot(nodes, wxT("footprint"));
    int model = FindChild(nodes, root, wxT("model"));
    assert(model >= 0);
    assert(nodes[model].atoms[0].value.EndsWith(wxT("Body.step")));

    assert(QuoteKiCadString(wxT("a\"b\\c")) == wxT("\"a\\\"b\\\\c\""));
    return 0;
}
