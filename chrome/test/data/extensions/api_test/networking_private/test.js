// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This just tests the interface. It does not test for specific results, only
// that callbacks are correctly invoked, expected parameters are correct,
// and failures are detected.

var callbackPass = chrome.test.callbackPass;

var kFailure = 'Failure';
var kGuid = 'SOME_GUID';

// Test properties for the verification API.
var verificationProperties = {
  "certificate": "certificate",
  "intermediateCertificates": ["ica1", "ica2", "ica3"],
  "publicKey": "cHVibGljX2tleQ==",  // Base64("public_key")
  "nonce": "nonce",
  "signedData": "c2lnbmVkX2RhdGE=",  // Base64("signed_data")
  "deviceSerial": "device_serial",
  "deviceSsid": "Device 0123",
  "deviceBssid": "00:01:02:03:04:05"
};

function callbackResult(result) {
  if (chrome.runtime.lastError)
    chrome.test.fail(chrome.runtime.lastError.message);
  else if (result == false || result == kFailure)
    chrome.test.fail('Failed: ' + result);
}

var availableTests = [
  function getProperties() {
    chrome.networkingPrivate.getProperties(
        kGuid, callbackPass(callbackResult));
  },
  function getManagedProperties() {
    chrome.networkingPrivate.getManagedProperties(
        kGuid, callbackPass(callbackResult));
  },
  function getState() {
    chrome.networkingPrivate.getState(
        kGuid, callbackPass(callbackResult));
  },
  function setProperties() {
    chrome.networkingPrivate.setProperties(
        kGuid, { 'GUID': kGuid }, callbackPass(callbackResult));
  },
  function createNetwork() {
    chrome.networkingPrivate.createNetwork(
        false, { 'GUID': kGuid }, callbackPass(callbackResult));
  },
  function getNetworks() {
    chrome.networkingPrivate.getNetworks(
        { networkType: 'Ethernet' }, callbackPass(callbackResult));
  },
  function getVisibleNetworks() {
    chrome.networkingPrivate.getVisibleNetworks(
        'Ethernet', callbackPass(callbackResult));
  },
  function getEnabledNetworkTypes() {
    chrome.networkingPrivate.getEnabledNetworkTypes(
        callbackPass(callbackResult));
  },
  function enableNetworkType() {
    chrome.networkingPrivate.enableNetworkType('Ethernet');
    chrome.test.succeed();
  },
  function disableNetworkType() {
    chrome.networkingPrivate.disableNetworkType('Ethernet');
    chrome.test.succeed();
  },
  function requestNetworkScan() {
    chrome.networkingPrivate.requestNetworkScan();
    chrome.test.succeed();
  },
  function startConnect() {
    chrome.networkingPrivate.startConnect(
        kGuid, callbackPass(callbackResult));
  },
  function startDisconnect() {
    chrome.networkingPrivate.startDisconnect(
        kGuid, callbackPass(callbackResult));
  },
  function startActivate() {
    chrome.networkingPrivate.startActivate(
        kGuid, '' /* carrier */, callbackPass(callbackResult));
  },
  function verifyDestination() {
    chrome.networkingPrivate.verifyDestination(
        verificationProperties, callbackPass(callbackResult));
  },
  function verifyAndEncryptCredentials() {
    chrome.networkingPrivate.verifyAndEncryptCredentials(
        verificationProperties, kGuid, callbackPass(callbackResult));
  },
  function verifyAndEncryptData() {
    chrome.networkingPrivate.verifyAndEncryptData(
        verificationProperties, 'data', callbackPass(callbackResult));
  },
  function setWifiTDLSEnabledState() {
    chrome.networkingPrivate.setWifiTDLSEnabledState(
        '', true, callbackPass(callbackResult));
  },
  function getWifiTDLSStatus() {
    chrome.networkingPrivate.getWifiTDLSStatus(
        '', callbackPass(callbackResult));
  },
  function getCaptivePortalStatus() {
    chrome.networkingPrivate.getWifiTDLSStatus(
        kGuid, callbackPass(callbackResult));
  },
];

var testToRun = window.location.search.substring(1);
chrome.test.runTests(availableTests.filter(function(op) {
  return op.name == testToRun;
}));
