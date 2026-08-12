#include "kicadfootprintpreview.h"

#include "kicadsexpr.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

struct FootprintDocument {
    std::vector<KiCadSexprNode> nodes;
    int root;
};

struct PreviewPad {
    wxString number;
    wxString type;
    wxString shape;
    double x;
    double y;
    double angle;
    double width;
    double height;
    double drillWidth;
    double drillHeight;
    double drillOffsetX;
    double drillOffsetY;
    double deltaX;
    double deltaY;
    double roundrectRatio;
    double pasteRatio;
    bool bottomOnly;
};

struct SizeClass {
    double width;
    double height;
    double angle;
    double ratio;
    int count;
};

static bool Fail(wxString* error, const wxString& message)
{
    if (error)
        *error = message;
    return false;
}

static int FindChild(const FootprintDocument& document, int parent, const wxString& head)
{
    for (size_t idx = 0; idx < document.nodes.size(); idx++) {
        if (document.nodes[idx].parent == parent && document.nodes[idx].head == head)
            return (int)idx;
    }
    return -1;
}

static bool AtomDouble(const KiCadSexprNode& node, size_t atom, double* value)
{
    return atom < node.atoms.size() && node.atoms[atom].value.ToDouble(value);
}

static bool ChildPoint(const FootprintDocument& document, int parent,
                       const wxString& head, double* x, double* y)
{
    int child = FindChild(document, parent, head);
    return child >= 0 && AtomDouble(document.nodes[child], 0, x)
        && AtomDouble(document.nodes[child], 1, y);
}

static double ChildDouble(const FootprintDocument& document, int parent,
                          const wxString& head, double fallback = 0.0)
{
    int child = FindChild(document, parent, head);
    double value = fallback;
    if (child >= 0)
        AtomDouble(document.nodes[child], 0, &value);
    return value;
}

static wxString Number(double value)
{
    if (std::fabs(value) < 1e-12)
        value = 0.0;
    return wxString::Format(wxT("%.10g"), value);
}

static wxString LegacyQuoted(const wxString& value)
{
    wxString escaped = value;
    escaped.Replace(wxT("\""), wxT("'"));
    return wxT("\"") + escaped + wxT("\"");
}

static bool ParseDocument(const wxString& text, FootprintDocument* document)
{
    document->nodes.clear();
    if (!ParseKiCadSexpr(text, &document->nodes))
        return false;
    document->root = FindKiCadSexprRoot(document->nodes, wxT("footprint"));
    if (document->root < 0)
        document->root = FindKiCadSexprRoot(document->nodes, wxT("module"));
    return document->root >= 0;
}

static double StrokeWidth(const FootprintDocument& document, int primitive)
{
    int stroke = FindChild(document, primitive, wxT("stroke"));
    int width = stroke >= 0 ? FindChild(document, stroke, wxT("width")) : -1;
    double value = 0.15;
    if (width >= 0)
        AtomDouble(document.nodes[width], 0, &value);
    else {
        width = FindChild(document, primitive, wxT("width"));
        if (width >= 0)
            AtomDouble(document.nodes[width], 0, &value);
    }
    return value;
}

static double NormalizeRadians(double angle)
{
    const double full = 2.0 * 3.14159265358979323846;
    while (angle < 0.0)
        angle += full;
    while (angle >= full)
        angle -= full;
    return angle;
}

static double CounterClockwise(double from, double to)
{
    double delta = NormalizeRadians(to) - NormalizeRadians(from);
    return delta < 0.0 ? delta + 2.0 * 3.14159265358979323846 : delta;
}

static bool ArcGeometry(const FootprintDocument& document, int primitive,
                        double* cx, double* cy, double* startX, double* startY,
                        double* sweepDegrees)
{
    double sx, sy, mx, my, ex, ey;
    if (!ChildPoint(document, primitive, wxT("start"), &sx, &sy)
        || !ChildPoint(document, primitive, wxT("mid"), &mx, &my)
        || !ChildPoint(document, primitive, wxT("end"), &ex, &ey))
    {
        return false;
    }
    double denominator = 2.0 * (sx * (my - ey) + mx * (ey - sy) + ex * (sy - my));
    if (std::fabs(denominator) < 1e-12)
        return false;
    double sc = sx * sx + sy * sy;
    double mc = mx * mx + my * my;
    double ec = ex * ex + ey * ey;
    *cx = (sc * (my - ey) + mc * (ey - sy) + ec * (sy - my)) / denominator;
    *cy = (sc * (ex - mx) + mc * (sx - ex) + ec * (mx - sx)) / denominator;
    double start = std::atan2(sy - *cy, sx - *cx);
    double middle = std::atan2(my - *cy, mx - *cx);
    double end = std::atan2(ey - *cy, ex - *cx);
    double sweep = CounterClockwise(start, end);
    if (CounterClockwise(start, middle) > sweep) {
        std::swap(sx, ex);
        std::swap(sy, ey);
        sweep = CounterClockwise(end, start);
    }
    *startX = sx;
    *startY = sy;
    *sweepDegrees = sweep * 180.0 / 3.14159265358979323846;
    return true;
}

static void AddLine(double x1, double y1, double x2, double y2,
                    double width, wxArrayString* preview)
{
    preview->Add(wxT("DS ") + Number(x1) + wxT(" ") + Number(y1)
        + wxT(" ") + Number(x2) + wxT(" ") + Number(y2)
        + wxT(" ") + Number(width) + wxT(" 21"));
}

static void ConvertGraphic(const FootprintDocument& document, int primitive,
                           wxArrayString* preview)
{
    const wxString& head = document.nodes[primitive].head;
    double width = StrokeWidth(document, primitive);
    double x1, y1, x2, y2;
    if (head == wxT("fp_line")) {
        if (ChildPoint(document, primitive, wxT("start"), &x1, &y1)
            && ChildPoint(document, primitive, wxT("end"), &x2, &y2))
        {
            AddLine(x1, y1, x2, y2, width, preview);
        }
    } else if (head == wxT("fp_rect")) {
        if (ChildPoint(document, primitive, wxT("start"), &x1, &y1)
            && ChildPoint(document, primitive, wxT("end"), &x2, &y2))
        {
            AddLine(x1, y1, x2, y1, width, preview);
            AddLine(x2, y1, x2, y2, width, preview);
            AddLine(x2, y2, x1, y2, width, preview);
            AddLine(x1, y2, x1, y1, width, preview);
        }
    } else if (head == wxT("fp_circle")) {
        if (ChildPoint(document, primitive, wxT("center"), &x1, &y1)
            && ChildPoint(document, primitive, wxT("end"), &x2, &y2))
        {
            preview->Add(wxT("DC ") + Number(x1) + wxT(" ") + Number(y1)
                + wxT(" ") + Number(x2) + wxT(" ") + Number(y2)
                + wxT(" ") + Number(width) + wxT(" 21"));
        }
    } else if (head == wxT("fp_arc")) {
        double cx, cy, sx, sy, sweep;
        if (ArcGeometry(document, primitive, &cx, &cy, &sx, &sy, &sweep)) {
            preview->Add(wxT("DA ") + Number(cx) + wxT(" ") + Number(cy)
                + wxT(" ") + Number(sx) + wxT(" ") + Number(sy)
                + wxT(" ") + Number(sweep * 10.0) + wxT(" ")
                + Number(width) + wxT(" 21"));
        }
    } else if (head == wxT("fp_poly")) {
        int points = FindChild(document, primitive, wxT("pts"));
        std::vector<CoordPair> vertices;
        for (size_t idx = 0; points >= 0 && idx < document.nodes.size(); idx++) {
            double x, y;
            if (document.nodes[idx].parent == points && document.nodes[idx].head == wxT("xy")
                && AtomDouble(document.nodes[idx], 0, &x)
                && AtomDouble(document.nodes[idx], 1, &y))
            {
                vertices.push_back(CoordPair(x, y));
            }
        }
        for (size_t idx = 1; idx < vertices.size(); idx++) {
            AddLine(vertices[idx - 1].GetX(), vertices[idx - 1].GetY(),
                    vertices[idx].GetX(), vertices[idx].GetY(), width, preview);
        }
        if (vertices.size() > 2) {
            AddLine(vertices.back().GetX(), vertices.back().GetY(),
                    vertices.front().GetX(), vertices.front().GetY(), width, preview);
        }
    }
}

static bool Hidden(const FootprintDocument& document, int owner)
{
    int hide = FindChild(document, owner, wxT("hide"));
    return hide >= 0 && (document.nodes[hide].atoms.empty()
        || document.nodes[hide].atoms[0].value.CmpNoCase(wxT("yes")) == 0);
}

static void ConvertText(const FootprintDocument& document, int textNode,
                        int field, const wxString& value, wxArrayString* preview)
{
    double x = 0.0, y = 0.0, angle = 0.0;
    int at = FindChild(document, textNode, wxT("at"));
    if (at >= 0) {
        AtomDouble(document.nodes[at], 0, &x);
        AtomDouble(document.nodes[at], 1, &y);
        AtomDouble(document.nodes[at], 2, &angle);
    }
    double fontWidth = 1.0, fontHeight = 1.0, thickness = 0.15;
    int effects = FindChild(document, textNode, wxT("effects"));
    int font = effects >= 0 ? FindChild(document, effects, wxT("font")) : -1;
    int size = font >= 0 ? FindChild(document, font, wxT("size")) : -1;
    int thick = font >= 0 ? FindChild(document, font, wxT("thickness")) : -1;
    if (size >= 0) {
        AtomDouble(document.nodes[size], 0, &fontWidth);
        AtomDouble(document.nodes[size], 1, &fontHeight);
    }
    if (thick >= 0)
        AtomDouble(document.nodes[thick], 0, &thickness);
    preview->Add(wxString::Format(wxT("T%d "), field) + Number(x) + wxT(" ")
        + Number(y) + wxT(" ") + Number(fontHeight) + wxT(" ")
        + Number(fontWidth) + wxT(" ") + Number(angle * 10.0) + wxT(" ")
        + Number(thickness) + wxT(" N ") + (Hidden(document, textNode) ? wxT("I") : wxT("V"))
        + wxT(" 21 ") + LegacyQuoted(value));
}

static char LegacyShape(const wxString& shape)
{
    if (shape == wxT("circle"))
        return 'C';
    if (shape == wxT("oval"))
        return 'O';
    if (shape == wxT("roundrect"))
        return 'D';
    if (shape == wxT("trapezoid"))
        return 'T';
    return 'R';
}

static bool ParsePad(const FootprintDocument& document, int padNode, PreviewPad* pad)
{
    const KiCadSexprNode& node = document.nodes[padNode];
    if (node.atoms.size() < 3)
        return false;
    pad->number = node.atoms[0].value;
    pad->type = node.atoms[1].value;
    pad->shape = node.atoms[2].value;
    pad->x = pad->y = pad->angle = 0.0;
    pad->width = pad->height = 0.0;
    pad->drillWidth = pad->drillHeight = 0.0;
    pad->drillOffsetX = pad->drillOffsetY = 0.0;
    pad->deltaX = pad->deltaY = 0.0;
    pad->roundrectRatio = pad->pasteRatio = 0.0;
    pad->bottomOnly = false;

    int at = FindChild(document, padNode, wxT("at"));
    if (at >= 0) {
        if (!AtomDouble(document.nodes[at], 0, &pad->x)
            || !AtomDouble(document.nodes[at], 1, &pad->y))
        {
            return false;
        }
        AtomDouble(document.nodes[at], 2, &pad->angle);
    }
    int size = FindChild(document, padNode, wxT("size"));
    if (size < 0 || !AtomDouble(document.nodes[size], 0, &pad->width)
        || !AtomDouble(document.nodes[size], 1, &pad->height))
    {
        return false;
    }
    int drill = FindChild(document, padNode, wxT("drill"));
    if (drill >= 0 && !document.nodes[drill].atoms.empty()) {
        size_t first = 0;
        if (document.nodes[drill].atoms[0].value == wxT("oval")) {
            first = 1;
            AtomDouble(document.nodes[drill], first + 1, &pad->drillHeight);
        }
        AtomDouble(document.nodes[drill], first, &pad->drillWidth);
        int offset = FindChild(document, drill, wxT("offset"));
        if (offset >= 0) {
            AtomDouble(document.nodes[offset], 0, &pad->drillOffsetX);
            AtomDouble(document.nodes[offset], 1, &pad->drillOffsetY);
        }
    }
    ChildPoint(document, padNode, wxT("rect_delta"), &pad->deltaX, &pad->deltaY);
    pad->roundrectRatio = ChildDouble(document, padNode, wxT("roundrect_rratio"));
    pad->pasteRatio = ChildDouble(document, padNode, wxT("solder_paste_margin_ratio"));

    int layers = FindChild(document, padNode, wxT("layers"));
    bool front = false, back = false;
    for (size_t idx = 0; layers >= 0 && idx < document.nodes[layers].atoms.size(); idx++) {
        wxString layer = document.nodes[layers].atoms[idx].value;
        front = front || layer == wxT("F.Cu") || layer == wxT("*.Cu");
        back = back || layer == wxT("B.Cu") || layer == wxT("*.Cu");
    }
    pad->bottomOnly = back && !front;
    return true;
}

static void ConvertPad(const PreviewPad& pad, wxArrayString* preview)
{
    double radians = pad.angle * 3.14159265358979323846 / 180.0;
    double offsetX = pad.drillOffsetX * std::cos(radians)
        - pad.drillOffsetY * std::sin(radians);
    double offsetY = pad.drillOffsetX * std::sin(radians)
        + pad.drillOffsetY * std::cos(radians);
    preview->Add(wxT("$PAD"));
    wxString shape = wxString::Format(wxT("%c"), LegacyShape(pad.shape));
    wxString sh = wxT("Sh ") + LegacyQuoted(pad.number) + wxT(" ") + shape
        + wxT(" ") + Number(pad.width) + wxT(" ") + Number(pad.height)
        + wxT(" ") + Number(pad.deltaX) + wxT(" ") + Number(pad.deltaY)
        + wxT(" ") + Number(pad.angle * 10.0);
    if (pad.shape == wxT("roundrect"))
        sh += wxT(" ") + Number(pad.roundrectRatio);
    preview->Add(sh);
    if (pad.drillWidth > 0.0) {
        wxString drill = wxT("Dr ") + Number(pad.drillWidth) + wxT(" ")
            + Number(offsetX) + wxT(" ") + Number(offsetY);
        if (pad.drillHeight > 0.0)
            drill += wxT(" O ") + Number(pad.drillWidth) + wxT(" ") + Number(pad.drillHeight);
        preview->Add(drill);
    }
    if (pad.type == wxT("smd"))
        preview->Add(pad.bottomOnly ? wxT("At SMD N 00000001") : wxT("At SMD N 00888000"));
    else if (pad.type == wxT("np_thru_hole"))
        preview->Add(wxT("At HOLE N 00000000"));
    else
        preview->Add(wxT("At STD N 00E0FFFF"));
    if (std::fabs(pad.pasteRatio) > 1e-12)
        preview->Add(wxT(".SolderPasteRatio ") + Number(pad.pasteRatio));
    preview->Add(wxT("Po ") + Number(pad.x) + wxT(" ") + Number(pad.y));
    preview->Add(wxT("$EndPAD"));
}

static bool SameSize(double width, double height, const SizeClass& size)
{
    return std::fabs(width - size.width) < 1e-6
        && std::fabs(height - size.height) < 1e-6;
}

static bool RightAngle(double angle)
{
    double normalized = std::fmod(std::fabs(angle), 180.0);
    return std::fabs(normalized - 90.0) < 1e-6;
}

static void BuildMetadata(const std::vector<PreviewPad>& pads, FootprintInfo* info)
{
    info->Clear(VER_S_EXPR);
    info->PadRRatio = 0;
    info->PadCount = (int)pads.size();
    std::vector<SizeClass> sizes;
    char shape = '\0';
    double drill = -1.0;
    for (size_t idx = 0; idx < pads.size(); idx++) {
        const PreviewPad& pad = pads[idx];
        if (!pad.number.IsEmpty())
            info->RegPadCount++;
        double radians = pad.angle * 3.14159265358979323846 / 180.0;
        double boxWidth = std::fabs(pad.width * std::cos(radians))
            + std::fabs(pad.height * std::sin(radians));
        double boxHeight = std::fabs(pad.width * std::sin(radians))
            + std::fabs(pad.height * std::cos(radians));
        info->Pads.Add(CoordSize(pad.x - boxWidth / 2.0, pad.y - boxHeight / 2.0,
                                 boxWidth, boxHeight));
        size_t sizeIndex = 0;
        while (sizeIndex < sizes.size() && !SameSize(pad.width, pad.height, sizes[sizeIndex]))
            sizeIndex++;
        if (sizeIndex == sizes.size()) {
            SizeClass sizeClass = { pad.width, pad.height, pad.angle,
                                    pad.roundrectRatio, 0 };
            sizes.push_back(sizeClass);
        }
        sizes[sizeIndex].count++;
        char currentShape = LegacyShape(pad.shape);
        if (shape == '\0')
            shape = currentShape;
        else if (shape != currentShape)
            shape = 'v';
        if (pad.drillWidth > 0.0) {
            if (drill < 0.0)
                drill = pad.drillWidth;
            else if (std::fabs(drill - pad.drillWidth) > 1e-6)
                drill = 0.0;
        }
    }
    std::sort(sizes.begin(), sizes.end(), [](const SizeClass& first, const SizeClass& second) {
        return first.count > second.count;
    });
    if (!sizes.empty()) {
        info->PadSize[0].Set(sizes[0].width, sizes[0].height);
        info->PadRightAngle[0] = RightAngle(sizes[0].angle);
        info->PadRRatio = (int)std::lround(sizes[0].ratio * 100.0);
    }
    if (sizes.size() > 1) {
        info->PadSize[1].Set(sizes[1].width, sizes[1].height);
        info->PadRightAngle[1] = RightAngle(sizes[1].angle);
    }
    info->PadShape = shape;
    info->DrillSize = drill > 0.0 ? drill : 0.0;

    const double alignment = 0.01;
    const double minimumPitch = 0.01;
    const double maximumPlausible = 10000.0;
    double bestHorizontal = std::numeric_limits<double>::max();
    double bestVertical = std::numeric_limits<double>::max();
    double maxHorizontal = 0.0, maxVertical = 0.0;
    size_t horizontalA = 0, horizontalB = 0, verticalA = 0, verticalB = 0;
    size_t horizontalSpanA = 0, horizontalSpanB = 0;
    size_t verticalSpanA = 0, verticalSpanB = 0;
    for (size_t first = 0; first < pads.size(); first++) {
        for (size_t second = first + 1; second < pads.size(); second++) {
            if (pads[first].number.IsEmpty() || pads[second].number.IsEmpty()
                || pads[first].type == wxT("np_thru_hole")
                || pads[second].type == wxT("np_thru_hole"))
            {
                continue;
            }
            double dx = std::fabs(pads[first].x - pads[second].x);
            double dy = std::fabs(pads[first].y - pads[second].y);
            if (dy < alignment && dx >= minimumPitch && dx <= maximumPlausible) {
                if (dx < bestHorizontal) {
                    bestHorizontal = dx;
                    horizontalA = first;
                    horizontalB = second;
                }
                if (dx > maxHorizontal) {
                    maxHorizontal = dx;
                    horizontalSpanA = first;
                    horizontalSpanB = second;
                }
            }
            if (dx < alignment && dy >= minimumPitch && dy <= maximumPlausible) {
                if (dy < bestVertical) {
                    bestVertical = dy;
                    verticalA = first;
                    verticalB = second;
                }
                if (dy > maxVertical) {
                    maxVertical = dy;
                    verticalSpanA = first;
                    verticalSpanB = second;
                }
            }
        }
    }
    bool haveHorizontal = bestHorizontal != std::numeric_limits<double>::max();
    bool haveVertical = bestVertical != std::numeric_limits<double>::max();
    if (haveHorizontal && (!haveVertical || bestHorizontal <= bestVertical)) {
        info->Pitch = bestHorizontal;
        info->PitchVertical = false;
        info->PitchPins[0].Set(pads[horizontalA].x, pads[horizontalA].y);
        info->PitchPins[1].Set(pads[horizontalB].x, pads[horizontalB].y);
        if (haveVertical) {
            info->SpanVer = maxVertical;
            info->SpanVerPins[0].Set(pads[verticalSpanA].x, pads[verticalSpanA].y);
            info->SpanVerPins[1].Set(pads[verticalSpanB].x, pads[verticalSpanB].y);
        }
    } else if (haveVertical) {
        info->Pitch = bestVertical;
        info->PitchVertical = true;
        info->PitchPins[0].Set(pads[verticalA].x, pads[verticalA].y);
        info->PitchPins[1].Set(pads[verticalB].x, pads[verticalB].y);
        if (haveHorizontal) {
            info->SpanHor = maxHorizontal;
            info->SpanHorPins[0].Set(pads[horizontalSpanA].x, pads[horizontalSpanA].y);
            info->SpanHorPins[1].Set(pads[horizontalSpanB].x, pads[horizontalSpanB].y);
        }
    }
    info->PitchValid = info->Pitch >= 0.0 && info->Pitch <= maximumPlausible;
    std::vector<double> padLines;
    for (size_t idx = 0; info->Pitch > 0.0 && idx < pads.size(); idx++) {
        if (pads[idx].number.IsEmpty() || pads[idx].type == wxT("np_thru_hole"))
            continue;
        double coordinate = info->PitchVertical ? pads[idx].x : pads[idx].y;
        size_t line = 0;
        while (line < padLines.size() && std::fabs(padLines[line] - coordinate) >= alignment)
            line++;
        if (line == padLines.size())
            padLines.push_back(coordinate);
    }
    info->PadLines = (int)padLines.size();
}

static bool BuildPreview(const FootprintDocument& document, wxArrayString* preview,
                         FootprintInfo* info, wxString* error)
{
    preview->Clear();
    wxString name = document.nodes[document.root].atoms.empty()
        ? wxT("ModernFootprint") : document.nodes[document.root].atoms[0].value;
    preview->Add(wxT("$MODULE ") + name);
    preview->Add(wxT("Po 0 0 0 15 00000000 00000000 ~~"));
    std::vector<PreviewPad> pads;
    int userText = 2;
    for (size_t idx = 0; idx < document.nodes.size(); idx++) {
        const KiCadSexprNode& node = document.nodes[idx];
        if (node.parent != document.root)
            continue;
        if (node.head == wxT("fp_line") || node.head == wxT("fp_rect")
            || node.head == wxT("fp_circle") || node.head == wxT("fp_arc")
            || node.head == wxT("fp_poly"))
        {
            ConvertGraphic(document, (int)idx, preview);
        } else if (node.head == wxT("fp_text") && node.atoms.size() >= 2) {
            ConvertText(document, (int)idx, userText++, node.atoms[1].value, preview);
        } else if (node.head == wxT("property") && node.atoms.size() >= 2) {
            if (node.atoms[0].value == wxT("Reference"))
                ConvertText(document, (int)idx, 0, node.atoms[1].value, preview);
            else if (node.atoms[0].value == wxT("Value"))
                ConvertText(document, (int)idx, 1, node.atoms[1].value, preview);
        } else if (node.head == wxT("pad")) {
            PreviewPad pad;
            if (!ParsePad(document, (int)idx, &pad))
                return Fail(error, wxT("Invalid modern pad definition."));
            pads.push_back(pad);
            ConvertPad(pad, preview);
        }
    }
    preview->Add(wxT("$EndMODULE ") + name);
    BuildMetadata(pads, info);
    return true;
}

} // namespace

bool ConvertModernFootprintTextToLegacyPreview(const wxString& footprintText,
                                               wxArrayString* preview,
                                               FootprintInfo* info,
                                               wxString* error)
{
    if (!preview || !info)
        return Fail(error, wxT("No footprint preview output was supplied."));
    FootprintDocument document;
    if (!ParseDocument(footprintText, &document))
        return Fail(error, wxT("Invalid modern footprint."));
    return BuildPreview(document, preview, info, error);
}

bool ConvertModernFootprintToLegacyPreview(const wxArrayString& footprint,
                                           wxArrayString* preview,
                                           FootprintInfo* info,
                                           wxString* error)
{
    return ConvertModernFootprintTextToLegacyPreview(KiCadLinesToText(footprint),
                                                     preview, info, error);
}
