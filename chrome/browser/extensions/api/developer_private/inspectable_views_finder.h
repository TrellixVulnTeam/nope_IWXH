// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_DEVELOPER_PRIVATE_INSPECTABLE_VIEWS_FINDER_H_
#define CHROME_BROWSER_EXTENSIONS_API_DEVELOPER_PRIVATE_INSPECTABLE_VIEWS_FINDER_H_

#include <set>
#include <vector>

#include "base/macros.h"
#include "base/memory/linked_ptr.h"
#include "extensions/common/view_type.h"

class Profile;
class GURL;

namespace content {
class RenderViewHost;
}

namespace extensions {
class Extension;

namespace api {
namespace developer_private {
struct ExtensionView;
}
}

// Finds inspectable views for the extensions, and returns them as represented
// by the developerPrivate API structure and schema compiler.
class InspectableViewsFinder {
 public:
  using View = linked_ptr<api::developer_private::ExtensionView>;
  using ViewList = std::vector<View>;

  // |deleting_rvh| refers to a RenderViewHost that is being deleted, and
  // should thus be omitted from any lists.
  // TODO(devlin): This is hacky.
  InspectableViewsFinder(Profile* profile,
                         content::RenderViewHost* deleting_rvh);
  ~InspectableViewsFinder();

  // Construct a view from the given parameters.
  static View ConstructView(const GURL& url,
                            int render_process_id,
                            int render_view_id,
                            bool incognito,
                            ViewType type);

  // Return a list of inspectable views for the given |extension|.
  ViewList GetViewsForExtension(const Extension& extension, bool is_enabled);

 private:
  // Returns all inspectable views for a given |profile|.
  void GetViewsForExtensionForProfile(const Extension& extension,
                                      Profile* profile,
                                      bool is_enabled,
                                      bool is_incognito,
                                      ViewList* result);

  // Returns all inspectable views for the extension process.
  void GetViewsForExtensionProcess(
      const Extension& extension,
      const std::set<content::RenderViewHost*>& views,
      bool is_incognito,
      ViewList* result);

  // Returns all inspectable app views for the extension.
  void GetAppWindowViewsForExtension(const Extension& extension,
                                     ViewList* result);

  Profile* profile_;

  content::RenderViewHost* deleting_rvh_;

  DISALLOW_COPY_AND_ASSIGN(InspectableViewsFinder);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_DEVELOPER_PRIVATE_INSPECTABLE_VIEWS_FINDER_H_
