// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This module implements WebView (<webview>) as a custom element that wraps a
// BrowserPlugin object element. The object element is hidden within
// the shadow DOM of the WebView element.

var DocumentNatives = requireNative('document_natives');
var GuestView = require('guestView').GuestView;
var GuestViewContainer = require('guestViewContainer').GuestViewContainer;
var WebViewConstants = require('webViewConstants').WebViewConstants;
var WebViewEvents = require('webViewEvents').WebViewEvents;
var WebViewInternal = require('webViewInternal').WebViewInternal;

// Represents the internal state of <webview>.
function WebViewImpl(webviewElement) {
  GuestViewContainer.call(this, webviewElement, 'webview');

  this.setupWebViewAttributes();
  this.setupElementProperties();

  new WebViewEvents(this, this.viewInstanceId);
}

WebViewImpl.prototype.__proto__ = GuestViewContainer.prototype;

WebViewImpl.VIEW_TYPE = 'WebView';

// Add extra functionality to |this.element|.
WebViewImpl.setupElement = function(proto) {
  // Public-facing API methods.
  var apiMethods = WebViewImpl.getApiMethods();

  // Add the experimental API methods, if available.
  var experimentalApiMethods =
      WebViewImpl.maybeGetExperimentalApiMethods();
  apiMethods = $Array.concat(apiMethods, experimentalApiMethods);

  // Create default implementations for undefined API methods.
  var createDefaultApiMethod = function(m) {
    return function(var_args) {
      if (!this.guest.getId()) {
        return false;
      }
      var args = $Array.concat([this.guest.getId()], $Array.slice(arguments));
      $Function.apply(WebViewInternal[m], null, args);
      return true;
    };
  };
  for (var i = 0; i != apiMethods.length; ++i) {
    if (WebViewImpl.prototype[apiMethods[i]] == undefined) {
      WebViewImpl.prototype[apiMethods[i]] =
          createDefaultApiMethod(apiMethods[i]);
    }
  }

  // Forward proto.foo* method calls to WebViewImpl.foo*.
  GuestViewContainer.forwardApiMethods(proto, apiMethods);
};

// Initiates navigation once the <webview> element is attached to the DOM.
WebViewImpl.prototype.onElementAttached = function() {
  for (var i in this.attributes) {
    this.attributes[i].attach();
  }
};

// Resets some state upon detaching <webview> element from the DOM.
WebViewImpl.prototype.onElementDetached = function() {
  this.guest.destroy();
  for (var i in this.attributes) {
    this.attributes[i].detach();
  }
};

// Sets the <webview>.request property.
WebViewImpl.prototype.setRequestPropertyOnWebViewElement = function(request) {
  Object.defineProperty(
      this.element,
      'request',
      {
        value: request,
        enumerable: true
      }
  );
};

WebViewImpl.prototype.setupElementProperties = function() {
  // We cannot use {writable: true} property descriptor because we want a
  // dynamic getter value.
  Object.defineProperty(this.element, 'contentWindow', {
    get: function() {
      if (this.guest.getContentWindow()) {
        return this.guest.getContentWindow();
      }
      window.console.error(
          WebViewConstants.ERROR_MSG_CONTENTWINDOW_NOT_AVAILABLE);
    }.bind(this),
    // No setter.
    enumerable: true
  });
};

// This observer monitors mutations to attributes of the <webview>.
WebViewImpl.prototype.handleAttributeMutation = function(
    attributeName, oldValue, newValue) {
  if (!this.attributes[attributeName]) {
    return;
  }

  // Let the changed attribute handle its own mutation;
  this.attributes[attributeName].maybeHandleMutation(oldValue, newValue);
};

WebViewImpl.prototype.onSizeChanged = function(webViewEvent) {
  var newWidth = webViewEvent.newWidth;
  var newHeight = webViewEvent.newHeight;

  var element = this.element;

  var width = element.offsetWidth;
  var height = element.offsetHeight;

  // Check the current bounds to make sure we do not resize <webview>
  // outside of current constraints.
  var maxWidth = this.attributes[
    WebViewConstants.ATTRIBUTE_MAXWIDTH].getValue() || width;
  var minWidth = this.attributes[
    WebViewConstants.ATTRIBUTE_MINWIDTH].getValue() || width;
  var maxHeight = this.attributes[
    WebViewConstants.ATTRIBUTE_MAXHEIGHT].getValue() || height;
  var minHeight = this.attributes[
    WebViewConstants.ATTRIBUTE_MINHEIGHT].getValue() || height;

  minWidth = Math.min(minWidth, maxWidth);
  minHeight = Math.min(minHeight, maxHeight);

  if (!this.attributes[WebViewConstants.ATTRIBUTE_AUTOSIZE].getValue() ||
      (newWidth >= minWidth &&
      newWidth <= maxWidth &&
      newHeight >= minHeight &&
      newHeight <= maxHeight)) {
    element.style.width = newWidth + 'px';
    element.style.height = newHeight + 'px';
    // Only fire the DOM event if the size of the <webview> has actually
    // changed.
    this.dispatchEvent(webViewEvent);
  }
};

WebViewImpl.prototype.createGuest = function() {
  this.guest.create(this.buildParams(), function() {
    this.attachWindow();
  }.bind(this));
};

WebViewImpl.prototype.onFrameNameChanged = function(name) {
  this.attributes[WebViewConstants.ATTRIBUTE_NAME].setValueIgnoreMutation(name);
};

// Updates state upon loadcommit.
WebViewImpl.prototype.onLoadCommit = function(
    baseUrlForDataUrl, currentEntryIndex, entryCount,
    processId, url, isTopLevel) {
  this.baseUrlForDataUrl = baseUrlForDataUrl;
  this.currentEntryIndex = currentEntryIndex;
  this.entryCount = entryCount;
  this.processId = processId;
  if (isTopLevel) {
    // Touching the src attribute triggers a navigation. To avoid
    // triggering a page reload on every guest-initiated navigation,
    // we do not handle this mutation.
    this.attributes[
        WebViewConstants.ATTRIBUTE_SRC].setValueIgnoreMutation(url);
  }
};

WebViewImpl.prototype.onAttach = function(storagePartitionId) {
  this.attributes[WebViewConstants.ATTRIBUTE_PARTITION].setValueIgnoreMutation(
      storagePartitionId);
};

WebViewImpl.prototype.buildContainerParams = function() {
  var params = { 'userAgentOverride': this.userAgentOverride };
  for (var i in this.attributes) {
    params[i] = this.attributes[i].getValue();
  }
  return params;
};

WebViewImpl.prototype.attachWindow = function(opt_guestInstanceId) {
  // If |opt_guestInstanceId| was provided, then a different existing guest is
  // being attached to this webview, and the current one will get destroyed.
  if (opt_guestInstanceId) {
    if (this.guest.getId() == opt_guestInstanceId) {
      return true;
    }
    this.guest.destroy();
    this.guest = new GuestView('webview', opt_guestInstanceId);
  }

  return GuestViewContainer.prototype.attachWindow.call(this);
};

// Shared implementation of executeScript() and insertCSS().
WebViewImpl.prototype.executeCode = function(func, args) {
  if (!this.guest.getId()) {
    window.console.error(WebViewConstants.ERROR_MSG_CANNOT_INJECT_SCRIPT);
    return false;
  }

  var webviewSrc = this.attributes[WebViewConstants.ATTRIBUTE_SRC].getValue();
  if (this.baseUrlForDataUrl) {
    webviewSrc = this.baseUrlForDataUrl;
  }

  args = $Array.concat([this.guest.getId(), webviewSrc],
                       $Array.slice(args));
  $Function.apply(func, null, args);
  return true;
}

// Implemented when the ChromeWebView API is available.
WebViewImpl.prototype.maybeGetChromeWebViewEvents = function() {};

// Implemented when the experimental WebView API is available.
WebViewImpl.maybeGetExperimentalApiMethods = function() { return []; };
WebViewImpl.prototype.setupExperimentalContextMenus = function() {};

GuestViewContainer.registerElement(WebViewImpl);

// Exports.
exports.WebViewImpl = WebViewImpl;
