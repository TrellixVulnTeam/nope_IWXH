# Copyright (c) 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'conditions': [
    ['archive_chromoting_tests==1', {
      'targets': [
        {
          'target_name': 'chromoting_integration_tests_run',
          'type': 'none',
          'dependencies': [
            '../../chrome/chrome.gyp:browser_tests',
          ],
          'includes': [
            '../../build/isolate.gypi',
          ],
          'sources': [
            'chromoting_integration_tests.isolate',
          ],
        },
      ],
    }],
  ],
}
