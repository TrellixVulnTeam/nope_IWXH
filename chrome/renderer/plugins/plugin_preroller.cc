// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/plugins/plugin_preroller.h"

#include "base/base64.h"
#include "chrome/grit/renderer_resources.h"
#include "chrome/renderer/plugins/chrome_plugin_placeholder.h"
#include "third_party/WebKit/public/platform/WebRect.h"
#include "third_party/WebKit/public/web/WebElement.h"
#include "third_party/WebKit/public/web/WebPlugin.h"
#include "third_party/WebKit/public/web/WebPluginContainer.h"
#include "ui/gfx/codec/png_codec.h"

PluginPreroller::PluginPreroller(content::RenderFrame* render_frame,
                                 blink::WebLocalFrame* frame,
                                 const blink::WebPluginParams& params,
                                 const content::WebPluginInfo& info,
                                 const std::string& identifier,
                                 const base::string16& name,
                                 const base::string16& message,
                                 content::PluginInstanceThrottler* throttler)
    : RenderFrameObserver(render_frame),
      frame_(frame),
      params_(params),
      info_(info),
      identifier_(identifier),
      name_(name),
      message_(message),
      throttler_(throttler) {
  DCHECK(throttler);
  throttler_->AddObserver(this);
}

PluginPreroller::~PluginPreroller() {
  if (throttler_)
    throttler_->RemoveObserver(this);
}

void PluginPreroller::OnKeyframeExtracted(const SkBitmap* bitmap) {
  std::vector<unsigned char> png_data;
  if (!gfx::PNGCodec::EncodeBGRASkBitmap(*bitmap, false, &png_data)) {
    DLOG(ERROR) << "Provided keyframe could not be encoded as PNG.";
    return;
  }

  base::StringPiece png_as_string(reinterpret_cast<char*>(&png_data[0]),
                                  png_data.size());

  std::string data_url_header = "data:image/png;base64,";
  std::string data_url_body;
  base::Base64Encode(png_as_string, &data_url_body);
  keyframe_data_url_ = GURL(data_url_header + data_url_body);
}

void PluginPreroller::OnThrottleStateChange() {
  if (!throttler_->IsThrottled())
    return;

  ChromePluginPlaceholder* placeholder =
      ChromePluginPlaceholder::CreateBlockedPlugin(
          render_frame(), frame_, params_, info_, identifier_, name_,
          IDR_PLUGIN_POSTER_HTML, message_, keyframe_data_url_.spec(), GURL());
  placeholder->SetPremadePlugin(throttler_);
  placeholder->set_power_saver_enabled(true);
  placeholder->set_allow_loading(true);

  blink::WebPluginContainer* container =
      throttler_->GetWebPlugin()->container();
  container->setPlugin(placeholder->plugin());

  bool success = placeholder->plugin()->initialize(container);
  DCHECK(success);

  container->invalidate();
  container->reportGeometry();

  delete this;
}

void PluginPreroller::OnThrottlerDestroyed() {
  throttler_ = nullptr;
  delete this;
}
