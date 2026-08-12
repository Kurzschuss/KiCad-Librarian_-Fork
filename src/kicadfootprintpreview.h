#ifndef KICADFOOTPRINTPREVIEW_H
#define KICADFOOTPRINTPREVIEW_H

#include <wx/arrstr.h>
#include <wx/string.h>

#include "libraryfunctions.h"

/* Build temporary legacy drawing data and structured pad metadata for the
   existing preview. The modern source remains read-only. */
bool ConvertModernFootprintTextToLegacyPreview(const wxString& footprintText,
                                               wxArrayString* preview,
                                               FootprintInfo* info,
                                               wxString* error = NULL);
bool ConvertModernFootprintToLegacyPreview(const wxArrayString& footprint,
                                           wxArrayString* preview,
                                           FootprintInfo* info,
                                           wxString* error = NULL);

#endif /* KICADFOOTPRINTPREVIEW_H */
