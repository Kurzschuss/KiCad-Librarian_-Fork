#include "kicadsymbolpreview.h"

#include "kicadsexpr.h"

#include <wx/filefn.h>
#include <wx/filename.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct PreviewDocument {
    std::vector<KiCadSexprNode> nodes;
    int libraryRoot;
};

struct PreviewSymbol {
    size_t document;
    int root;
    wxString name;
};

struct PreviewContext {
    wxString library;
    bool directory;
    std::vector<PreviewDocument> documents;
};

static bool Fail(wxString* error, const wxString& message)
{
    if (error)
        *error = message;
    return false;
}

static int FindChild(const PreviewDocument& document, int parent, const wxString& head)
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

static bool ChildPoint(const PreviewDocument& document, int parent,
                       const wxString& head, double* x, double* y)
{
    int child = FindChild(document, parent, head);
    return child >= 0 && AtomDouble(document.nodes[child], 0, x)
        && AtomDouble(document.nodes[child], 1, y);
}

static bool ParseDocument(const wxString& text, PreviewDocument* document)
{
    document->nodes.clear();
    if (!ParseKiCadSexpr(text, &document->nodes))
        return false;
    document->libraryRoot = FindKiCadSexprRoot(document->nodes, wxT("kicad_symbol_lib"));
    return document->libraryRoot >= 0;
}

static int FindSymbol(const PreviewDocument& document, const wxString& name)
{
    for (size_t idx = 0; idx < document.nodes.size(); idx++) {
        const KiCadSexprNode& node = document.nodes[idx];
        if (node.parent == document.libraryRoot && node.head == wxT("symbol")
            && !node.atoms.empty() && node.atoms[0].value.CmpNoCase(name) == 0)
        {
            return (int)idx;
        }
    }
    return -1;
}

static bool LoadSymbol(PreviewContext* context, const wxString& name,
                       PreviewSymbol* symbol, wxString* error)
{
    if (!context->directory) {
        if (context->documents.empty())
            return Fail(error, wxT("The symbol library is empty."));
        int root = FindSymbol(context->documents[0], name);
        if (root < 0)
            return Fail(error, wxT("Missing parent symbol: ") + name);
        symbol->document = 0;
        symbol->root = root;
        symbol->name = name;
        return true;
    }

    wxString path = wxFileName(context->library, name + wxT(".kicad_sym")).GetFullPath();
    wxString text;
    PreviewDocument document;
    if (!ReadKiCadTextFile(path, &text) || !ParseDocument(text, &document))
        return Fail(error, wxT("Cannot read parent symbol: ") + name);
    int root = FindSymbol(document, name);
    if (root < 0)
        return Fail(error, wxT("Missing symbol definition: ") + name);
    context->documents.push_back(document);
    symbol->document = context->documents.size() - 1;
    symbol->root = root;
    symbol->name = name;
    return true;
}

static bool ResolveInheritance(PreviewContext* context, const wxString& name,
                               wxArrayString* visiting,
                               std::vector<PreviewSymbol>* symbols,
                               wxString* error)
{
    if (visiting->Index(name, false) != wxNOT_FOUND)
        return Fail(error, wxT("Cyclic symbol inheritance at: ") + name);
    visiting->Add(name);

    PreviewSymbol symbol;
    if (!LoadSymbol(context, name, &symbol, error)) {
        visiting->Remove(name);
        return false;
    }
    const PreviewDocument& document = context->documents[symbol.document];
    int extends = FindChild(document, symbol.root, wxT("extends"));
    if (extends >= 0 && !document.nodes[extends].atoms.empty()) {
        wxString parent = document.nodes[extends].atoms[0].value;
        if (!ResolveInheritance(context, parent, visiting, symbols, error)) {
            visiting->Remove(name);
            return false;
        }
    }
    symbols->push_back(symbol);
    visiting->Remove(name);
    return true;
}

static bool ParseUnitName(const wxString& name, int* unit, int* bodyStyle)
{
    int last = name.Find(wxT('_'), true);
    if (last < 0)
        return false;
    int previous = name.Left(last).Find(wxT('_'), true);
    if (previous < 0)
        return false;
    long parsedUnit = 0;
    long parsedStyle = 0;
    if (!name.Mid(previous + 1, last - previous - 1).ToLong(&parsedUnit)
        || !name.Mid(last + 1).ToLong(&parsedStyle))
    {
        return false;
    }
    *unit = (int)parsedUnit;
    *bodyStyle = (int)parsedStyle;
    return true;
}

static long ToMil(double millimetres)
{
    return (long)std::lround(millimetres / 0.0254);
}

static long ToAngle(double degrees)
{
    return (long)std::lround(degrees * 10.0);
}

static wxString LegacyText(const wxString& value)
{
    if (value.IsEmpty())
        return wxT("~");
    wxString escaped = value;
    escaped.Replace(wxT("\""), wxT("'"));
    return wxT("\"") + escaped + wxT("\"");
}

static long StrokeWidth(const PreviewDocument& document, int primitive)
{
    int stroke = FindChild(document, primitive, wxT("stroke"));
    int width = stroke >= 0 ? FindChild(document, stroke, wxT("width")) : -1;
    double value = 0.0;
    return width >= 0 && AtomDouble(document.nodes[width], 0, &value) ? ToMil(value) : 0;
}

static wxChar FillType(const PreviewDocument& document, int primitive)
{
    int fill = FindChild(document, primitive, wxT("fill"));
    int type = fill >= 0 ? FindChild(document, fill, wxT("type")) : -1;
    if (type < 0 || document.nodes[type].atoms.empty())
        return wxT('N');
    wxString value = document.nodes[type].atoms[0].value;
    if (value == wxT("background"))
        return wxT('f');
    if (value == wxT("outline"))
        return wxT('F');
    return wxT('N');
}

static long TextSize(const PreviewDocument& document, int owner)
{
    int effects = FindChild(document, owner, wxT("effects"));
    int font = effects >= 0 ? FindChild(document, effects, wxT("font")) : -1;
    int size = font >= 0 ? FindChild(document, font, wxT("size")) : -1;
    double value = 1.27;
    if (size >= 0)
        AtomDouble(document.nodes[size], 0, &value);
    return std::max(1L, ToMil(value));
}

static wxString ElectricalType(const wxString& modern)
{
    if (modern == wxT("input")) return wxT("I");
    if (modern == wxT("output")) return wxT("O");
    if (modern == wxT("bidirectional")) return wxT("B");
    if (modern == wxT("tri_state")) return wxT("T");
    if (modern == wxT("passive")) return wxT("P");
    if (modern == wxT("open_collector")) return wxT("C");
    if (modern == wxT("open_emitter")) return wxT("E");
    if (modern == wxT("no_connect")) return wxT("N");
    if (modern == wxT("power_in")) return wxT("W");
    if (modern == wxT("power_out")) return wxT("w");
    return wxT("U");
}

static wxString PinShape(const wxString& modern)
{
    if (modern == wxT("inverted")) return wxT("I");
    if (modern == wxT("clock")) return wxT("C");
    if (modern == wxT("inverted_clock")) return wxT("CI");
    if (modern == wxT("input_low")) return wxT("L");
    if (modern == wxT("clock_low")) return wxT("CL");
    if (modern == wxT("output_low")) return wxT("V");
    if (modern == wxT("falling_edge_clock")) return wxT("F");
    if (modern == wxT("non_logic")) return wxT("X");
    return wxEmptyString;
}

static wxChar PinOrientation(double angle)
{
    int normalized = ((int)std::lround(angle) % 360 + 360) % 360;
    if (normalized >= 45 && normalized < 135) return wxT('U');
    if (normalized >= 135 && normalized < 225) return wxT('L');
    if (normalized >= 225 && normalized < 315) return wxT('D');
    return wxT('R');
}

static void ConvertPin(const PreviewDocument& document, int primitive,
                       int unit, int bodyStyle, wxArrayString* preview)
{
    const KiCadSexprNode& pin = document.nodes[primitive];
    if (pin.atoms.size() < 2)
        return;
    int at = FindChild(document, primitive, wxT("at"));
    int lengthNode = FindChild(document, primitive, wxT("length"));
    int nameNode = FindChild(document, primitive, wxT("name"));
    int numberNode = FindChild(document, primitive, wxT("number"));
    double x = 0.0, y = 0.0, angle = 0.0, length = 0.0;
    if (at < 0 || lengthNode < 0 || nameNode < 0 || numberNode < 0
        || !AtomDouble(document.nodes[at], 0, &x)
        || !AtomDouble(document.nodes[at], 1, &y)
        || !AtomDouble(document.nodes[lengthNode], 0, &length))
    {
        return;
    }
    AtomDouble(document.nodes[at], 2, &angle);
    wxString name = document.nodes[nameNode].atoms.empty()
        ? wxEmptyString : document.nodes[nameNode].atoms[0].value;
    wxString number = document.nodes[numberNode].atoms.empty()
        ? wxEmptyString : document.nodes[numberNode].atoms[0].value;
    long nameSize = TextSize(document, nameNode);
    long numberSize = TextSize(document, numberNode);
    wxString line = wxString::Format(wxT("X %s %s %ld %ld %ld %c %ld %ld %d %d %s"),
        LegacyText(name).c_str(), LegacyText(number).c_str(), ToMil(x), ToMil(y),
        ToMil(length), PinOrientation(angle), numberSize, nameSize, unit, bodyStyle,
        ElectricalType(pin.atoms[0].value).c_str());
    wxString shape = PinShape(pin.atoms[1].value);
    if (!shape.IsEmpty())
        line += wxT(" ") + shape;
    preview->Add(line);
}

static void ConvertRectangle(const PreviewDocument& document, int primitive,
                             int unit, int bodyStyle, wxArrayString* preview)
{
    double x1, y1, x2, y2;
    if (!ChildPoint(document, primitive, wxT("start"), &x1, &y1)
        || !ChildPoint(document, primitive, wxT("end"), &x2, &y2))
    {
        return;
    }
    preview->Add(wxString::Format(wxT("S %ld %ld %ld %ld %d %d %ld %c"),
        ToMil(x1), ToMil(y1), ToMil(x2), ToMil(y2), unit, bodyStyle,
        StrokeWidth(document, primitive), FillType(document, primitive)));
}

static void ConvertCircle(const PreviewDocument& document, int primitive,
                          int unit, int bodyStyle, wxArrayString* preview)
{
    double x, y, radius;
    int radiusNode = FindChild(document, primitive, wxT("radius"));
    if (!ChildPoint(document, primitive, wxT("center"), &x, &y)
        || radiusNode < 0 || !AtomDouble(document.nodes[radiusNode], 0, &radius))
    {
        return;
    }
    preview->Add(wxString::Format(wxT("C %ld %ld %ld %d %d %ld %c"),
        ToMil(x), ToMil(y), ToMil(radius), unit, bodyStyle,
        StrokeWidth(document, primitive), FillType(document, primitive)));
}

static void ConvertPolyline(const PreviewDocument& document, int primitive,
                            int unit, int bodyStyle, wxArrayString* preview)
{
    int pointsNode = FindChild(document, primitive, wxT("pts"));
    if (pointsNode < 0)
        return;
    wxArrayString points;
    for (size_t idx = 0; idx < document.nodes.size(); idx++) {
        const KiCadSexprNode& point = document.nodes[idx];
        double x, y;
        if (point.parent == pointsNode && point.head == wxT("xy")
            && AtomDouble(point, 0, &x) && AtomDouble(point, 1, &y))
        {
            points.Add(wxString::Format(wxT("%ld %ld"), ToMil(x), ToMil(y)));
        }
    }
    if (points.IsEmpty())
        return;
    wxString line = wxString::Format(wxT("P %u %d %d %ld"),
        (unsigned)points.Count(), unit, bodyStyle, StrokeWidth(document, primitive));
    for (size_t idx = 0; idx < points.Count(); idx++)
        line += wxT(" ") + points[idx];
    line += wxString::Format(wxT(" %c"), FillType(document, primitive));
    preview->Add(line);
}

static double NormalizeRadians(double angle)
{
    const double full = 2.0 * 3.14159265358979323846;
    while (angle < 0.0) angle += full;
    while (angle >= full) angle -= full;
    return angle;
}

static double CounterClockwise(double from, double to)
{
    double delta = NormalizeRadians(to) - NormalizeRadians(from);
    return delta < 0.0 ? delta + 2.0 * 3.14159265358979323846 : delta;
}

static void ConvertArc(const PreviewDocument& document, int primitive,
                       int unit, int bodyStyle, wxArrayString* preview)
{
    double sx, sy, mx, my, ex, ey;
    if (!ChildPoint(document, primitive, wxT("start"), &sx, &sy)
        || !ChildPoint(document, primitive, wxT("mid"), &mx, &my)
        || !ChildPoint(document, primitive, wxT("end"), &ex, &ey))
    {
        return;
    }
    double denominator = 2.0 * (sx * (my - ey) + mx * (ey - sy) + ex * (sy - my));
    if (std::fabs(denominator) < 1e-12)
        return;
    double sc = sx * sx + sy * sy;
    double mc = mx * mx + my * my;
    double ec = ex * ex + ey * ey;
    double cx = (sc * (my - ey) + mc * (ey - sy) + ec * (sy - my)) / denominator;
    double cy = (sc * (ex - mx) + mc * (sx - ex) + ec * (mx - sx)) / denominator;
    double radius = std::sqrt((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy));
    double start = std::atan2(sy - cy, sx - cx);
    double middle = std::atan2(my - cy, mx - cx);
    double end = std::atan2(ey - cy, ex - cx);
    if (CounterClockwise(start, middle) > CounterClockwise(start, end))
        std::swap(start, end);
    const double degrees = 180.0 / 3.14159265358979323846;
    preview->Add(wxString::Format(wxT("A %ld %ld %ld %ld %ld %d %d %ld %c"),
        ToMil(cx), ToMil(cy), ToMil(radius), ToAngle(start * degrees),
        ToAngle(end * degrees), unit, bodyStyle, StrokeWidth(document, primitive),
        FillType(document, primitive)));
}

static void ConvertText(const PreviewDocument& document, int primitive,
                        int unit, int bodyStyle, wxArrayString* preview)
{
    const KiCadSexprNode& text = document.nodes[primitive];
    if (text.atoms.empty())
        return;
    int at = FindChild(document, primitive, wxT("at"));
    double x = 0.0, y = 0.0, angle = 0.0;
    if (at < 0 || !AtomDouble(document.nodes[at], 0, &x)
        || !AtomDouble(document.nodes[at], 1, &y))
    {
        return;
    }
    AtomDouble(document.nodes[at], 2, &angle);
    preview->Add(wxString::Format(wxT("T %ld %ld %ld %ld 0 %d %d %s Normal 0 C C"),
        ToAngle(angle), ToMil(x), ToMil(y), TextSize(document, primitive), unit,
        bodyStyle, LegacyText(text.atoms[0].value).c_str()));
}

static void ConvertNestedSymbol(const PreviewDocument& document, int nested,
                                wxArrayString* preview)
{
    if (document.nodes[nested].atoms.empty())
        return;
    int unit = 0;
    int bodyStyle = 1;
    if (!ParseUnitName(document.nodes[nested].atoms[0].value, &unit, &bodyStyle))
        return;
    for (size_t idx = 0; idx < document.nodes.size(); idx++) {
        if (document.nodes[idx].parent != nested)
            continue;
        wxString head = document.nodes[idx].head;
        if (head == wxT("pin"))
            ConvertPin(document, (int)idx, unit, bodyStyle, preview);
        else if (head == wxT("rectangle"))
            ConvertRectangle(document, (int)idx, unit, bodyStyle, preview);
        else if (head == wxT("circle"))
            ConvertCircle(document, (int)idx, unit, bodyStyle, preview);
        else if (head == wxT("polyline"))
            ConvertPolyline(document, (int)idx, unit, bodyStyle, preview);
        else if (head == wxT("arc"))
            ConvertArc(document, (int)idx, unit, bodyStyle, preview);
        else if (head == wxT("text"))
            ConvertText(document, (int)idx, unit, bodyStyle, preview);
    }
}

static int UnitCount(const PreviewContext& context, const std::vector<PreviewSymbol>& symbols)
{
    int maximum = 1;
    for (size_t chain = 0; chain < symbols.size(); chain++) {
        const PreviewDocument& document = context.documents[symbols[chain].document];
        for (size_t idx = 0; idx < document.nodes.size(); idx++) {
            if (document.nodes[idx].parent != symbols[chain].root
                || document.nodes[idx].head != wxT("symbol")
                || document.nodes[idx].atoms.empty())
            {
                continue;
            }
            int unit = 0, style = 0;
            if (ParseUnitName(document.nodes[idx].atoms[0].value, &unit, &style))
                maximum = std::max(maximum, unit);
        }
    }
    return maximum;
}

static int FindProperty(const PreviewContext& context,
                        const std::vector<PreviewSymbol>& symbols,
                        const wxString& key, size_t* documentIndex)
{
    for (size_t reverse = symbols.size(); reverse > 0; reverse--) {
        const PreviewSymbol& symbol = symbols[reverse - 1];
        const PreviewDocument& document = context.documents[symbol.document];
        for (size_t idx = 0; idx < document.nodes.size(); idx++) {
            const KiCadSexprNode& node = document.nodes[idx];
            if (node.parent == symbol.root && node.head == wxT("property")
                && node.atoms.size() >= 2 && node.atoms[0].value == key)
            {
                *documentIndex = symbol.document;
                return (int)idx;
            }
        }
    }
    return -1;
}

static wxString PropertyField(const PreviewContext& context,
                              const std::vector<PreviewSymbol>& symbols,
                              const wxString& key, const wxString& fallback,
                              int fieldNumber)
{
    size_t documentIndex = 0;
    int property = FindProperty(context, symbols, key, &documentIndex);
    if (property < 0)
        return wxString::Format(wxT("F%d %s 0 0 50 H V C CNN"),
            fieldNumber, LegacyText(fallback).c_str());
    const PreviewDocument& document = context.documents[documentIndex];
    const KiCadSexprNode& node = document.nodes[property];
    double x = 0.0, y = 0.0, angle = 0.0;
    int at = FindChild(document, property, wxT("at"));
    if (at >= 0) {
        AtomDouble(document.nodes[at], 0, &x);
        AtomDouble(document.nodes[at], 1, &y);
        AtomDouble(document.nodes[at], 2, &angle);
    }
    int hide = FindChild(document, property, wxT("hide"));
    bool hidden = hide >= 0 && (document.nodes[hide].atoms.empty()
        || document.nodes[hide].atoms[0].value == wxT("yes"));
    wxString value = node.atoms.size() >= 2 ? node.atoms[1].value : fallback;
    wxChar orientation = std::fabs(std::fmod(angle, 180.0) - 90.0) < 1.0 ? wxT('V') : wxT('H');
    return wxString::Format(wxT("F%d %s %ld %ld %ld %c %c C CNN"), fieldNumber,
        LegacyText(value).c_str(), ToMil(x), ToMil(y), TextSize(document, property),
        orientation, hidden ? wxT('I') : wxT('V'));
}

static bool BuildPreview(PreviewContext* context, const wxString& symbolName,
                         wxArrayString* preview, wxString* error)
{
    preview->Clear();
    std::vector<PreviewSymbol> symbols;
    wxArrayString visiting;
    if (!ResolveInheritance(context, symbolName, &visiting, &symbols, error))
        return false;
    if (symbols.empty())
        return Fail(error, wxT("No symbol definition found."));

    size_t referenceDocument = 0;
    int referenceProperty = FindProperty(*context, symbols, wxT("Reference"), &referenceDocument);
    wxString prefix = wxT("U");
    if (referenceProperty >= 0) {
        const KiCadSexprNode& property = context->documents[referenceDocument].nodes[referenceProperty];
        if (property.atoms.size() >= 2 && !property.atoms[1].value.IsEmpty())
            prefix = property.atoms[1].value;
    }
    preview->Add(wxT("# Modern KiCad symbol preview"));
    preview->Add(wxString::Format(wxT("DEF %s %s 0 40 Y Y %d F N"),
        symbolName.c_str(), prefix.c_str(), UnitCount(*context, symbols)));
    preview->Add(PropertyField(*context, symbols, wxT("Reference"), prefix, 0));
    preview->Add(PropertyField(*context, symbols, wxT("Value"), symbolName, 1));
    preview->Add(wxT("DRAW"));
    for (size_t chain = 0; chain < symbols.size(); chain++) {
        const PreviewDocument& document = context->documents[symbols[chain].document];
        for (size_t idx = 0; idx < document.nodes.size(); idx++) {
            if (document.nodes[idx].parent == symbols[chain].root
                && document.nodes[idx].head == wxT("symbol"))
            {
                ConvertNestedSymbol(document, (int)idx, preview);
            }
        }
    }
    preview->Add(wxT("ENDDRAW"));
    preview->Add(wxT("ENDDEF"));
    return true;
}

} // namespace

bool ConvertModernSymbolTextToLegacyPreview(const wxString& libraryText,
                                            const wxString& symbolName,
                                            wxArrayString* preview,
                                            wxString* error)
{
    if (!preview)
        return Fail(error, wxT("No preview output was supplied."));
    preview->Clear();
    PreviewDocument document;
    if (!ParseDocument(libraryText, &document))
        return Fail(error, wxT("Invalid modern symbol library."));
    PreviewContext context;
    context.directory = false;
    context.documents.push_back(document);
    return BuildPreview(&context, symbolName, preview, error);
}

bool LoadModernSymbolPreview(const wxString& library,
                             const wxString& symbolName,
                             wxArrayString* preview,
                             wxString* error)
{
    if (!preview)
        return Fail(error, wxT("No preview output was supplied."));
    preview->Clear();
    PreviewContext context;
    context.library = library;
    context.directory = wxFileName::DirExists(library)
        && wxFileName(library).GetExt().CmpNoCase(wxT("kicad_symdir")) == 0;
    if (!context.directory) {
        wxString text;
        PreviewDocument document;
        if (!ReadKiCadTextFile(library, &text) || !ParseDocument(text, &document))
            return Fail(error, wxT("Invalid modern symbol library."));
        context.documents.push_back(document);
    }
    return BuildPreview(&context, symbolName, preview, error);
}
