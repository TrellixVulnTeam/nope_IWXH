// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_FAVICON_FALLBACK_ICON_URL_PARSER_H_
#define CHROME_COMMON_FAVICON_FALLBACK_ICON_URL_PARSER_H_

#include <string>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "components/favicon_base/fallback_icon_style.h"
#include "third_party/skia/include/core/SkColor.h"
#include "url/gurl.h"

namespace chrome {

class ParsedFallbackIconPath {
 public:
  ParsedFallbackIconPath();
  ~ParsedFallbackIconPath();

  const GURL& url() const { return url_; }

  int size_in_pixels() const { return size_in_pixels_; }

  const favicon_base::FallbackIconStyle& style() const { return style_; }

  // Parses |path|, which should be in the format described at the top of the
  // file "chrome/browser/ui/webui/fallback_icon_source.h".
  bool Parse(const std::string& path);

 private:
  // Parses |specs_str|, which should be the comma-separated value portion
  // in the format described at the top of the file
  // "chrome/browser/ui/webui/fallback_icon_source.h".
  static bool ParseSpecs(const std::string& specs_str,
                         int *size,
                         favicon_base::FallbackIconStyle* style);

  // Helper to parse color string (e.g., "red", "#f00", "#aB0137"). On success,
  // returns true and writes te result to |*color|.
  static bool ParseColor(const std::string& color_str, SkColor* color);

  friend class FallbackIconUrlParserTest;
  FRIEND_TEST_ALL_PREFIXES(FallbackIconUrlParserTest, ParseColorSuccess);
  FRIEND_TEST_ALL_PREFIXES(FallbackIconUrlParserTest, ParseColorFailure);
  FRIEND_TEST_ALL_PREFIXES(FallbackIconUrlParserTest, ParseSpecsEmpty);
  FRIEND_TEST_ALL_PREFIXES(FallbackIconUrlParserTest, ParseSpecsPartial);
  FRIEND_TEST_ALL_PREFIXES(FallbackIconUrlParserTest, ParseSpecsFull);
  FRIEND_TEST_ALL_PREFIXES(FallbackIconUrlParserTest, ParseSpecsFailure);

  // The page URL the fallback icon is requested for.
  GURL url_;

  // The size of the requested fallback icon in pixels.
  int size_in_pixels_;

  // Styling specifications of fallback icon.
  favicon_base::FallbackIconStyle style_;

  DISALLOW_COPY_AND_ASSIGN(ParsedFallbackIconPath);
};

}  // namespace chrome

#endif  // CHROME_COMMON_FAVICON_FALLBACK_ICON_URL_PARSER_H_
