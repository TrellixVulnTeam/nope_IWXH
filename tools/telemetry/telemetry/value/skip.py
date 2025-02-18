# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from telemetry import value as value_module


class SkipValue(value_module.Value):

  def __init__(self, page, reason, description=None):
    """A value representing a skipped page.

    Args:
      page: The skipped page object.
      reason: The string reason the page was skipped.
    """
    super(SkipValue, self).__init__(page, 'skip', '', True, description, None)
    self._reason = reason

  def __repr__(self):
    page_name = self.page.url
    return 'SkipValue(%s, %s)' % (page_name, self._reason)

  @property
  def reason(self):
    return self._reason

  def GetBuildbotDataType(self, output_context):
    return None

  def GetBuildbotValue(self):
    return None

  def GetChartAndTraceNameForPerPageResult(self):
    return None

  def GetRepresentativeNumber(self):
    return None

  def GetRepresentativeString(self):
    return None

  @staticmethod
  def GetJSONTypeName():
    return 'skip'

  def AsDict(self):
    d = super(SkipValue, self).AsDict()
    d['reason'] = self._reason
    return d

  @staticmethod
  def FromDict(value_dict, page_dict):
    kwargs = value_module.Value.GetConstructorKwArgs(value_dict, page_dict)
    del kwargs['name']
    del kwargs['units']
    if 'important' in kwargs:
      del kwargs['important']
    kwargs['reason'] = value_dict['reason']
    if 'interaction_record' in kwargs:
      del kwargs['interaction_record']

    return SkipValue(**kwargs)

  @classmethod
  def MergeLikeValuesFromSamePage(cls, values):
    assert False, 'Should not be called.'

  @classmethod
  def MergeLikeValuesFromDifferentPages(cls, values,
                                        group_by_name_suffix=False):
    assert False, 'Should not be called.'
