// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var chrome;

loadTimeData.data = {
  DRIVE_DIRECTORY_LABEL: 'My Drive',
  DOWNLOADS_DIRECTORY_LABEL: 'Downloads'
};

function setUp() {
  // Set up mock of chrome.fileManagerPrivate APIs.
  chrome = {
    fileManagerPrivate: {
      mountSourcePath_: null,
      onMountCompletedListeners_: [],
      onDriveConnectionStatusChangedListeners_: [],
      addMount: function(fileUrl, callback) {
        callback(chrome.fileManagerPrivate.mountSourcePath_);
      },
      removeMount: function(volumeId) {
        var event = {
          eventType: 'unmount',
          status: 'success',
          volumeMetadata: {
            volumeId: volumeId
          }
        };
        chrome.fileManagerPrivate.onMountCompleted.dispatchEvent(event);
      },
      onDriveConnectionStatusChanged: {
        addListener: function(listener) {
          chrome.fileManagerPrivate.onDriveConnectionStatusChangedListeners_
              .push(listener);
        },
        dispatchEvent: function(event) {
          chrome.fileManagerPrivate
              .onDriveConnectionStatusChangedListeners_
              .forEach(function(listener) { listener(event); });
        }
      },
      onMountCompleted: {
        addListener: function(listener) {
          chrome.fileManagerPrivate.onMountCompletedListeners_.push(listener);
        },
        dispatchEvent: function(event) {
          chrome.fileManagerPrivate
              .onMountCompletedListeners_.forEach(function(listener) {
            listener(event);
          });
        }
      },
      getDriveConnectionState: function(callback) {
        callback(chrome.fileManagerPrivate.driveConnectionState_);
      },
      getVolumeMetadataList: function(callback) {
        callback(chrome.fileManagerPrivate.volumeMetadataList_);
      },
      requestFileSystem: function(volumeId, callback) {
        callback(chrome.fileManagerPrivate.fileSystemMap_[volumeId]);
      },
      set driveConnectionState(state) {
        chrome.fileManagerPrivate.driveConnectionState_ = state;
        chrome.fileManagerPrivate.onDriveConnectionStatusChanged
            .dispatchEvent(null);
      }
    }
  };

  chrome.fileManagerPrivate.mountSourcePath_ = null;
  chrome.fileManagerPrivate.onMountCompletedListeners_ = [];
  chrome.fileManagerPrivate.onDriveConnectionStatusChangedListeners_ = [];
  chrome.fileManagerPrivate.driveConnectionState_ =
      VolumeManagerCommon.DriveConnectionType.ONLINE;
  chrome.fileManagerPrivate.volumeMetadataList_ = [
    {
      volumeId: 'download:Downloads',
      volumeLabel: '',
      volumeType: VolumeManagerCommon.VolumeType.DOWNLOADS,
      isReadOnly: false,
      profile: getMockProfile()
    },
    {
      volumeId: 'drive:drive-foobar%40chromium.org-hash',
      volumeLabel: '',
      volumeType: VolumeManagerCommon.VolumeType.DRIVE,
      isReadOnly: false,
      profile: getMockProfile()
    }
  ];
  chrome.fileManagerPrivate.fileSystemMap_ = {
    'download:Downloads': new MockFileSystem('download:Downloads'),
    'drive:drive-foobar%40chromium.org-hash':
        new MockFileSystem('drive:drive-foobar%40chromium.org-hash')
  };
}

function tearDown() {
  VolumeManager.revokeInstanceForTesting();
  chrome = null;
}

/**
 * Returns a mock profile.
 *
 * @return {{displayName:string, isCurrentProfile:boolean, profileId:string}}
 *     Mock profile
 */
function getMockProfile() {
  return {
    displayName: 'foobar@chromium.org',
    isCurrentProfile: true,
    profileId: ''
  };
}

function testGetVolumeInfo(callback) {
  reportPromise(VolumeManager.getInstance().then(function(volumeManager) {
    var entry = new MockFileEntry(new MockFileSystem('download:Downloads'),
        '/foo/bar/bla.zip');

    var volumeInfo = volumeManager.getVolumeInfo(entry);
    assertEquals('download:Downloads', volumeInfo.volumeId);
    assertEquals(VolumeManagerCommon.VolumeType.DOWNLOADS,
        volumeInfo.volumeType);
  }), callback);
}

function testGetDriveConnectionState(callback) {
  reportPromise(VolumeManager.getInstance().then(function(volumeManager) {
    // Default connection state is online
    assertEquals(VolumeManagerCommon.DriveConnectionType.ONLINE,
        volumeManager.getDriveConnectionState());

    // Sets it to offline.
    chrome.fileManagerPrivate.driveConnectionState =
        VolumeManagerCommon.DriveConnectionType.OFFLINE;
    assertEquals(VolumeManagerCommon.DriveConnectionType.OFFLINE,
        volumeManager.getDriveConnectionState());

    // Sets it back to online
    chrome.fileManagerPrivate.driveConnectionState =
        VolumeManagerCommon.DriveConnectionType.ONLINE;
    assertEquals(VolumeManagerCommon.DriveConnectionType.ONLINE,
        volumeManager.getDriveConnectionState());
  }), callback);
}

function testMountArchiveAndUnmount(callback) {
  // Set states of mock fileManagerPrivate APIs.
  const mountSourcePath = '/usr/local/home/test/Downloads/foobar.zip';
  chrome.fileManagerPrivate.mountSourcePath_ = mountSourcePath;
  chrome.fileManagerPrivate.fileSystemMap_['archive:foobar.zip'] =
      new MockFileSystem('archive:foobar.zip');

  reportPromise(VolumeManager.getInstance().then(function(volumeManager) {
    var numberOfVolumes = volumeManager.volumeInfoList.length;

    return new Promise(function(resolve, reject) {
      // Mount an archieve
      volumeManager.mountArchive(
          'filesystem:chrome-extension://extensionid/external/Downloads-test/' +
          'foobar.zip',
          resolve, reject);

      chrome.fileManagerPrivate.onMountCompleted.dispatchEvent({
        eventType: 'mount',
        status: 'success',
        volumeMetadata: {
          volumeId: 'archive:foobar.zip',
          volumeLabel: 'foobar.zip',
          volumeType: VolumeManagerCommon.VolumeType.ARCHIVE,
          isReadOnly: true,
          sourcePath: mountSourcePath,
          profile: getMockProfile()
        }
      });
    }).then(function(result) {
      assertEquals(numberOfVolumes + 1, volumeManager.volumeInfoList.length);

      return new Promise(function(resolve, reject) {
        // Unmount the mounted archievea
        volumeManager.volumeInfoList.addEventListener('splice', function(e) {
          assertEquals(numberOfVolumes, volumeManager.volumeInfoList.length);
          resolve(true);
        });
        var entry = new MockFileEntry(
            new MockFileSystem('archive:foobar.zip'),
            '/foo.txt');
        var volumeInfo = volumeManager.getVolumeInfo(entry);
        volumeManager.unmount(volumeInfo);
      });
    });
  }), callback);
}

function testGetCurrentProfileVolumeInfo(callback) {
  reportPromise(VolumeManager.getInstance().then(function(volumeManager) {
    var volumeInfo = volumeManager.getCurrentProfileVolumeInfo(
        VolumeManagerCommon.VolumeType.DRIVE);

    assertEquals('drive:drive-foobar%40chromium.org-hash',
        volumeInfo.volumeId);
    assertEquals(VolumeManagerCommon.VolumeType.DRIVE, volumeInfo.volumeType);
  }), callback);
}

function testGetLocationInfo(callback) {
  reportPromise(VolumeManager.getInstance().then(function(volumeManager) {
    var downloadEntry = new MockFileEntry(
        new MockFileSystem('download:Downloads'),
        '/foo/bar/bla.zip');
    var downloadLocationInfo = volumeManager.getLocationInfo(downloadEntry);
    assertEquals(VolumeManagerCommon.VolumeType.DOWNLOADS,
        downloadLocationInfo.rootType);
    assertFalse(downloadLocationInfo.isReadOnly);
    assertFalse(downloadLocationInfo.isRootEntry);

    var driveEntry = new MockFileEntry(
        new MockFileSystem('drive:drive-foobar%40chromium.org-hash'),
        '/root');
    var driveLocationInfo = volumeManager.getLocationInfo(driveEntry);
    assertEquals(VolumeManagerCommon.VolumeType.DRIVE,
        driveLocationInfo.rootType);
    assertFalse(driveLocationInfo.isReadOnly);
    assertTrue(driveLocationInfo.isRootEntry);
  }), callback);
}

function testVolumeInfoListWhenReady(callback) {
  var list = new VolumeInfoList();
  var promiseBeforeAdd = list.whenVolumeInfoReady('volumeId');
  var volumeInfo = new VolumeInfo(
      /* volumeType */ null,
      'volumeId',
      /* fileSystem */ null,
      /* error */ null,
      /* deviceType */ null,
      /* devicePath */ null,
      /* isReadOnly */ false,
      /* profile */ {},
      /* label */ null,
      /* extensionid */ null,
      /* hasMedia */ false);
  list.add(volumeInfo);
  var promiseAfterAdd = list.whenVolumeInfoReady('volumeId');
  reportPromise(Promise.all([promiseBeforeAdd, promiseAfterAdd]).then(
      function(volumes) {
        assertEquals(volumeInfo, volumes[0]);
        assertEquals(volumeInfo, volumes[1]);
      }), callback);
}

function testDriveMountedDuringInitialization(callback) {
  var sendMetadataListCallback;
  chrome.fileManagerPrivate.getVolumeMetadataList = function(callback) {
    sendMetadataListCallback = callback;
  };

  // Start initialization.
  var instancePromise = VolumeManager.getInstance();

  // Drive is mounted during initialization.
  chrome.fileManagerPrivate.onMountCompleted.dispatchEvent({
    eventType: 'mount',
    status: 'success',
    volumeMetadata: {
      volumeId: 'drive',
      volumeType: VolumeManagerCommon.VolumeType.DRIVE,
      sourcePath: '/drive',
      profile: getMockProfile()
    }
  });

  // Complete initialization.
  sendMetadataListCallback([]);

  reportPromise(instancePromise.then(function(volumeManager) {
    assertTrue(!!volumeManager.getCurrentProfileVolumeInfo(
        VolumeManagerCommon.VolumeType.DRIVE));
  }), callback);
}
