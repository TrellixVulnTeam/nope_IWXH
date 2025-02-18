# Copyright 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Runs Microsoft's RoboHornet Pro benchmark."""

import os

from metrics import power
from telemetry import benchmark
from telemetry import page as page_module
from telemetry.page import page_set
from telemetry.page import page_test
from telemetry.value import scalar


class _RobohornetProMeasurement(page_test.PageTest):
  def __init__(self):
    super(_RobohornetProMeasurement, self).__init__()
    self._power_metric = None

  def CustomizeBrowserOptions(self, options):
    power.PowerMetric.CustomizeBrowserOptions(options)

  def WillStartBrowser(self, platform):
    self._power_metric = power.PowerMetric(platform)

  def DidNavigateToPage(self, page, tab):
    self._power_metric.Start(page, tab)

  def ValidateAndMeasurePage(self, page, tab, results):
    tab.ExecuteJavaScript('ToggleRoboHornet()')
    tab.WaitForJavaScriptExpression(
        'document.getElementById("results").innerHTML.indexOf("Total") != -1',
        600)

    self._power_metric.Stop(page, tab)
    self._power_metric.AddResults(tab, results)

    result = int(tab.EvaluateJavaScript('stopTime - startTime'))
    results.AddValue(
        scalar.ScalarValue(results.current_page, 'Total', 'ms', result))


# We plan to remove this test because it doesn't give useful data, but
# we need to wait until Chrome OS can implement support for more helpful
# benchmarks.
@benchmark.Enabled('chromeos')
class RobohornetPro(benchmark.Benchmark):
  """Milliseconds to complete the RoboHornetPro demo by Microsoft.

  http://ie.microsoft.com/testdrive/performance/robohornetpro/
  """
  test = _RobohornetProMeasurement

  @classmethod
  def Name(cls):
    return 'robohornet_pro'

  def CreatePageSet(self, options):
    ps = page_set.PageSet(
        archive_data_file='../page_sets/data/robohornet_pro.json',
        file_path=os.path.abspath(__file__),
        bucket=page_set.PARTNER_BUCKET)
    ps.AddUserStory(page_module.Page(
        'http://ie.microsoft.com/testdrive/performance/robohornetpro/',
        ps, ps.base_dir,
        # Measurement require use of real Date.now() for measurement.
        make_javascript_deterministic=False))
    return ps
