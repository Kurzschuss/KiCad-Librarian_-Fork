#include "kicadsexpr.h"
#include "kicadsymbolpreview.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <wx/filefn.h>
#include <wx/filename.h>
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

static int CountPreviewLines(const wxArrayString& preview, const wxString& prefix)
{
    int count = 0;
    for (size_t idx = 0; idx < preview.Count(); idx++) {
        if (preview[idx].StartsWith(prefix))
            count++;
    }
    return count;
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

    wxString previewLibrary =
        wxString(wxT("(kicad_symbol_lib\n"))
        + wxT("  (version 20251024)\n")
        + wxT("  (future_library_field (opaque \"untouched\"))\n")
        + wxT("  (symbol \"Base\"\n")
        + wxT("    (property \"Reference\" \"U\" (at 0 3.81 0)\n")
        + wxT("      (effects (font (size 1.27 1.27))))\n")
        + wxT("    (property \"Value\" \"Base\" (at 0 -3.81 0)\n")
        + wxT("      (effects (font (size 1.27 1.27))))\n")
        + wxT("    (symbol \"Base_0_1\"\n")
        + wxT("      (rectangle (start -2.54 2.54) (end 2.54 -2.54)\n")
        + wxT("        (stroke (width 0.254) (type default)) (fill (type none)))\n")
        + wxT("      (circle (center 0 0) (radius 1.27)\n")
        + wxT("        (stroke (width 0.254) (type default)) (fill (type background)))\n")
        + wxT("      (polyline (pts (xy -2.54 0) (xy 0 2.54) (xy 2.54 0))\n")
        + wxT("        (stroke (width 0.254) (type default)) (fill (type none)))\n")
        + wxT("      (arc (start 0 2.54) (mid 2.54 0) (end 0 -2.54)\n")
        + wxT("        (stroke (width 0.254) (type default)) (fill (type none)))\n")
        + wxT("      (text \"BASE\" (at 0 0 0) (effects (font (size 1.27 1.27))))\n")
        + wxT("      (future_graphic (new_field yes))\n")
        + wxT("    )\n")
        + wxT("    (symbol \"Base_1_1\"\n")
        + wxT("      (pin input line (at -5.08 0 0) (length 2.54)\n")
        + wxT("        (name \"IN\" (effects (font (size 1.27 1.27))))\n")
        + wxT("        (number \"1\" (effects (font (size 1.27 1.27)))))\n")
        + wxT("    )\n")
        + wxT("  )\n")
        + wxT("  (symbol \"Child\"\n")
        + wxT("    (extends \"Base\")\n")
        + wxT("    (property \"Value\" \"Child\" (at 0 -3.81 0)\n")
        + wxT("      (effects (font (size 1.27 1.27))))\n")
        + wxT("    (future_child_field 123)\n")
        + wxT("    (symbol \"Child_2_1\"\n")
        + wxT("      (pin output inverted (at 5.08 0 180) (length 2.54)\n")
        + wxT("        (name \"OUT\" (effects (font (size 1.27 1.27))))\n")
        + wxT("        (number \"2\" (effects (font (size 1.27 1.27)))))\n")
        + wxT("    )\n")
        + wxT("  )\n")
        + wxT(")\n");
    wxString originalPreviewLibrary = previewLibrary;
    wxArrayString preview;
    wxString previewError;
    assert(ConvertModernSymbolTextToLegacyPreview(previewLibrary, wxT("Base"),
                                                  &preview, &previewError));
    assert(CountPreviewLines(preview, wxT("X ")) == 1);
    assert(CountPreviewLines(preview, wxT("S ")) == 1);
    assert(CountPreviewLines(preview, wxT("C ")) == 1);
    assert(CountPreviewLines(preview, wxT("P ")) == 1);
    assert(CountPreviewLines(preview, wxT("A ")) == 1);
    assert(CountPreviewLines(preview, wxT("T ")) == 1);
    assert(previewLibrary == originalPreviewLibrary);

    assert(ConvertModernSymbolTextToLegacyPreview(previewLibrary, wxT("Child"),
                                                  &preview, &previewError));
    assert(CountPreviewLines(preview, wxT("X ")) == 2);
    assert(CountPreviewLines(preview, wxT("S ")) == 1);
    assert(preview.Index(wxT("DEF Child U 0 40 Y Y 2 F N")) != wxNOT_FOUND);
    assert(previewLibrary == originalPreviewLibrary);

    wxString cycleLibrary = wxT("(kicad_symbol_lib (version 20251024) ")
        wxT("(symbol \"CycleA\" (extends \"CycleB\")) ")
        wxT("(symbol \"CycleB\" (extends \"CycleA\")))");
    assert(!ConvertModernSymbolTextToLegacyPreview(cycleLibrary, wxT("CycleA"),
                                                   &preview, &previewError));
    assert(previewError.Find(wxT("Cyclic")) >= 0);
    wxString missingLibrary = wxT("(kicad_symbol_lib (version 20251024) ")
        wxT("(symbol \"Orphan\" (extends \"Missing\")))");
    assert(!ConvertModernSymbolTextToLegacyPreview(missingLibrary, wxT("Orphan"),
                                                   &preview, &previewError));
    assert(previewError.Find(wxT("Missing")) >= 0);

    wxString temporary = wxFileName::CreateTempFileName(wxT("kicad-preview"));
    assert(!temporary.IsEmpty());
    assert(wxRemoveFile(temporary));
    wxString symbolDirectory = temporary + wxT(".kicad_symdir");
    assert(wxMkdir(symbolDirectory));
    wxString baseFile = wxFileName(symbolDirectory, wxT("Base.kicad_sym")).GetFullPath();
    wxString childFile = wxFileName(symbolDirectory, wxT("Child.kicad_sym")).GetFullPath();
    assert(WriteKiCadTextFile(baseFile, previewLibrary));
    assert(WriteKiCadTextFile(childFile, previewLibrary));
    assert(LoadModernSymbolPreview(symbolDirectory, wxT("Child"), &preview, &previewError));
    assert(CountPreviewLines(preview, wxT("X ")) == 2);
    assert(wxFileName::Rmdir(symbolDirectory, wxPATH_RMDIR_RECURSIVE));

    wxString realLibrary = wxT("C:\\Program Files\\KiCad\\10.99\\share\\kicad\\symbols\\74xGxx.kicad_sym");
    if (wxFileExists(realLibrary)) {
        assert(LoadModernSymbolPreview(realLibrary, wxT("74AHC1G00"), &preview, &previewError));
        assert(CountPreviewLines(preview, wxT("X ")) >= 5);
        assert(CountPreviewLines(preview, wxT("A ")) + CountPreviewLines(preview, wxT("P ")) > 0);
        assert(LoadModernSymbolPreview(realLibrary, wxT("74AHC1G02"), &preview, &previewError));
        assert(CountPreviewLines(preview, wxT("X ")) >= 5);
        assert(CountPreviewLines(preview, wxT("A ")) + CountPreviewLines(preview, wxT("P ")) > 0);
    }
    return 0;
}
