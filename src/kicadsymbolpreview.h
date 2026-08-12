#ifndef KICADSYMBOLPREVIEW_H
#define KICADSYMBOLPREVIEW_H

#include <wx/arrstr.h>
#include <wx/string.h>

/* Build a temporary legacy DEF/DRAW representation for the existing preview
   renderer. The modern source is read-only and is never rewritten. */
bool ConvertModernSymbolTextToLegacyPreview(const wxString& libraryText,
                                            const wxString& symbolName,
                                            wxArrayString* preview,
                                            wxString* error = NULL);
bool LoadModernSymbolPreview(const wxString& library,
                             const wxString& symbolName,
                             wxArrayString* preview,
                             wxString* error = NULL);

#endif /* KICADSYMBOLPREVIEW_H */
