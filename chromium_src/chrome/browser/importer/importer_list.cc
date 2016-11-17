// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * This is not a straight copy from chromium src, in particular
 * some functionality is removed.
 * This was originally forked with 52.0.2743.116.  Diff against
 * a version of that file for a full list of changes.
 */

#include "chrome/browser/importer/importer_list.h"

#include <stdint.h>

#include "atom/common/importer/chrome_importer_utils.h"
#include "base/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "build/build_config.h"
//#include "chrome/browser/shell_integration.h"
#include "chrome/common/importer/firefox_importer_utils.h"
#include "chrome/common/importer/importer_bridge.h"
#include "chrome/common/importer/importer_data_types.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/browser_thread.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(OS_MACOSX)
#include <CoreFoundation/CoreFoundation.h>

#include "base/mac/foundation_util.h"
#include "chrome/common/importer/safari_importer_utils.h"
#endif

#if defined(OS_WIN)
#include "chrome/common/importer/edge_importer_utils_win.h"
#endif

using content::BrowserThread;

namespace shell_integration {
  bool IsFirefoxDefaultBrowser() {
    return false;
  }
}

namespace {

#if defined(OS_WIN)
void DetectIEProfiles(std::vector<importer::SourceProfile>* profiles) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);
  // IE always exists and doesn't have multiple profiles.
  importer::SourceProfile ie;
  // ie.importer_name = l10n_util::GetStringUTF16(IDS_IMPORT_FROM_IE);
  ie.importer_name = base::UTF8ToUTF16("Microsoft Internet Explorer");
  ie.importer_type = importer::TYPE_IE;
  ie.services_supported = importer::HISTORY | importer::FAVORITES |
                          importer::PASSWORDS | importer::SEARCH_ENGINES;
  profiles->push_back(ie);
}

void DetectEdgeProfiles(std::vector<importer::SourceProfile>* profiles) {
  importer::SourceProfile edge;
  // edge.importer_name = l10n_util::GetStringUTF16(IDS_IMPORT_FROM_EDGE);
  edge.importer_name = base::UTF8ToUTF16("Microsoft Edge");
  edge.importer_type = importer::TYPE_EDGE;
  edge.services_supported = importer::FAVORITES;
  edge.source_path = importer::GetEdgeDataFilePath();
  profiles->push_back(edge);
}

void DetectBuiltinWindowsProfiles(
    std::vector<importer::SourceProfile>* profiles) {
  // Make the assumption on Windows 10 that Edge exists and is probably default.
  if (importer::EdgeImporterCanImport())
    DetectEdgeProfiles(profiles);
  DetectIEProfiles(profiles);
}

#endif  // defined(OS_WIN)

#if defined(OS_MACOSX)
void DetectSafariProfiles(std::vector<importer::SourceProfile>* profiles) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);
  uint16_t items = importer::NONE;
  if (!SafariImporterCanImport(base::mac::GetUserLibraryPath(), &items))
    return;

  importer::SourceProfile safari;
  // safari.importer_name = l10n_util::GetStringUTF16(IDS_IMPORT_FROM_SAFARI);
  safari.importer_name = base::UTF8ToUTF16("Safari");
  safari.importer_type = importer::TYPE_SAFARI;
  safari.services_supported = items;
  profiles->push_back(safari);
}
#endif  // defined(OS_MACOSX)

// |locale|: The application locale used for lookups in Firefox's
// locale-specific search engines feature (see firefox_importer.cc for
// details).
void DetectFirefoxProfiles(const std::string locale,
                           std::vector<importer::SourceProfile>* profiles) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);
  base::FilePath profile_path = GetFirefoxProfilePath();
  if (profile_path.empty())
    return;

  // Detects which version of Firefox is installed.
  importer::ImporterType firefox_type;
  base::FilePath app_path;
  int version = 0;
#if defined(OS_WIN)
  version = GetCurrentFirefoxMajorVersionFromRegistry();
#endif
  if (version < 2)
    GetFirefoxVersionAndPathFromProfile(profile_path, &version, &app_path);

  if (version >= 3) {
    firefox_type = importer::TYPE_FIREFOX;
  } else {
    // Ignores old versions of firefox.
    return;
  }

  importer::SourceProfile firefox;
  firefox.importer_name = GetFirefoxImporterName(app_path);
  firefox.importer_type = firefox_type;
  firefox.source_path = profile_path;
#if defined(OS_WIN)
  firefox.app_path = GetFirefoxInstallPathFromRegistry();
#endif
  if (firefox.app_path.empty())
    firefox.app_path = app_path;
  firefox.services_supported = importer::HISTORY | importer::FAVORITES |
                               importer::PASSWORDS | importer::SEARCH_ENGINES |
                               importer::AUTOFILL_FORM_DATA | importer::COOKIES;
  firefox.locale = locale;
  profiles->push_back(firefox);
}

void AddChromeToProfiles(std::vector<importer::SourceProfile>* profiles,
                         base::ListValue* chrome_profiles,
                         base::FilePath& user_data_folder,
                         std::string& brand) {
  for (const auto& value : *chrome_profiles) {
    const base::DictionaryValue* dict;
    if (!value->GetAsDictionary(&dict))
      continue;
    uint16_t items = importer::NONE;
    std::string profile;
    std::string name;
    dict->GetString("id", &profile);
    dict->GetString("name", &name);
    if (!ChromeImporterCanImport(user_data_folder.Append(
      base::FilePath::StringType(profile.begin(), profile.end())), &items))
      continue;
    importer::SourceProfile chrome;
    std::string importer_name(brand);
    importer_name.append(name);
    chrome.importer_name = base::UTF8ToUTF16(importer_name);
    chrome.importer_type = importer::TYPE_CHROME;
    chrome.services_supported = items;
    chrome.source_path =
      user_data_folder.Append(
        base::FilePath::StringType(profile.begin(), profile.end()));
    profiles->push_back(chrome);
  }
  delete chrome_profiles;
}

void DetectChromeProfiles(std::vector<importer::SourceProfile>* profiles) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);
  base::FilePath chrome_user_data_folder = GetChromeUserDataFolder();
  base::ListValue* chrome_profiles =
    GetChromeSourceProfiles(chrome_user_data_folder);
  std::string brandChrome("Chrome ");
  AddChromeToProfiles(profiles, chrome_profiles, chrome_user_data_folder,
                      brandChrome);
#if !defined(OS_LINUX)
  base::FilePath canary_user_data_folder = GetCanaryUserDataFolder();
  base::ListValue* canary_profiles =
    GetChromeSourceProfiles(canary_user_data_folder);
  std::string brandCanary("Chrome Canary ");
  AddChromeToProfiles(profiles, canary_profiles, canary_user_data_folder,
                      brandCanary);
#endif
  base::FilePath chromium_user_data_folder = GetChromiumUserDataFolder();
  base::ListValue* chromium_profiles =
    GetChromeSourceProfiles(chromium_user_data_folder);
  std::string brandChromium("Chromium ");
  AddChromeToProfiles(profiles, chromium_profiles, chromium_user_data_folder,
                      brandChromium);
}

std::vector<importer::SourceProfile> DetectSourceProfilesWorker(
    const std::string& locale,
    bool include_interactive_profiles) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);

  std::vector<importer::SourceProfile> profiles;

  // The first run import will automatically take settings from the first
  // profile detected, which should be the user's current default.
#if defined(OS_WIN)
  if (shell_integration::IsFirefoxDefaultBrowser()) {
    DetectFirefoxProfiles(locale, &profiles);
    DetectBuiltinWindowsProfiles(&profiles);
  } else {
    DetectBuiltinWindowsProfiles(&profiles);
    DetectFirefoxProfiles(locale, &profiles);
    DetectChromeProfiles(&profiles);
  }
#elif defined(OS_MACOSX)
  if (shell_integration::IsFirefoxDefaultBrowser()) {
    DetectFirefoxProfiles(locale, &profiles);
    DetectSafariProfiles(&profiles);
  } else {
    DetectSafariProfiles(&profiles);
    DetectFirefoxProfiles(locale, &profiles);
    DetectChromeProfiles(&profiles);
  }
#else
  DetectFirefoxProfiles(locale, &profiles);
    DetectChromeProfiles(&profiles);
#endif
  if (include_interactive_profiles) {
    importer::SourceProfile bookmarks_profile;
    bookmarks_profile.importer_name =
        // l10n_util::GetStringUTF16(IDS_IMPORT_FROM_BOOKMARKS_HTML_FILE);
        base::UTF8ToUTF16("Bookmarks HTML File");
    bookmarks_profile.importer_type = importer::TYPE_BOOKMARKS_FILE;
    bookmarks_profile.services_supported = importer::FAVORITES;
    profiles.push_back(bookmarks_profile);
  }

  return profiles;
}

}  // namespace

ImporterList::ImporterList()
    : weak_ptr_factory_(this) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
}

ImporterList::~ImporterList() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
}

void ImporterList::DetectSourceProfiles(
    const std::string& locale,
    bool include_interactive_profiles,
    const base::Closure& profiles_loaded_callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  BrowserThread::PostTaskAndReplyWithResult(
      BrowserThread::FILE,
      FROM_HERE,
      base::Bind(&DetectSourceProfilesWorker,
                 locale,
                 include_interactive_profiles),
      base::Bind(&ImporterList::SourceProfilesLoaded,
                 weak_ptr_factory_.GetWeakPtr(),
                 profiles_loaded_callback));
}

const importer::SourceProfile& ImporterList::GetSourceProfileAt(
    size_t index) const {
  DCHECK_LT(index, count());
  return source_profiles_[index];
}

void ImporterList::SourceProfilesLoaded(
    const base::Closure& profiles_loaded_callback,
    const std::vector<importer::SourceProfile>& profiles) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  source_profiles_.assign(profiles.begin(), profiles.end());
  profiles_loaded_callback.Run();
}
