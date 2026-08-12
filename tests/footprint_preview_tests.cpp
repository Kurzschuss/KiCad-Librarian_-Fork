#include "kicadfootprintpreview.h"
#include "kicadsexpr.h"
#include "libraryfunctions.h"
#include "librarymanager.h"
#include "libmngr_paths.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <wx/filefn.h>
#include <wx/init.h>

LibraryManagerApp* theApp = NULL;

wxString ResolveKiCadPathVariables(const wxString& path, KiCadLibraryPathKind)
{
    return path;
}

static int CountLines(const wxArrayString& preview, const wxString& prefix)
{
    int count = 0;
    for (size_t idx = 0; idx < preview.Count(); idx++) {
        if (preview[idx].StartsWith(prefix))
            count++;
    }
    return count;
}

static bool Near(double value, double expected, double tolerance = 0.01)
{
    return std::fabs(value - expected) <= tolerance;
}

int main()
{
    wxInitializer initializer;
    assert(initializer.IsOk());

    wxString modern =
        wxString(wxT("(footprint \"ModernPreview\"\n"))
        + wxT("  (version 20260206)\n")
        + wxT("  (future_footprint_field (opaque \"keep me\"))\n")
        + wxT("  (property \"Reference\" \"REF**\" (at 0 -4 0)\n")
        + wxT("    (effects (font (size 1 1) (thickness 0.15))))\n")
        + wxT("  (property \"Value\" \"ModernPreview\" (at 0 8 0)\n")
        + wxT("    (effects (font (size 1 1) (thickness 0.15))))\n")
        + wxT("  (fp_line (start -4 -3) (end 4 -3)\n")
        + wxT("    (stroke (width 0.2) (type solid)) (layer \"F.SilkS\"))\n")
        + wxT("  (fp_rect (start -4 -3) (end 4 7)\n")
        + wxT("    (stroke (width 0.15) (type solid)) (fill none) (layer \"F.Fab\"))\n")
        + wxT("  (fp_circle (center 0 2) (end 1 2)\n")
        + wxT("    (stroke (width 0.12) (type solid)) (fill none) (layer \"F.SilkS\"))\n")
        + wxT("  (fp_arc (start -2 2) (mid 0 0) (end 2 2)\n")
        + wxT("    (stroke (width 0.25) (type solid)) (layer \"F.SilkS\"))\n")
        + wxT("  (fp_poly (pts (xy -2 4) (xy 0 6) (xy 2 4))\n")
        + wxT("    (stroke (width 0.1) (type solid)) (fill solid) (layer \"F.Fab\"))\n")
        + wxT("  (fp_text user \"LABEL\" (at 0 2 45)\n")
        + wxT("    (layer \"F.SilkS\") (effects (font (size 1.2 1) (thickness 0.18))))\n")
        + wxT("  (pad \"1\" smd rect (at 0 0) (size 1.5 1) (layers \"F.Cu\" \"F.Paste\" \"F.Mask\"))\n")
        + wxT("  (pad \"2\" thru_hole oval (at 2.54 0 90) (size 2 1.4)\n")
        + wxT("    (drill oval 1.1 0.7 (offset 0.1 -0.2)) (layers \"*.Cu\" \"*.Mask\"))\n")
        + wxT("  (pad \"3\" smd roundrect (at 5.08 0) (size 1.8 1.2)\n")
        + wxT("    (layers \"B.Cu\" \"B.Paste\" \"B.Mask\") (roundrect_rratio 0.25))\n")
        + wxT("  (pad \"\" np_thru_hole circle (at 0 5) (size 1.6 1.6)\n")
        + wxT("    (drill 1.6) (layers \"*.Cu\" \"*.Mask\"))\n")
        + wxT("  (pad \"4\" smd trapezoid (at 5.08 5) (size 2 1.5)\n")
        + wxT("    (rect_delta 0.4 -0.2) (layers \"F.Cu\" \"F.Paste\" \"F.Mask\"))\n")
        + wxT(")\n");
    wxString original = modern;
    wxArrayString preview;
    FootprintInfo info;
    wxString error;
    assert(ConvertModernFootprintTextToLegacyPreview(modern, &preview, &info, &error));
    assert(modern == original);
    assert(info.PadCount == 5);
    assert(info.Pads.Count() == 5);
    assert(info.Pitch > 0.01 && info.Pitch < 10.0);
    assert(CountLines(preview, wxT("$PAD")) == 5);
    assert(CountLines(preview, wxT("DS ")) >= 8);
    assert(CountLines(preview, wxT("DC ")) == 1);
    assert(CountLines(preview, wxT("DA ")) == 1);
    assert(CountLines(preview, wxT("T2 ")) == 1);
    assert(CountLines(preview, wxT("At SMD")) == 3);
    assert(CountLines(preview, wxT("At STD")) == 1);
    assert(CountLines(preview, wxT("At HOLE")) == 1);
    assert(preview.Index(wxT("DS -4 -3 4 -3 0.2 21")) != wxNOT_FOUND);
    assert(preview.Index(wxT("DA 0 2 -2 2 1800 0.25 21")) != wxNOT_FOUND);
    assert(preview.Index(wxT("Dr 1.1 0.2 0.1 O 1.1 0.7")) != wxNOT_FOUND);
    assert(preview.Index(wxT("Sh \"3\" D 1.8 1.2 0 0 0 0.25")) != wxNOT_FOUND);
    BodyInfo body;
    assert(GetBodySize(preview, &body, false, true));
    assert(body.BodyWidth >= 8.0 && body.BodyLength >= 10.0);

    wxString dipLibrary = wxT("C:\\Program Files\\KiCad\\10.99\\share\\kicad\\footprints\\Package_DIP.pretty");
    wxString dipFile = dipLibrary + wxT("\\DIP-8_W7.62mm.kicad_mod");
    if (wxFileExists(dipFile)) {
        wxArrayString footprint;
        int version = VER_INVALID;
        assert(LoadFootprint(dipLibrary, wxT("DIP-8_W7.62mm"), wxEmptyString,
                             false, &footprint, &version));
        assert(version == VER_S_EXPR);
        wxString sourceBefore = KiCadLinesToText(footprint);
        assert(ConvertModernFootprintToLegacyPreview(footprint, &preview, &info, &error));
        assert(KiCadLinesToText(footprint) == sourceBefore);
        assert(info.PadCount == 8);
        assert(info.RegPadCount == 8);
        assert(info.Pads.Count() == 8);
        assert(Near(info.Pitch, 2.54));
        assert(info.Pitch < 100.0);
        assert(info.PitchVertical);
        assert(info.PadLines == 2);
        assert(Near(info.SpanHor, 7.62));
        assert(Near(info.PadSize[0].GetX(), 1.6));
        assert(Near(info.PadSize[0].GetY(), 1.6));
        assert(Near(info.DrillSize, 0.8));
        double minimumX = info.Pads[0].GetMidX();
        double maximumX = minimumX;
        double minimumY = info.Pads[0].GetMidY();
        double maximumY = minimumY;
        for (size_t idx = 0; idx < info.Pads.Count(); idx++) {
            assert(info.Pads[idx].GetWidth() > 0.5 && info.Pads[idx].GetWidth() < 5.0);
            assert(info.Pads[idx].GetHeight() > 0.5 && info.Pads[idx].GetHeight() < 5.0);
            minimumX = std::min(minimumX, info.Pads[idx].GetMidX());
            maximumX = std::max(maximumX, info.Pads[idx].GetMidX());
            minimumY = std::min(minimumY, info.Pads[idx].GetMidY());
            maximumY = std::max(maximumY, info.Pads[idx].GetMidY());
        }
        assert(Near(maximumX - minimumX, 7.62));
        assert(Near(maximumY - minimumY, 7.62));
        assert(CountLines(preview, wxT("$PAD")) == 8);
        assert(CountLines(preview, wxT("DS ")) >= 5);
        assert(CountLines(preview, wxT("DA ")) >= 1);
        assert(GetBodySize(preview, &body, false, true));
        assert(body.BodyWidth > 5.0 && body.BodyLength > 8.0);
    }

    wxString audioLibrary = wxT("C:\\Program Files\\KiCad\\10.99\\share\\kicad\\footprints\\Audio_Module.pretty");
    wxString audioFile = audioLibrary + wxT("\\Reverb_BTDR-1H.kicad_mod");
    if (wxFileExists(audioFile)) {
        wxArrayString footprint;
        int version = VER_INVALID;
        assert(LoadFootprint(audioLibrary, wxT("Reverb_BTDR-1H"), wxEmptyString,
                             false, &footprint, &version));
        assert(ConvertModernFootprintToLegacyPreview(footprint, &preview, &info, &error));
        assert(info.PadCount > 0);
        assert(CountLines(preview, wxT("$PAD")) == info.PadCount);
        assert(CountLines(preview, wxT("DS ")) + CountLines(preview, wxT("DC "))
            + CountLines(preview, wxT("DA ")) > 0);
        assert(GetBodySize(preview, &body, false, true));
        assert(body.BodyWidth > 20.0 && body.BodyLength > 20.0);
    }
    return 0;
}
