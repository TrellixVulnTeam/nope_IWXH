// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var WALLPAPER_PICKER_WIDTH = 574;
var WALLPAPER_PICKER_HEIGHT = 420;

var wallpaperPickerWindow;

var surpriseWallpaper = null;

function SurpriseWallpaper() {
}

/**
 * Gets SurpriseWallpaper instance. In case it hasn't been initialized, a new
 * instance is created.
 * @return {SurpriseWallpaper} A SurpriseWallpaper instance.
 */
SurpriseWallpaper.getInstance = function() {
  if (!surpriseWallpaper)
    surpriseWallpaper = new SurpriseWallpaper();
  return surpriseWallpaper;
};

/**
 * Tries to change wallpaper to a new one in the background. May fail due to a
 * network issue.
 */
SurpriseWallpaper.prototype.tryChangeWallpaper = function() {
  var self = this;
  var onFailure = function(status) {
    if (status != 404)
      self.fallbackToLocalRss_();
    else
      self.updateRandomWallpaper_();
  };
  // Try to fetch newest rss as document from server first. If the requested
  // URL is not found (404 error), set a random wallpaper displayed in the
  // wallpaper picker. If any other error occurs, proceed with local copy of
  // rss.
  WallpaperUtil.fetchURL(Constants.WallpaperRssURL, 'document', function(xhr) {
    WallpaperUtil.saveToStorage(Constants.AccessRssKey,
        new XMLSerializer().serializeToString(xhr.responseXML), false);
    self.updateSurpriseWallpaper(xhr.responseXML);
  }, onFailure);
};

/**
 * Retries changing the wallpaper 1 hour later. This is called when fetching the
 * rss or wallpaper from server fails.
 * @private
 */
SurpriseWallpaper.prototype.retryLater_ = function() {
  chrome.alarms.create('RetryAlarm', {delayInMinutes: 60});
};

/**
 * Fetches the cached rss feed from local storage in the event of being unable
 * to download the online feed.
 * @private
 */
SurpriseWallpaper.prototype.fallbackToLocalRss_ = function() {
  var self = this;
  Constants.WallpaperLocalStorage.get(Constants.AccessRssKey, function(items) {
    var rssString = items[Constants.AccessRssKey];
    if (rssString) {
      self.updateSurpriseWallpaper(new DOMParser().parseFromString(rssString,
                                                                   'text/xml'));
    } else {
      self.updateSurpriseWallpaper();
    }
  });
};

/**
 * Starts to change wallpaper. Called after rss is fetched.
 * @param {Document=} opt_rss The fetched rss document. If opt_rss is null, uses
 *     a random wallpaper.
 */
SurpriseWallpaper.prototype.updateSurpriseWallpaper = function(opt_rss) {
  if (opt_rss) {
    var items = opt_rss.querySelectorAll('item');
    var date = new Date(new Date().toDateString()).getTime();
    for (var i = 0; i < items.length; i++) {
      item = items[i];
      var disableDate = new Date(item.getElementsByTagNameNS(
          Constants.WallpaperNameSpaceURI, 'disableDate')[0].textContent).
              getTime();
      var enableDate = new Date(item.getElementsByTagNameNS(
          Constants.WallpaperNameSpaceURI, 'enableDate')[0].textContent).
              getTime();
      var regionsString = item.getElementsByTagNameNS(
          Constants.WallpaperNameSpaceURI, 'regions')[0].textContent;
      var regions = regionsString.split(', ');
      if (enableDate <= date && disableDate > date &&
          regions.indexOf(navigator.language) != -1) {
        var self = this;
        this.setWallpaperFromRssItem_(item,
                                      function() {},
                                      function(status) {
                                        if (status != 404)
                                          self.retryLater_();
                                        else
                                          self.updateRandomWallpaper_();
                                      });
        return;
      }
    }
  }
  // No surprise wallpaper for today at current locale or fetching rss feed
  // fails. Fallback to use a random one from wallpaper server.
  this.updateRandomWallpaper_();
};

/**
 * Sets a new random wallpaper if one has not already been set today.
 * @private
 */
SurpriseWallpaper.prototype.updateRandomWallpaper_ = function() {
  var self = this;
  Constants.WallpaperSyncStorage.get(
      Constants.AccessLastSurpriseWallpaperChangedDate, function(items) {
    var dateString = new Date().toDateString();
    // At most one random wallpaper per day.
    if (items[Constants.AccessLastSurpriseWallpaperChangedDate] != dateString) {
      self.setRandomWallpaper_(dateString);
    }
  });
};

/**
 * Sets wallpaper to one of the wallpapers displayed in wallpaper picker. If
 * the wallpaper download fails, retry one hour later. Wallpapers that are
 * disabled for surprise me are excluded.
 * @param {string} dateString String representation of current local date.
 * @private
 */
SurpriseWallpaper.prototype.setRandomWallpaper_ = function(dateString) {
  var self = this;
  Constants.WallpaperLocalStorage.get(Constants.AccessManifestKey,
                                      function(items) {
    var manifest = items[Constants.AccessManifestKey];
    if (manifest && manifest.wallpaper_list) {
      var filtered = manifest.wallpaper_list.filter(function(element) {
        // Older version manifest do not have available_for_surprise_me field.
        // In this case, no wallpaper should be filtered out.
        return element.available_for_surprise_me ||
            element.available_for_surprise_me == undefined;
      });
      var index = Math.floor(Math.random() * filtered.length);
      var wallpaper = filtered[index];
      var wallpaperURL = wallpaper.base_url + Constants.HighResolutionSuffix;
      var onSuccess = function() {
        WallpaperUtil.saveWallpaperInfo(wallpaperURL, wallpaper.default_layout,
            Constants.WallpaperSourceEnum.Online);
        WallpaperUtil.saveToStorage(
            Constants.AccessLastSurpriseWallpaperChangedDate,
            dateString,
            true);
      };
      WallpaperUtil.setOnlineWallpaper(wallpaperURL, wallpaper.default_layout,
          onSuccess, self.retryLater_.bind(self));
    }
  });
};

/**
 * Sets wallpaper to the wallpaper specified by item from rss. If downloading
 * the wallpaper fails, retry one hour later.
 * @param {Element} item The wallpaper rss item element.
 * @param {function} onSuccess Success callback.
 * @param {function} onFailure Failure callback.
 * @private
 */
SurpriseWallpaper.prototype.setWallpaperFromRssItem_ = function(item,
                                                                onSuccess,
                                                                onFailure) {
  var url = item.querySelector('link').textContent;
  var layout = item.getElementsByTagNameNS(
        Constants.WallpaperNameSpaceURI, 'layout')[0].textContent;
  var self = this;
  WallpaperUtil.fetchURL(url, 'arraybuffer', function(xhr) {
    if (xhr.response != null) {
      chrome.wallpaperPrivate.setCustomWallpaper(xhr.response, layout, false,
                                                 'surprise_wallpaper',
                                                 onSuccess);
      WallpaperUtil.saveWallpaperInfo(url, layout,
                                      Constants.WallpaperSourceEnum.Online);
      var dateString = new Date().toDateString();
      WallpaperUtil.saveToStorage(
          Constants.AccessLastSurpriseWallpaperChangedDate, dateString, true);
    } else {
      self.updateRandomWallpaper_();
    }
  }, onFailure);
};

/**
 * Disables the wallpaper surprise me feature. Clear all alarms and states.
 */
SurpriseWallpaper.prototype.disable = function() {
  chrome.alarms.clearAll();
  // Makes last changed date invalid.
  WallpaperUtil.saveToStorage(Constants.AccessLastSurpriseWallpaperChangedDate,
                              '', true);
};

/**
 * Changes current wallpaper and sets up an alarm to schedule next change around
 * midnight.
 */
SurpriseWallpaper.prototype.next = function() {
  var nextUpdate = this.nextUpdateTime(new Date());
  chrome.alarms.create({when: nextUpdate});
  this.tryChangeWallpaper();
};

/**
 * Calculates when the next wallpaper change should be triggered.
 * @param {Date} now Current time.
 * @return {number} The time when next wallpaper change should happen.
 */
SurpriseWallpaper.prototype.nextUpdateTime = function(now) {
  var nextUpdate = new Date(now.setDate(now.getDate() + 1)).toDateString();
  return new Date(nextUpdate).getTime();
};

chrome.app.runtime.onLaunched.addListener(function() {
  if (wallpaperPickerWindow && !wallpaperPickerWindow.contentWindow.closed) {
    wallpaperPickerWindow.focus();
    chrome.wallpaperPrivate.minimizeInactiveWindows();
    return;
  }

  chrome.app.window.create('main.html', {
    frame: 'none',
    width: WALLPAPER_PICKER_WIDTH,
    height: WALLPAPER_PICKER_HEIGHT,
    resizable: false,
    alphaEnabled: true
  }, function(w) {
    wallpaperPickerWindow = w;
    chrome.wallpaperPrivate.minimizeInactiveWindows();
    w.onClosed.addListener(function() {
      chrome.wallpaperPrivate.restoreMinimizedWindows();
    });
    WallpaperUtil.testSendMessage('wallpaper-window-created');
  });
});

chrome.syncFileSystem.onFileStatusChanged.addListener(function(detail) {
  WallpaperUtil.enabledSyncThemesCallback(function() {
    if (detail.status == 'synced' &&
        detail.direction == 'remote_to_local') {
      if (detail.action == 'added') {
        Constants.WallpaperLocalStorage.get(
            Constants.AccessLocalWallpaperInfoKey,
            function(items) {
              var localData = items[Constants.AccessLocalWallpaperInfoKey];
              if (localData && localData.url == detail.fileEntry.name &&
                  localData.source == Constants.WallpaperSourceEnum.Custom) {
                WallpaperUtil.setCustomWallpaperFromSyncFS(localData.url,
                                                           localData.layout);
              } else if (localData.url !=
                         detail.fileEntry.name.replace(
                             Constants.CustomWallpaperThumbnailSuffix, '')) {
                WallpaperUtil.storeWallpaperFromSyncFSToLocalFS(
                    detail.fileEntry);
              }
           });
      } else if (detail.action == 'deleted') {
        var fileName = detail.fileEntry.name.replace(
            Constants.CustomWallpaperThumbnailSuffix, '');
        WallpaperUtil.deleteWallpaperFromLocalFS(fileName);
      }
    }
  });
});

chrome.storage.onChanged.addListener(function(changes, namespace) {
  WallpaperUtil.enabledSyncThemesCallback(function() {
    WallpaperUtil.requestSyncFS(function() {});
    if (changes[Constants.AccessSurpriseMeEnabledKey]) {
      if (changes[Constants.AccessSurpriseMeEnabledKey].newValue) {
        SurpriseWallpaper.getInstance().next();
      } else {
        SurpriseWallpaper.getInstance().disable();
      }
    }

    if (changes[Constants.AccessSyncWallpaperInfoKey]) {
      var syncInfo = changes[Constants.AccessSyncWallpaperInfoKey].newValue;

      Constants.WallpaperSyncStorage.get(Constants.AccessSurpriseMeEnabledKey,
                                         function(enabledItems) {
        var syncSurpriseMeEnabled =
            enabledItems[Constants.AccessSurpriseMeEnabledKey];

        Constants.WallpaperSyncStorage.get(
            Constants.AccessLastSurpriseWallpaperChangedDate, function(items) {
          var syncLastSurpriseMeChangedDate =
              items[Constants.AccessLastSurpriseWallpaperChangedDate];

          var today = new Date().toDateString();
          // If SurpriseMe is enabled and surprise wallpaper hasn't been changed
          // today, we should not sync the change, instead onAlarm() will be
          // triggered to update a surprise me wallpaper.
          if (!syncSurpriseMeEnabled ||
              (syncSurpriseMeEnabled &&
                  syncLastSurpriseMeChangedDate == today)) {
            Constants.WallpaperLocalStorage.get(
                Constants.AccessLocalWallpaperInfoKey, function(infoItems) {
              var localInfo = infoItems[Constants.AccessLocalWallpaperInfoKey];
              // Normally, the wallpaper info saved in local storage and sync
              // storage are the same. If the synced value changed by sync
              // service, they may different. In that case, change wallpaper to
              // the one saved in sync storage and update the local value.
              if (localInfo == undefined ||
                  localInfo.url != syncInfo.url ||
                  localInfo.layout != syncInfo.layout ||
                  localInfo.source != syncInfo.source) {
                if (syncInfo.source == Constants.WallpaperSourceEnum.Online) {
                  // TODO(bshe): Consider schedule an alarm to set online
                  // wallpaper later when failed. Note that we need to cancel
                  // the retry if user set another wallpaper before retry alarm
                  // invoked.
                  WallpaperUtil.setOnlineWallpaper(syncInfo.url,
                      syncInfo.layout, function() {}, function() {});
                } else if (syncInfo.source ==
                              Constants.WallpaperSourceEnum.Custom) {
                  WallpaperUtil.setCustomWallpaperFromSyncFS(syncInfo.url,
                                                             syncInfo.layout);
                } else if (syncInfo.source ==
                              Constants.WallpaperSourceEnum.Default) {
                  chrome.wallpaperPrivate.resetWallpaper();
                }
                WallpaperUtil.saveToStorage(
                    Constants.AccessLocalWallpaperInfoKey, syncInfo, false);
              }
            });
          }
        });
      });
    }
  });
});

chrome.alarms.onAlarm.addListener(function() {
  SurpriseWallpaper.getInstance().next();
});
