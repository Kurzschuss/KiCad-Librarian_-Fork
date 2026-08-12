#include "kicadsexpr.h"
#include "kicadsymbolpreview.h"
#include "libraryfunctions.h"
#include "librarymanager.h"
#include "libmngr_paths.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/init.h>

LibraryManagerApp* theApp = NULL;

wxString ResolveKiCadPathVariables(const wxString& path, KiCadLibraryPathKind)
{
    return path;
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
    wxInitializer initializer;
    assert(initializer.IsOk());

    wxString temporary = wxFileName::CreateTempFileName(wxT("kicad-symdir"));
    assert(!temporary.IsEmpty());
    assert(wxRemoveFile(temporary));

    wxString sourceFile = temporary + wxT(".kicad_sym");
    wxString symbolDirectory = temporary + wxT(".kicad_symdir");
    assert(wxFileName::Mkdir(symbolDirectory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL));

    wxString sourceText =
        wxString(wxT("(kicad_symbol_lib\n"))
        + wxT("  (version 20251024)\n")
        + wxT("  (symbol \"Standalone\"\n")
        + wxT("    (property \"Value\" \"Standalone\")\n")
        + wxT("    (future_standalone_field (opaque \"keep standalone\"))\n")
        + wxT("  )\n")
        + wxT("  (symbol \"Parent\"\n")
        + wxT("    (symbol \"Parent_0_1\"\n")
        + wxT("      (rectangle (start -2.54 2.54) (end 2.54 -2.54)\n")
        + wxT("        (stroke (width 0.254) (type default)) (fill (type none)))\n")
        + wxT("    )\n")
        + wxT("    (symbol \"Parent_1_1\"\n")
        + wxT("      (pin input line (at -5.08 0 0) (length 2.54)\n")
        + wxT("        (name \"IN\" (effects (font (size 1.27 1.27))))\n")
        + wxT("        (number \"1\" (effects (font (size 1.27 1.27)))))\n")
        + wxT("    )\n")
        + wxT("  )\n")
        + wxT("  (symbol \"Child\"\n")
        + wxT("    (extends \"Parent\")\n")
        + wxT("    (property \"Value\" \"Child\")\n")
        + wxT("    (future_child_field (opaque \"keep child\"))\n")
        + wxT("  )\n")
        + wxT(")\n");
    assert(WriteKiCadTextFile(sourceFile, sourceText));

    wxArrayString symbol;
    assert(LoadSymbol(sourceFile, wxT("Standalone"), wxEmptyString, false, &symbol));
    assert(!ExistSymbol(symbolDirectory, wxT("Standalone")));
    assert(InsertSymbol(symbolDirectory, wxT("Standalone"), symbol));
    wxString standaloneFile = wxFileName(symbolDirectory,
        wxT("Standalone.kicad_sym")).GetFullPath();
    assert(wxFileExists(standaloneFile));
    symbol.Clear();
    assert(LoadSymbol(symbolDirectory, wxT("Standalone"), wxEmptyString, false, &symbol));
    assert(KiCadLinesToText(symbol).Find(wxT("future_standalone_field")) >= 0);

    wxArrayString names;
    assert(GetSymbolNames(symbolDirectory, &names));
    assert(names.Index(wxT("Standalone"), false) != wxNOT_FOUND);

    assert(CopySymbolDependencies(sourceFile, symbolDirectory, wxT("Child")));
    wxString parentFile = wxFileName(symbolDirectory,
        wxT("Parent.kicad_sym")).GetFullPath();
    assert(wxFileExists(parentFile));

    assert(LoadSymbol(sourceFile, wxT("Child"), wxEmptyString, false, &symbol));
    assert(InsertSymbol(symbolDirectory, wxT("Child"), symbol));
    wxString childFile = wxFileName(symbolDirectory,
        wxT("Child.kicad_sym")).GetFullPath();
    assert(wxFileExists(childFile));
    assert(wxFileExists(parentFile));

    symbol.Clear();
    assert(LoadSymbol(symbolDirectory, wxT("Child"), wxEmptyString, false, &symbol));
    assert(KiCadLinesToText(symbol).Find(wxT("future_child_field")) >= 0);
    assert(GetSymbolNames(symbolDirectory, &names));
    assert(names.Index(wxT("Child"), false) != wxNOT_FOUND);
    assert(names.Index(wxT("Parent"), false) != wxNOT_FOUND);

    wxArrayString preview;
    wxString previewError;
    assert(LoadModernSymbolPreview(symbolDirectory, wxT("Child"),
                                   &preview, &previewError));
    assert(CountPreviewLines(preview, wxT("X ")) == 1);
    assert(CountPreviewLines(preview, wxT("S ")) == 1);

    assert(RenameSymbol(symbolDirectory, wxT("Child"), wxT("RenamedChild")));
    wxString renamedFile = wxFileName(symbolDirectory,
        wxT("RenamedChild.kicad_sym")).GetFullPath();
    assert(!wxFileExists(childFile));
    assert(wxFileExists(renamedFile));
    symbol.Clear();
    assert(LoadSymbol(symbolDirectory, wxT("RenamedChild"), wxEmptyString, false, &symbol));
    assert(KiCadLinesToText(symbol).Find(wxT("future_child_field")) >= 0);

    assert(RemoveSymbol(symbolDirectory, wxT("RenamedChild")));
    assert(!wxFileExists(renamedFile));

    assert(wxRemoveFile(sourceFile));
    assert(wxFileName::Rmdir(symbolDirectory, wxPATH_RMDIR_RECURSIVE));
    return 0;
}
