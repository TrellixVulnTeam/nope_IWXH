# Copyright 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import inspect
import logging
import os
import urlparse

from telemetry import user_story
from telemetry.page import shared_page_state
from telemetry.util import cloud_storage
from telemetry.util import path


def _UpdateCredentials(credentials_path):
  # Attempt to download the credentials file.
  try:
    cloud_storage.GetIfChanged(credentials_path, cloud_storage.PUBLIC_BUCKET)
  except (cloud_storage.CredentialsError, cloud_storage.PermissionError,
          cloud_storage.CloudStorageError) as e:
    logging.warning('Cannot retrieve credential file %s due to cloud storage '
                    'error %s', credentials_path, str(e))


class Page(user_story.UserStory):
  def __init__(self, url, page_set=None, base_dir=None, name='',
               credentials_path=None, labels=None, startup_url='',
               make_javascript_deterministic=True,
               shared_page_state_class=shared_page_state.SharedPageState):
    self._url = url

    super(Page, self).__init__(
        shared_page_state_class, name=name, labels=labels,
        is_local=self._scheme in ['file', 'chrome', 'about'],
        make_javascript_deterministic=make_javascript_deterministic)

    self._page_set = page_set
    # Default value of base_dir is the directory of the file that defines the
    # class of this page instance.
    if base_dir is None:
      base_dir = os.path.dirname(inspect.getfile(self.__class__))
    self._base_dir = base_dir
    self._name = name
    if credentials_path:
      credentials_path = os.path.join(self._base_dir, credentials_path)
      _UpdateCredentials(credentials_path)
      if not os.path.exists(credentials_path):
        logging.error('Invalid credentials path: %s' % credentials_path)
        credentials_path = None
    self._credentials_path = credentials_path

    # These attributes can be set dynamically by the page.
    self.synthetic_delays = dict()
    self._startup_url = startup_url
    self.credentials = None
    self.skip_waits = False
    self.script_to_evaluate_on_commit = None
    self._SchemeErrorCheck()

  @property
  def credentials_path(self):
    return self._credentials_path

  @property
  def startup_url(self):
    return self._startup_url

  def _SchemeErrorCheck(self):
    if not self._scheme:
      raise ValueError('Must prepend the URL with scheme (e.g. file://)')

    if self.startup_url:
      startup_url_scheme = urlparse.urlparse(self.startup_url).scheme
      if not startup_url_scheme:
        raise ValueError('Must prepend the URL with scheme (e.g. http://)')
      if startup_url_scheme == 'file':
        raise ValueError('startup_url with local file scheme is not supported')

  def TransferToPageSet(self, another_page_set):
    """ Transfer this page to another page set.
    Args:
      another_page_set: an instance of telemetry.page.PageSet to transfer this
          page to.
    Note:
      This method removes this page instance from the pages list of its current
      page_set, so one should be careful not to iterate through the list of
      pages of a page_set and calling this method.
      For example, the below loop is erroneous:
        for p in page_set_A.pages:
          p.TransferToPageSet(page_set_B.pages)
    """
    assert self._page_set
    if another_page_set is self._page_set:
      return
    self._page_set.pages.remove(self)
    self._page_set = another_page_set
    self._page_set.AddUserStory(self)

  def RunNavigateSteps(self, action_runner):
    url = self.file_path_url_with_scheme if self.is_file else self.url
    action_runner.Navigate(
        url, script_to_evaluate_on_commit=self.script_to_evaluate_on_commit)

  def RunPageInteractions(self, action_runner):
    """Override this to define custom interactions with the page.
    e.g:
      def RunPageInteractions(self, action_runner):
        action_runner.ScrollPage()
        action_runner.TapElement(text='Next')
    """
    pass

  def CanRunOnBrowser(self, browser_info):
    """Override this to returns whether this page can be run on specific
    browser.

    Args:
      browser_info: an instance of telemetry.core.browser_info.BrowserInfo
    """
    assert browser_info
    return True

  def AsDict(self):
    """Converts a page object to a dict suitable for JSON output."""
    d = {
      'id': self._id,
      'url': self._url,
    }
    if self._name:
      d['name'] = self._name
    return d

  @property
  def page_set(self):
    return self._page_set

  @property
  def url(self):
    return self._url

  def GetSyntheticDelayCategories(self):
    result = []
    for delay, options in self.synthetic_delays.items():
      options = '%f;%s' % (options.get('target_duration', 0),
                           options.get('mode', 'static'))
      result.append('DELAY(%s;%s)' % (delay, options))
    return result

  def __lt__(self, other):
    return self.url < other.url

  def __cmp__(self, other):
    x = cmp(self.name, other.name)
    if x != 0:
      return x
    return cmp(self.url, other.url)

  def __str__(self):
    return self.url

  def AddCustomizeBrowserOptions(self, options):
    """ Inherit page overrides this to add customized browser options."""
    pass

  @property
  def _scheme(self):
    return urlparse.urlparse(self.url).scheme

  @property
  def is_file(self):
    """Returns True iff this URL points to a file."""
    return self._scheme == 'file'

  @property
  def file_path(self):
    """Returns the path of the file, stripping the scheme and query string."""
    assert self.is_file
    # Because ? is a valid character in a filename,
    # we have to treat the url as a non-file by removing the scheme.
    parsed_url = urlparse.urlparse(self.url[7:])
    return os.path.normpath(os.path.join(
        self._base_dir, parsed_url.netloc + parsed_url.path))

  @property
  def base_dir(self):
    return self._base_dir

  @property
  def file_path_url(self):
    """Returns the file path, including the params, query, and fragment."""
    assert self.is_file
    file_path_url = os.path.normpath(os.path.join(self._base_dir, self.url[7:]))
    # Preserve trailing slash or backslash.
    # It doesn't matter in a file path, but it does matter in a URL.
    if self.url.endswith('/'):
      file_path_url += os.sep
    return file_path_url

  @property
  def file_path_url_with_scheme(self):
    return 'file://' + self.file_path_url

  @property
  def serving_dir(self):
    file_path = os.path.realpath(self.file_path)
    if os.path.isdir(file_path):
      return file_path
    else:
      return os.path.dirname(file_path)

  @property
  def display_name(self):
    if self.name:
      return self.name
    if not self.is_file:
      return self.url
    all_urls = [p.url.rstrip('/') for p in self.page_set if p.is_file]
    common_prefix = os.path.dirname(os.path.commonprefix(all_urls))
    return self.url[len(common_prefix):].strip('/')
