/*
 *  Librarian for KiCad, a free EDA CAD application.
 *  The dialog for the search paths settings.
 *
 *  Copyright (C) 2013-2018 CompuPhase
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may not
 *  use this file except in compliance with the License. You may obtain a copy
 *  of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 *  License for the specific language governing permissions and limitations
 *  under the License.
 *
 *  $Id: libmngr_paths.cpp 5907 2018-12-14 22:05:40Z thiadmer $
 */
#include "librarymanager.h"
#include "libmngr_paths.h"
#include <wx/dir.h>
#include <wx/dirdlg.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

static wxString AppendPath(const wxString& base, const wxString& first,
                           const wxString& second = wxEmptyString)
{
    wxFileName path = wxFileName::DirName(base);
    path.AppendDir(first);
    if (!second.IsEmpty())
        path.AppendDir(second);
    return path.GetFullPath();
}

static void AddPathIfPresent(wxArrayString* paths, const wxString& path)
{
    if (!paths || path.IsEmpty() || !wxDirExists(path))
        return;
    wxFileName normalized = wxFileName::DirName(path);
    normalized.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    wxString fullpath = normalized.GetFullPath();
    for (size_t idx = 0; idx < paths->Count(); idx++) {
        if ((*paths)[idx].CmpNoCase(fullpath) == 0)
            return;
    }
    paths->Add(fullpath);
}

static wxString PathLeaf(KiCadLibraryPathKind kind)
{
    switch (kind) {
    case KICAD_PATH_SYMBOLS: return wxT("symbols");
    case KICAD_PATH_FOOTPRINTS: return wxT("footprints");
    default: return wxT("3dmodels");
    }
}

static wxString EnvironmentSuffix(KiCadLibraryPathKind kind)
{
    switch (kind) {
    case KICAD_PATH_SYMBOLS: return wxT("_SYMBOL_DIR");
    case KICAD_PATH_FOOTPRINTS: return wxT("_FOOTPRINT_DIR");
    default: return wxT("_3DMODEL_DIR");
    }
}

static wxString InstalledKiCadPath(const wxString& version, KiCadLibraryPathKind kind)
{
    wxArrayString roots;
    wxString value;
    if (wxGetEnv(wxT("ProgramFiles"), &value))
        roots.Add(value);
    if (wxGetEnv(wxT("ProgramW6432"), &value))
        roots.Add(value);
    if (wxGetEnv(wxT("ProgramFiles(x86)"), &value))
        roots.Add(value);

    for (size_t idx = 0; idx < roots.Count(); idx++) {
        wxString kicadRoot = AppendPath(roots[idx], wxT("KiCad"));
        wxString base = AppendPath(kicadRoot, version);
        wxString candidate = AppendPath(AppendPath(base, wxT("share"), wxT("kicad")), PathLeaf(kind));
        if (wxDirExists(candidate))
            return candidate;

        /* Development and preview installations use versions such as 10.99. */
        wxDir directory(kicadRoot);
        wxString installedVersion;
        bool more = directory.IsOpened()
            && directory.GetFirst(&installedVersion, version.BeforeFirst(wxT('.')) + wxT(".*"), wxDIR_DIRS);
        while (more) {
            base = AppendPath(kicadRoot, installedVersion);
            candidate = AppendPath(AppendPath(base, wxT("share"), wxT("kicad")), PathLeaf(kind));
            if (wxDirExists(candidate))
                return candidate;
            more = directory.GetNext(&installedVersion);
        }
    }
    return wxEmptyString;
}

wxArrayString DetectKiCadLibraryPaths(KiCadLibraryPathKind kind)
{
    wxArrayString paths;
    wxString leaf = PathLeaf(kind);

    /* User libraries. GetDocumentsDir() follows redirected/localized Windows
       Documents folders and therefore never relies on a hard-coded user name. */
    wxString documents = wxStandardPaths::Get().GetDocumentsDir();
    AddPathIfPresent(&paths, AppendPath(documents, wxT("kicad"), leaf));
    AddPathIfPresent(&paths, AppendPath(documents, wxT("KiCad"), leaf));

    /* KiCad exports versioned variables in its own process. They may also be
       configured system-wide, so prefer them over installation heuristics. */
    for (int version = 10; version >= 5; version--) {
        wxString variable = wxString::Format(wxT("KICAD%d%s"), version,
                                              EnvironmentSuffix(kind).c_str());
        wxString value;
        if (wxGetEnv(variable, &value))
            AddPathIfPresent(&paths, value);
    }
    const wxChar* legacyVariable = kind == KICAD_PATH_SYMBOLS ? wxT("KICAD_SYMBOL_DIR")
        : (kind == KICAD_PATH_FOOTPRINTS ? wxT("KICAD_FOOTPRINT_DIR") : wxT("KISYS3DMOD"));
    wxString legacyValue;
    if (wxGetEnv(legacyVariable, &legacyValue))
        AddPathIfPresent(&paths, legacyValue);

    /* Standard Windows installers keep versioned data below Program Files. */
    for (int version = 10; version >= 5; version--) {
        wxString value = InstalledKiCadPath(wxString::Format(wxT("%d.0"), version), kind);
        AddPathIfPresent(&paths, value);
    }

    #if !defined _WIN32
        AddPathIfPresent(&paths, wxT("/usr/share/kicad/") + leaf);
        AddPathIfPresent(&paths, wxT("/usr/local/share/kicad/") + leaf);
    #endif
    return paths;
}

static wxString ResolveKnownVariable(const wxString& variable, KiCadLibraryPathKind fallbackKind)
{
    wxString value;
    if (wxGetEnv(variable, &value) && !value.IsEmpty())
        return value;

    if (variable == wxT("KISYS3DMOD")) {
        wxArrayString paths = DetectKiCadLibraryPaths(KICAD_PATH_3DMODELS);
        return paths.IsEmpty() ? wxEmptyString : paths[0];
    }

    KiCadLibraryPathKind kind = fallbackKind;
    if (variable.EndsWith(wxT("_SYMBOL_DIR")))
        kind = KICAD_PATH_SYMBOLS;
    else if (variable.EndsWith(wxT("_FOOTPRINT_DIR")))
        kind = KICAD_PATH_FOOTPRINTS;
    else if (variable.EndsWith(wxT("_3DMODEL_DIR")))
        kind = KICAD_PATH_3DMODELS;
    else
        return wxEmptyString;

    if (!variable.StartsWith(wxT("KICAD")))
        return wxEmptyString;
    wxString version = variable.Mid(5).BeforeFirst(wxT('_'));
    long versionNumber = 0;
    if (!version.ToLong(&versionNumber) || versionNumber < 5 || versionNumber > 99)
        return wxEmptyString;
    value = InstalledKiCadPath(version + wxT(".0"), kind);
    if (!value.IsEmpty())
        return value;
    wxArrayString detected = DetectKiCadLibraryPaths(kind);
    return detected.IsEmpty() ? wxEmptyString : detected[0];
}

wxString ResolveKiCadPathVariables(const wxString& path, KiCadLibraryPathKind kind)
{
    wxString resolved = path;
    size_t searchFrom = 0;
    while (searchFrom < resolved.length()) {
        size_t begin = resolved.find(wxT("${"), searchFrom);
        wxChar close = wxT('}');
        size_t alternate = resolved.find(wxT("$("), searchFrom);
        if (begin == wxString::npos || (alternate != wxString::npos && alternate < begin)) {
            begin = alternate;
            close = wxT(')');
        }
        if (begin == wxString::npos)
            break;
        size_t end = resolved.find(close, begin + 2);
        if (end == wxString::npos)
            break;
        wxString variable = resolved.Mid(begin + 2, end - begin - 2);
        wxString value = ResolveKnownVariable(variable, kind);
        if (value.IsEmpty()) {
            searchFrom = end + 1;
        } else {
            resolved.replace(begin, end - begin + 1, value);
            searchFrom = begin + value.length();
        }
    }
    return resolved;
}

void EnsureDefaultKiCadLibraryPaths()
{
    wxFileConfig config(APP_NAME, VENDOR_NAME, theApp->GetINIPath());
    bool initialized = false;
    config.Read(wxT("settings/pathautodetected"), &initialized, false);
    if (initialized)
        return;
    struct PathGroup {
        const wxChar* key;
        KiCadLibraryPathKind kind;
    } groups[] = {
        { wxT("paths/footprints"), KICAD_PATH_FOOTPRINTS },
        { wxT("paths/symbols"), KICAD_PATH_SYMBOLS }
    };

    for (size_t group = 0; group < sizeof(groups) / sizeof(groups[0]); group++) {
        wxArrayString existingPaths;
        wxString existing;
        size_t stored = 1;
        while (config.Read(wxString::Format(wxT("%s%d"), groups[group].key, (int)stored), &existing)) {
            existingPaths.Add(existing);
            stored++;
        }
        wxArrayString paths = DetectKiCadLibraryPaths(groups[group].kind);
        for (size_t idx = 0; idx < paths.Count(); idx++) {
            bool duplicate = false;
            for (size_t old = 0; old < existingPaths.Count(); old++)
                duplicate = duplicate || existingPaths[old].CmpNoCase(paths[idx]) == 0;
            if (!duplicate) {
                config.Write(wxString::Format(wxT("%s%d"), groups[group].key, (int)stored), paths[idx]);
                existingPaths.Add(paths[idx]);
                stored++;
            }
        }
    }
    config.Write(wxT("settings/pathautodetected"), true);
    config.Flush();
}

libmngrDlgPaths::libmngrDlgPaths(wxWindow* parent)
    : DlgPaths(parent)
{
    EnsureDefaultKiCadLibraryPaths();
    /* fill in the list of search paths */
    wxFileConfig *config = new wxFileConfig(APP_NAME, VENDOR_NAME, theApp->GetINIPath());
    wxString path;
    wxString key;
    int idx = 1;
    for ( ;; ) {
        key = key.Format(wxT("paths/footprints%d"), idx);
        if (!config->Read(key, &path))
            break;
        m_lstFootprints->AppendString(path);
        idx++;
    }
    idx = 1;
    for ( ;; ) {
        key = key.Format(wxT("paths/symbols%d"), idx);
        if (!config->Read(key, &path))
            break;
        m_lstSymbols->AppendString(path);
        idx++;
    }

    bool recurse = false;
    config->Read(wxT("path/recurse"), &recurse);
    m_chkRecurseDirectories->SetValue(recurse);

    delete config;

    m_btnRemoveFootprint->Enable(false);
    m_btnRemoveSymbol->Enable(false);
}

void libmngrDlgPaths::OnFootprintPathSelect( wxCommandEvent& event )
{
    int idx = m_lstFootprints->GetSelection();
    m_btnRemoveFootprint->Enable(idx >= 0 && idx < (int)m_lstFootprints->GetCount());
    event.Skip();
}

void libmngrDlgPaths::OnAddFootprintPath( wxCommandEvent& /*event*/ )
{
    /* open the add-path dialog */
    wxDirDialog dlg(NULL, wxT("Select search path"), wxEmptyString, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        wxString path = dlg.GetPath();
        if (path.Right(7).CmpNoCase(wxT(".pretty")) == 0) {
            int pos = path.Find(DIRSEP_CHAR, true);
            wxASSERT(pos >= 0);
            wxString name = path.Mid(pos + 1);
            path = path.Left(pos);
            wxMessageBox(wxT("The library name \"") + name + wxT("\" was removed from the path.\nPlease add paths were libraries are found, don't add the libraries themselves."));
        }
        m_lstFootprints->AppendString(path);
    }
}

void libmngrDlgPaths::OnRemoveFootprintPath( wxCommandEvent& /*event*/ )
{
    int idx = m_lstFootprints->GetSelection();
    wxASSERT(idx >= 0 && idx < (int)m_lstFootprints->GetCount());
    m_lstFootprints->Delete(idx);
    m_btnRemoveFootprint->Enable(false);
}

void libmngrDlgPaths::OnSymbolPathSelect( wxCommandEvent& event )
{
    int idx = m_lstSymbols->GetSelection();
    m_btnRemoveSymbol->Enable(idx >= 0 && idx < (int)m_lstSymbols->GetCount());
    event.Skip();
}

void libmngrDlgPaths::OnAddSymbolPath( wxCommandEvent& /*event*/ )
{
    /* open the add-path dialog */
    wxDirDialog dlg(NULL, wxT("Select search path"), wxEmptyString, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        wxString path = dlg.GetPath();
        if (path.Right(13).CmpNoCase(wxT(".kicad_symdir")) == 0) {
            wxFileName library = wxFileName::DirName(path);
            wxString name = library.GetDirs().Last();
            library.RemoveLastDir();
            path = library.GetFullPath();
            wxMessageBox(wxT("The library name \"") + name
                         + wxT("\" was removed from the path.\nPlease add paths where libraries are found, not the libraries themselves."));
        }
        m_lstSymbols->AppendString(path);
    }
}

void libmngrDlgPaths::OnRemoveSymbolPath( wxCommandEvent& /*event*/ )
{
    int idx = m_lstSymbols->GetSelection();
    wxASSERT(idx >= 0 && idx < (int)m_lstSymbols->GetCount());
    m_lstSymbols->Delete(idx);
    m_btnRemoveSymbol->Enable(false);
}

void libmngrDlgPaths::OnOK( wxCommandEvent& event )
{
    wxFileConfig *config = new wxFileConfig(APP_NAME, VENDOR_NAME, theApp->GetINIPath());

    /* clear the existing search paths */
    config->DeleteGroup(wxT("paths"));

    /* save the search paths */
    wxString path;
    wxString key;
    for (int idx = 0; idx < (int)m_lstFootprints->GetCount(); idx++) {
        path = m_lstFootprints->GetString(idx);
        if (path.IsEmpty())
            break;
        key = key.Format(wxT("paths/footprints%d"), idx + 1);
        config->Write(key, path);
    }
    for (int idx = 0; idx < (int)m_lstSymbols->GetCount(); idx++) {
        path = m_lstSymbols->GetString(idx);
        if (path.IsEmpty())
            break;
        key = key.Format(wxT("paths/symbols%d"), idx + 1);
        config->Write(key, path);
    }

    bool recurse = m_chkRecurseDirectories->GetValue();
    config->Write(wxT("path/recurse"), recurse);

    delete config;
    event.Skip();
}
