// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('downloads', function() {
  /** @const */ var Item = downloads.Item;

  /**
   * Creates and updates the DOM representation for a download.
   * @constructor
   */
  function ItemView() {
    this.node = $('templates').querySelector('.download').cloneNode(true);

    this.safe_ = this.queryRequired_('.safe');
    this.since_ = this.queryRequired_('.since');
    this.dateContainer = this.queryRequired_('.date-container');
    this.date_ = this.queryRequired_('.date');
    this.save_ = this.queryRequired_('.save');
    this.backgroundProgress_ = this.queryRequired_('.progress.background');
    this.foregroundProgress_ = /** @type !HTMLCanvasElement */(
        this.queryRequired_('canvas.progress'));
    this.safeImg_ = /** @type !HTMLImageElement */(
        this.queryRequired_('.safe img'));
    this.fileName_ = this.queryRequired_('span.name');
    this.fileLink = this.queryRequired_('[is="action-link"].name');
    this.status_ = this.queryRequired_('.status');
    this.srcUrl = this.queryRequired_('.src-url');
    this.show = this.queryRequired_('.show');
    this.retry = this.queryRequired_('.retry');
    this.pause = this.queryRequired_('.pause');
    this.resume = this.queryRequired_('.resume');
    this.safeRemove = this.queryRequired_('.safe .remove');
    this.cancel = this.queryRequired_('.cancel');
    this.controlledBy = this.queryRequired_('.controlled-by');

    this.dangerous_ = this.queryRequired_('.dangerous');
    this.dangerImg_ = /** @type {!HTMLImageElement} */(
        this.queryRequired_('.dangerous img'));
    this.description_ = this.queryRequired_('.description');
    this.malwareControls_ = this.queryRequired_('.dangerous .controls');
    this.restore = this.queryRequired_('.restore');
    this.dangerRemove = this.queryRequired_('.dangerous .remove');
    this.save = this.queryRequired_('.save');
    this.discard = this.queryRequired_('.discard');

    // Event handlers (bound once on creation).
    this.safe_.ondragstart = this.onSafeDragstart_.bind(this);
    this.fileLink.onclick = this.onFileLinkClick_.bind(this);
    this.show.onclick = this.onShowClick_.bind(this);
    this.pause.onclick = this.onPauseClick_.bind(this);
    this.resume.onclick = this.onResumeClick_.bind(this);
    this.safeRemove.onclick = this.onSafeRemoveClick_.bind(this);
    this.cancel.onclick = this.onCancelClick_.bind(this);
    this.restore.onclick = this.onRestoreClick_.bind(this);
    this.save.onclick = this.onSaveClick_.bind(this);
    this.dangerRemove.onclick = this.onDangerRemoveClick_.bind(this);
    this.discard.onclick = this.onDiscardClick_.bind(this);
  }

  /** Progress meter constants. */
  ItemView.Progress = {
    /** @const {number} */
    START_ANGLE: -0.5 * Math.PI,
    /** @const {number} */
    SIDE: 48,
  };

  /** @const {number} */
  ItemView.Progress.HALF = ItemView.Progress.SIDE / 2;

  ItemView.computeDownloadProgress = function() {
    /**
     * @param {number} a Some float.
     * @param {number} b Some float.
     * @param {number=} opt_pct Percent of min(a,b).
     * @return {boolean} true if a is within opt_pct percent of b.
     */
    function floatEq(a, b, opt_pct) {
      return Math.abs(a - b) < (Math.min(a, b) * (opt_pct || 1.0) / 100.0);
    }

    if (floatEq(ItemView.Progress.scale, window.devicePixelRatio)) {
      // Zooming in or out multiple times then typing Ctrl+0 resets the zoom
      // level directly to 1x, which fires the matchMedia event multiple times.
      return;
    }
    var Progress = ItemView.Progress;
    Progress.scale = window.devicePixelRatio;
    Progress.width = Progress.SIDE * Progress.scale;
    Progress.height = Progress.SIDE * Progress.scale;
    Progress.radius = Progress.HALF * Progress.scale;
    Progress.centerX = Progress.HALF * Progress.scale;
    Progress.centerY = Progress.HALF * Progress.scale;
  };
  ItemView.computeDownloadProgress();

  // Listens for when device-pixel-ratio changes between any zoom level.
  [0.3, 0.4, 0.6, 0.7, 0.8, 0.95, 1.05, 1.2, 1.4, 1.6, 1.9, 2.2, 2.7, 3.5, 4.5].
      forEach(function(scale) {
    var media = '(-webkit-min-device-pixel-ratio:' + scale + ')';
    window.matchMedia(media).addListener(ItemView.computeDownloadProgress);
  });

  /**
   * @return {!HTMLImageElement} The correct <img> to show when an item is
   *     progressing in the foreground.
   */
  ItemView.getForegroundProgressImage = function() {
    var x = window.devicePixelRatio >= 2 ? '2x' : '1x';
    ItemView.foregroundImages_ = ItemView.foregroundImages_ || {};
    if (!ItemView.foregroundImages_[x]) {
      ItemView.foregroundImages_[x] = new Image;
      var IMAGE_URL = 'chrome://theme/IDR_DOWNLOAD_PROGRESS_FOREGROUND_32';
      ItemView.foregroundImages_[x].src = IMAGE_URL + '@' + x;
    }
    return ItemView.foregroundImages_[x];
  };

  /** @private {Array<{img: HTMLImageElement, url: string}>} */
  ItemView.iconsToLoad_ = [];

  /**
   * Load the provided |url| into |img.src| after appending ?scale=.
   * @param {!HTMLImageElement} img An <img> to show the loaded image in.
   * @param {string} url A remote image URL to load.
   */
  ItemView.loadScaledIcon = function(img, url) {
    var scale = '?scale=' + window.devicePixelRatio + 'x';
    ItemView.iconsToLoad_.push({img: img, url: url + scale});
    ItemView.loadNextIcon_();
  };

  /** @private */
  ItemView.loadNextIcon_ = function() {
    if (ItemView.isIconLoading_)
      return;

    ItemView.isIconLoading_ = true;

    while (ItemView.iconsToLoad_.length) {
      var request = ItemView.iconsToLoad_.shift();
      var img = request.img;

      if (img.src == request.url)
        continue;

      img.onabort = img.onerror = img.onload = function() {
        ItemView.isIconLoading_ = false;
        ItemView.loadNextIcon_();
      };

      img.src = request.url;
      return;
    }

    // If we reached here, there's no more work to do.
    ItemView.isIconLoading_ = false;
  };

  ItemView.prototype = {
    /** @param {!downloads.Data} data */
    update: function(data) {
      assert(!this.id_ || data.id == this.id_);
      this.id_ = data.id;  // This is the only thing saved from |data|.

      this.node.classList.toggle('otr', data.otr);

      var dangerText = this.getDangerText_(data);
      this.dangerous_.hidden = !dangerText;
      this.safe_.hidden = !!dangerText;

      if (dangerText) {
        this.ensureTextIs_(this.description_, dangerText);

        var dangerousFile = data.danger_type == Item.DangerType.DANGEROUS_FILE;
        this.description_.classList.toggle('malware', !dangerousFile);

        var idr = dangerousFile ? 'IDR_WARNING' : 'IDR_SAFEBROWSING_WARNING';
        ItemView.loadScaledIcon(this.dangerImg_, 'chrome://theme/' + idr);

        var showMalwareControls =
            data.danger_type == Item.DangerType.DANGEROUS_CONTENT ||
            data.danger_type == Item.DangerType.DANGEROUS_HOST ||
            data.danger_type == Item.DangerType.DANGEROUS_URL ||
            data.danger_type == Item.DangerType.POTENTIALLY_UNWANTED;

        this.malwareControls_.hidden = !showMalwareControls;
        this.discard.hidden = showMalwareControls;
        this.save.hidden = showMalwareControls;
      } else {
        var path = encodeURIComponent(data.file_path);
        ItemView.loadScaledIcon(this.safeImg_, 'chrome://fileicon/' + path);

        /** @const */ var isInProgress = data.state == Item.States.IN_PROGRESS;
        this.node.classList.toggle('in-progress', isInProgress);

        /** @const */ var completelyOnDisk =
            data.state == Item.States.COMPLETE && !data.file_externally_removed;

        this.fileLink.href = data.url;
        this.ensureTextIs_(this.fileLink, data.file_name);
        this.fileLink.hidden = !completelyOnDisk;

        /** @const */ var isInterrupted = data.state == Item.States.INTERRUPTED;
        this.fileName_.classList.toggle('interrupted', isInterrupted);
        this.ensureTextIs_(this.fileName_, data.file_name);
        this.fileName_.hidden = completelyOnDisk;

        this.show.hidden = !completelyOnDisk;

        this.retry.href = data.url;
        this.retry.hidden = !data.retry;

        this.pause.hidden = !isInProgress;

        this.resume.hidden = !data.resume;

        /** @const */ var isPaused = data.state == Item.States.PAUSED;
        /** @const */ var showCancel = isPaused || isInProgress;
        this.cancel.hidden = !showCancel;

        this.safeRemove.hidden = showCancel ||
            !loadTimeData.getBoolean('allow_deleting_history');

        /** @const */ var controlledByExtension = data.by_ext_id &&
                                                  data.by_ext_name;
        this.controlledBy.hidden = !controlledByExtension;
        if (controlledByExtension) {
          var link = this.controlledBy.querySelector('a');
          link.href = 'chrome://extensions#' + data.by_ext_id;
          link.textContent = data.by_ext_name;
        }

        this.ensureTextIs_(this.since_, data.since_string);
        this.ensureTextIs_(this.date_, data.date_string);
        this.ensureTextIs_(this.srcUrl, data.url);
        this.srcUrl.href = data.url;
        this.ensureTextIs_(this.status_, this.getStatusText_(data));

        this.foregroundProgress_.hidden = !isInProgress;
        this.backgroundProgress_.hidden = !isInProgress;

        if (isInProgress) {
          this.foregroundProgress_.width = ItemView.Progress.width;
          this.foregroundProgress_.height = ItemView.Progress.height;

          if (!this.progressContext_) {
            /** @private */
            this.progressContext_ = /** @type !CanvasRenderingContext2D */(
                this.foregroundProgress_.getContext('2d'));
          }

          var foregroundImage = ItemView.getForegroundProgressImage();

          // Draw a pie-slice for the progress.
          this.progressContext_.globalCompositeOperation = 'copy';
          this.progressContext_.drawImage(
              foregroundImage,
              0, 0,  // sx, sy
              foregroundImage.width,
              foregroundImage.height,
              0, 0,  // x, y
              ItemView.Progress.width, ItemView.Progress.height);

          this.progressContext_.globalCompositeOperation = 'destination-in';
          this.progressContext_.beginPath();
          this.progressContext_.moveTo(ItemView.Progress.centerX,
                                       ItemView.Progress.centerY);

          // Draw an arc CW for both RTL and LTR. http://crbug.com/13215
          this.progressContext_.arc(
              ItemView.Progress.centerX,
              ItemView.Progress.centerY,
              ItemView.Progress.radius,
              ItemView.Progress.START_ANGLE,
              ItemView.Progress.START_ANGLE + Math.PI * 0.02 * data.percent,
              false);

          this.progressContext_.lineTo(ItemView.Progress.centerX,
                                       ItemView.Progress.centerY);
          this.progressContext_.fill();
          this.progressContext_.closePath();
        }
      }
    },

    destroy: function() {
      if (this.node.parentNode)
        this.node.parentNode.removeChild(this.node);
    },

    /**
     * @param {string} selector A CSS selector (e.g. '.class-name').
     * @return {!Element} The element found by querying for |selector|.
     * @private
     */
    queryRequired_: function(selector) {
      return assert(this.node.querySelector(selector));
    },

    /**
     * Overwrite |el|'s textContent if it differs from |text|.
     * @param {!Element} el
     * @param {string} text
     * @private
     */
    ensureTextIs_: function(el, text) {
      if (el.textContent != text)
        el.textContent = text;
    },

    /**
     * @param {!downloads.Data} data
     * @return {string} Text describing the danger of a download. Empty if not
     *     dangerous.
     */
    getDangerText_: function(data) {
      switch (data.danger_type) {
        case Item.DangerType.DANGEROUS_FILE:
          return loadTimeData.getStringF('danger_file_desc', data.file_name);
        case Item.DangerType.DANGEROUS_URL:
          return loadTimeData.getString('danger_url_desc');
        case Item.DangerType.DANGEROUS_CONTENT:  // Fall through.
        case Item.DangerType.DANGEROUS_HOST:
          return loadTimeData.getStringF('danger_content_desc', data.file_name);
        case Item.DangerType.UNCOMMON_CONTENT:
          return loadTimeData.getStringF('danger_uncommon_desc',
                                         data.file_name);
        case Item.DangerType.POTENTIALLY_UNWANTED:
          return loadTimeData.getStringF('danger_settings_desc',
                                         data.file_name);
        default:
          return '';
      }
    },

    /**
     * @param {!downloads.Data} data
     * @return {string} User-visible status update text.
     * @private
     */
    getStatusText_: function(data) {
      switch (data.state) {
        case Item.States.IN_PROGRESS:
        case Item.States.PAUSED:  // Fallthrough.
          return assert(data.progress_status_text);
        case Item.States.CANCELLED:
          return loadTimeData.getString('status_cancelled');
        case Item.States.DANGEROUS:
          break;  // Intentionally hit assertNotReached(); at bottom.
        case Item.States.INTERRUPTED:
          return assert(data.last_reason_text);
        case Item.States.COMPLETE:
          return data.file_externally_removed ?
              loadTimeData.getString('status_removed') : '';
      }
      assertNotReached();
      return '';
    },

    /**
     * @private
     * @param {Event} e
     */
    onSafeDragstart_: function(e) {
      e.preventDefault();
      chrome.send('drag', [this.id_]);
    },

    /**
     * @param {Event} e
     * @private
     */
    onFileLinkClick_: function(e) {
      e.preventDefault();
      chrome.send('openFile', [this.id_]);
    },

    /** @private */
    onShowClick_: function() {
      chrome.send('show', [this.id_]);
    },

    /** @private */
    onPauseClick_: function() {
      chrome.send('pause', [this.id_]);
    },

    /** @private */
    onResumeClick_: function() {
      chrome.send('resume', [this.id_]);
    },

    /** @private */
    onSafeRemoveClick_: function() {
      chrome.send('remove', [this.id_]);
    },

    /** @private */
    onCancelClick_: function() {
      chrome.send('cancel', [this.id_]);
    },

    /** @private */
    onRestoreClick_: function() {
      this.onSaveClick_();
    },

    /** @private */
    onSaveClick_: function() {
      chrome.send('saveDangerous', [this.id_]);
    },

    /** @private */
    onDangerRemoveClick_: function() {
      this.onDiscardClick_();
    },

    /** @private */
    onDiscardClick_: function() {
      chrome.send('discardDangerous', [this.id_]);
    },
  };

  return {ItemView: ItemView};
});
