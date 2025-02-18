// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_FALLBACK_ICON_SOURCE_H_
#define CHROME_BROWSER_UI_WEBUI_FALLBACK_ICON_SOURCE_H_

#include "base/memory/scoped_ptr.h"
#include "components/favicon_base/fallback_icon_service.h"
#include "content/public/browser/url_data_source.h"

// FallbackIconSource services explicit chrome:// requests for fallback icons.
//
// Format:
//   chrome://fallback-icon/size,bc,tc,fsr,r/url
// All of the parameters except for the url are optional. However, the order of
// the parameters is not interchangeable, and all "," must be in place.
//
// Parameter:
//  'size'
//    Positive integer to specify the fallback icon's size in pixels.
//  'bc'
//    Fallback icon's background color, as named CSS color, or RGB / ARGB /
//    RRGGBB / AARRGGBB hex formats (no leading "#").
//  'tc'
//    Fallback icon text color, as named CSS color, or RGB / ARGB / RRGGBB /
//    AARRGGBB hex formats (no leading "#").
//  'fsr'
//    Number in [0.0, 1.0] to specify the fallback icon's font size (pixels)
//    as a ratio to the icon's size.
//  'r'
//    Number in [0.0, 1.0] to specify the fallback icon's roundness.
//    0.0 specifies a square icon; 1.0 specifies a circle icon; intermediate
//    values specify a rounded square icon.
//  'url'
//    String to specify the page URL of the fallback icon.
//
//  Example: chrome://fallback-icon/32,red,#000,0.5,1.0/http://www.google.com/
//    This requests a 32x32 fallback icon for http://www.google.com, using
//    red as the background color, #000 as the text color, with font size of
//    32 * 0.5 = 16, and the icon's background shape is a circle.
class FallbackIconSource : public content::URLDataSource {
 public:
  FallbackIconSource();

  ~FallbackIconSource() override;

  // content::URLDataSource implementation.
  std::string GetSource() const override;
  void StartDataRequest(
      const std::string& path,
      int render_process_id,
      int render_frame_id,
      const content::URLDataSource::GotDataCallback& callback) override;
  std::string GetMimeType(const std::string&) const override;
  bool ShouldReplaceExistingSource() const override;
  bool ShouldServiceRequest(const net::URLRequest* request) const override;

 private:
  // Sends the default fallback icon.
  void SendDefaultResponse(
      const content::URLDataSource::GotDataCallback& callback);

  scoped_ptr<favicon_base::FallbackIconService> fallback_icon_service_;

  DISALLOW_COPY_AND_ASSIGN(FallbackIconSource);
};

#endif  // CHROME_BROWSER_UI_WEBUI_FALLBACK_ICON_SOURCE_H_
