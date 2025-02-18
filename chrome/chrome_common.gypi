# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    # File lists shared with GN build.
    'chrome_common_sources': [
      'common/all_messages.h',
      'common/attrition_experiments.h',
      'common/auto_start_linux.cc',
      'common/auto_start_linux.h',
      'common/badge_util.cc',
      'common/badge_util.h',
      'common/child_process_logging.h',
      'common/child_process_logging_win.cc',
      'common/chrome_content_client.cc',
      'common/chrome_content_client.h',
      'common/chrome_content_client_constants.cc',
      'common/chrome_content_client_ios.mm',
      'common/chrome_result_codes.h',
      'common/chrome_utility_messages.h',
      'common/chrome_utility_printing_messages.h',
      'common/chrome_version_info.cc',
      'common/chrome_version_info.h',
      'common/chrome_version_info_android.cc',
      'common/chrome_version_info_chromeos.cc',
      'common/chrome_version_info_mac.mm',
      'common/chrome_version_info_posix.cc',
      'common/chrome_version_info_win.cc',
      'common/cloud_print/cloud_print_cdd_conversion.cc',
      'common/cloud_print/cloud_print_cdd_conversion.h',
      'common/cloud_print/cloud_print_class_mac.h',
      'common/cloud_print/cloud_print_class_mac.mm',
      'common/cloud_print/cloud_print_constants.cc',
      'common/cloud_print/cloud_print_constants.h',
      'common/cloud_print/cloud_print_helpers.cc',
      'common/cloud_print/cloud_print_helpers.h',
      'common/cloud_print/cloud_print_proxy_info.cc',
      'common/cloud_print/cloud_print_proxy_info.h',
      'common/common_message_generator.cc',
      'common/common_message_generator.h',
      'common/common_param_traits.cc',
      'common/common_param_traits.h',
      'common/common_param_traits_macros.h',
      'common/content_restriction.h',
      'common/content_settings_pattern_serializer.cc',
      'common/content_settings_pattern_serializer.h',
      'common/crash_keys.cc',
      'common/crash_keys.h',
      'common/custom_handlers/protocol_handler.cc',
      'common/custom_handlers/protocol_handler.h',
      'common/descriptors_android.h',
      'common/favicon/fallback_icon_url_parser.cc',
      'common/favicon/fallback_icon_url_parser.h',
      'common/favicon/favicon_url_parser.cc',
      'common/favicon/favicon_url_parser.h',
      'common/icon_with_badge_image_source.cc',
      'common/icon_with_badge_image_source.h',
      'common/ini_parser.cc',
      'common/ini_parser.h',
      'common/instant_types.cc',
      'common/instant_types.h',
      'common/localized_error.cc',
      'common/localized_error.h',
      'common/logging_chrome.cc',
      'common/logging_chrome.h',
      'common/mac/app_mode_common.h',
      'common/mac/app_mode_common.mm',
      'common/mac/app_shim_launch.h',
      'common/mac/app_shim_messages.h',
      'common/mac/cfbundle_blocker.h',
      'common/mac/cfbundle_blocker.mm',
      'common/mac/launchd.h',
      'common/mac/launchd.mm',
      'common/mac/objc_zombie.h',
      'common/mac/objc_zombie.mm',
      'common/media/webrtc_logging_message_data.cc',
      'common/media/webrtc_logging_message_data.h',
      'common/media/webrtc_logging_messages.h',
      'common/media_galleries/metadata_types.h',
      'common/multi_process_lock.h',
      'common/multi_process_lock_linux.cc',
      'common/multi_process_lock_mac.cc',
      'common/multi_process_lock_win.cc',
      'common/omnibox_focus_state.h',
      'common/partial_circular_buffer.cc',
      'common/partial_circular_buffer.h',
      'common/pref_names_util.cc',
      'common/pref_names_util.h',
      'common/prerender_types.h',
      'common/profiling.cc',
      'common/profiling.h',
      'common/ref_counted_util.h',
      'common/render_messages.cc',
      'common/render_messages.h',
      'common/safe_browsing/safebrowsing_messages.h',
      'common/search_provider.h',
      'common/search_types.h',
      'common/search_urls.cc',
      'common/search_urls.h',
      'common/spellcheck_common.cc',
      'common/spellcheck_common.h',
      'common/spellcheck_marker.h',
      'common/spellcheck_messages.h',
      'common/spellcheck_result.h',
      'common/switch_utils.cc',
      'common/switch_utils.h',
      'common/tts_messages.h',
      'common/tts_utterance_request.cc',
      'common/tts_utterance_request.h',
      'common/url_constants.cc',
      'common/url_constants.h',
      'common/v8_breakpad_support_win.cc',
      'common/v8_breakpad_support_win.h',
      'common/variations/experiment_labels.cc',
      'common/variations/experiment_labels.h',
      'common/variations/variations_util.cc',
      'common/variations/variations_util.h',
      'common/web_application_info.cc',
      'common/web_application_info.h',
      'common/worker_thread_ticker.cc',
      'common/worker_thread_ticker.h',
    ],
    'chrome_common_extensions_sources': [
      'common/extensions/api/commands/commands_handler.cc',
      'common/extensions/api/commands/commands_handler.h',
      'common/extensions/api/extension_action/action_info.cc',
      'common/extensions/api/extension_action/action_info.h',
      'common/extensions/api/file_browser_handlers/file_browser_handler.cc',
      'common/extensions/api/file_browser_handlers/file_browser_handler.h',
      'common/extensions/api/input_ime/input_components_handler.cc',
      'common/extensions/api/input_ime/input_components_handler.h',
      'common/extensions/api/notifications/notification_style.cc',
      'common/extensions/api/notifications/notification_style.h',
      'common/extensions/api/omnibox/omnibox_handler.cc',
      'common/extensions/api/omnibox/omnibox_handler.h',
      'common/extensions/api/plugins/plugins_handler.cc',
      'common/extensions/api/plugins/plugins_handler.h',
      'common/extensions/api/speech/tts_engine_manifest_handler.cc',
      'common/extensions/api/speech/tts_engine_manifest_handler.h',
      'common/extensions/api/spellcheck/spellcheck_handler.cc',
      'common/extensions/api/spellcheck/spellcheck_handler.h',
      'common/extensions/api/storage/storage_schema_manifest_handler.cc',
      'common/extensions/api/storage/storage_schema_manifest_handler.h',
      'common/extensions/api/supervised_user_private/supervised_user_handler.cc',
      'common/extensions/api/supervised_user_private/supervised_user_handler.h',
      'common/extensions/api/system_indicator/system_indicator_handler.cc',
      'common/extensions/api/system_indicator/system_indicator_handler.h',
      'common/extensions/api/url_handlers/url_handlers_parser.cc',
      'common/extensions/api/url_handlers/url_handlers_parser.h',
      'common/extensions/api/webstore/webstore_api_constants.cc',
      'common/extensions/api/webstore/webstore_api_constants.h',
      'common/extensions/chrome_extension_messages.h',
      'common/extensions/chrome_extensions_client.cc',
      'common/extensions/chrome_extensions_client.h',
      'common/extensions/chrome_manifest_handlers.cc',
      'common/extensions/chrome_manifest_handlers.h',
      'common/extensions/chrome_manifest_url_handlers.cc',
      'common/extensions/chrome_manifest_url_handlers.h',
      'common/extensions/chrome_utility_extensions_messages.h',
      'common/extensions/command.cc',
      'common/extensions/command.h',
      'common/extensions/extension_constants.cc',
      'common/extensions/extension_constants.h',
      'common/extensions/extension_metrics.cc',
      'common/extensions/extension_metrics.h',
      'common/extensions/extension_process_policy.cc',
      'common/extensions/extension_process_policy.h',
      'common/extensions/features/chrome_channel_feature_filter.cc',
      'common/extensions/features/chrome_channel_feature_filter.h',
      'common/extensions/features/feature_channel.cc',
      'common/extensions/features/feature_channel.h',
      'common/extensions/image_writer/image_writer_util_mac.cc',
      'common/extensions/image_writer/image_writer_util_mac.h',
      'common/extensions/manifest_handlers/app_icon_color_info.cc',
      'common/extensions/manifest_handlers/app_icon_color_info.h',
      'common/extensions/manifest_handlers/app_isolation_info.cc',
      'common/extensions/manifest_handlers/app_isolation_info.h',
      'common/extensions/manifest_handlers/app_launch_info.cc',
      'common/extensions/manifest_handlers/app_launch_info.h',
      'common/extensions/manifest_handlers/automation.cc',
      'common/extensions/manifest_handlers/automation.h',
      'common/extensions/manifest_handlers/content_scripts_handler.cc',
      'common/extensions/manifest_handlers/content_scripts_handler.h',
      'common/extensions/manifest_handlers/copresence_manifest.cc',
      'common/extensions/manifest_handlers/copresence_manifest.h',
      'common/extensions/manifest_handlers/extension_action_handler.cc',
      'common/extensions/manifest_handlers/extension_action_handler.h',
      'common/extensions/manifest_handlers/minimum_chrome_version_checker.cc',
      'common/extensions/manifest_handlers/minimum_chrome_version_checker.h',
      'common/extensions/manifest_handlers/settings_overrides_handler.cc',
      'common/extensions/manifest_handlers/settings_overrides_handler.h',
      'common/extensions/manifest_handlers/theme_handler.cc',
      'common/extensions/manifest_handlers/theme_handler.h',
      'common/extensions/manifest_handlers/ui_overrides_handler.cc',
      'common/extensions/manifest_handlers/ui_overrides_handler.h',
      'common/extensions/permissions/chrome_api_permissions.cc',
      'common/extensions/permissions/chrome_api_permissions.h',
      'common/extensions/permissions/chrome_permission_message_provider.cc',
      'common/extensions/permissions/chrome_permission_message_provider.h',
      'common/extensions/permissions/chrome_permission_message_rules.cc',
      'common/extensions/permissions/chrome_permission_message_rules.h',
      'common/extensions/sync_helper.cc',
      'common/extensions/sync_helper.h',
    ],
    'chrome_common_full_safe_browsing_sources': [
      'common/safe_browsing/download_protection_util.cc',
      'common/safe_browsing/download_protection_util.h',
      'common/safe_browsing/zip_analyzer.cc',
      'common/safe_browsing/zip_analyzer.h',
    ],
    'chrome_common_importer_sources': [
      'common/importer/firefox_importer_utils.cc',
      'common/importer/firefox_importer_utils.h',
      'common/importer/firefox_importer_utils_linux.cc',
      'common/importer/firefox_importer_utils_mac.mm',
      'common/importer/firefox_importer_utils_win.cc',
      'common/importer/ie_importer_test_registry_overrider_win.cc',
      'common/importer/ie_importer_test_registry_overrider_win.h',
      'common/importer/ie_importer_utils_win.cc',
      'common/importer/ie_importer_utils_win.h',
      'common/importer/imported_bookmark_entry.cc',
      'common/importer/imported_bookmark_entry.h',
      'common/importer/importer_autofill_form_data_entry.cc',
      'common/importer/importer_autofill_form_data_entry.h',
      'common/importer/importer_bridge.cc',
      'common/importer/importer_bridge.h',
      'common/importer/importer_data_types.cc',
      'common/importer/importer_data_types.h',
      'common/importer/importer_type.h',
      'common/importer/importer_url_row.cc',
      'common/importer/importer_url_row.h',
      'common/importer/profile_import_process_messages.cc',
      'common/importer/profile_import_process_messages.h',
      'common/importer/profile_import_process_param_traits_macros.h',
      'common/importer/safari_importer_utils.h',
      'common/importer/safari_importer_utils.mm',
    ],
    'chrome_common_ipc_fuzzer_sources': [
      'common/external_ipc_dumper.h',
      'common/external_ipc_dumper.cc',
    ],
    'chrome_common_service_process_sources': [
      'common/service_messages.h',
      'common/service_process_util.cc',
      'common/service_process_util.h',
      'common/service_process_util_linux.cc',
      'common/service_process_util_mac.mm',
      'common/service_process_util_posix.cc',
      'common/service_process_util_posix.h',
      'common/service_process_util_win.cc',
    ],
    'chrome_common_win_mac_sources': [
      'common/media_galleries/itunes_library.cc',
      'common/media_galleries/itunes_library.h',
      'common/media_galleries/picasa_types.cc',
      'common/media_galleries/picasa_types.h',
      'common/media_galleries/pmp_constants.h',
    ],
    'chrome_common_networking_private_sources_openssl' : [
      'common/extensions/api/networking_private/networking_private_crypto.cc',
      'common/extensions/api/networking_private/networking_private_crypto.h',
      'common/extensions/api/networking_private/networking_private_crypto_openssl.cc',
    ],
    'chrome_common_networking_private_sources_nss' : [
      'common/extensions/api/networking_private/networking_private_crypto.cc',
      'common/extensions/api/networking_private/networking_private_crypto.h',
      'common/extensions/api/networking_private/networking_private_crypto_nss.cc',
    ],

    'chrome_common_mac_sources': [
      'common/media_galleries/iphoto_library.cc',
      'common/media_galleries/iphoto_library.h',
    ]
  },
  'targets': [
    {
      # GN: //chrome/common:common
      'target_name': 'common',
      'type': 'static_library',
      'variables': {
        'chrome_common_target': 1,
        'enable_wexit_time_destructors': 1,
      },
      'include_dirs': [
          '..',
          '<(SHARED_INTERMEDIATE_DIR)',  # Needed by chrome_content_client.cc.
        ],
      'direct_dependent_settings': {
        'include_dirs': [
          '..',
        ],
      },
      'dependencies': [
        # TODO(gregoryd): chrome_resources and chrome_strings could be
        #  shared with the 64-bit target, but it does not work due to a gyp
        # issue.
        'common_net',
        'common_version',
        'installer_util',
        'safe_browsing_proto',
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/base/base.gyp:base_i18n',
        '<(DEPTH)/base/base.gyp:base_prefs',
        '<(DEPTH)/base/base.gyp:base_static',
        '<(DEPTH)/chrome/chrome_resources.gyp:chrome_resources',
        '<(DEPTH)/chrome/chrome_resources.gyp:chrome_strings',
        '<(DEPTH)/chrome/chrome_resources.gyp:theme_resources',
        '<(DEPTH)/chrome/common_constants.gyp:common_constants',
        '<(DEPTH)/components/components.gyp:cloud_devices_common',
        '<(DEPTH)/components/components.gyp:component_updater',
        '<(DEPTH)/components/components.gyp:content_settings_core_common',
        '<(DEPTH)/components/components.gyp:favicon_base',
        '<(DEPTH)/components/components.gyp:json_schema',
        '<(DEPTH)/components/components.gyp:metrics',
        '<(DEPTH)/components/components.gyp:policy_component_common',
        '<(DEPTH)/components/components.gyp:translate_core_common',
        '<(DEPTH)/components/components.gyp:variations',
        '<(DEPTH)/content/content.gyp:content_common',
        '<(DEPTH)/crypto/crypto.gyp:crypto',
        '<(DEPTH)/net/net.gyp:net',
        '<(DEPTH)/skia/skia.gyp:skia',
        '<(DEPTH)/skia/skia.gyp:skia_library',
        '<(DEPTH)/third_party/icu/icu.gyp:icui18n',
        '<(DEPTH)/third_party/icu/icu.gyp:icuuc',
        '<(DEPTH)/third_party/libxml/libxml.gyp:libxml',
        '<(DEPTH)/third_party/sqlite/sqlite.gyp:sqlite',
        '<(DEPTH)/third_party/zlib/google/zip.gyp:zip',
        '<(DEPTH)/ui/gfx/ipc/gfx_ipc.gyp:gfx_ipc',
        '<(DEPTH)/ui/resources/ui_resources.gyp:ui_resources',
        '<(DEPTH)/url/url.gyp:url_lib',
      ],
      'sources': [
        '<@(chrome_common_sources)'
      ],
      'conditions': [
        ['enable_extensions==1', {
          'sources': [ '<@(chrome_common_extensions_sources)' ],
          'dependencies': [
            '<(DEPTH)/device/usb/usb.gyp:device_usb',
            '<(DEPTH)/chrome/common/extensions/api/api.gyp:chrome_api',
            '<(DEPTH)/extensions/common/api/api.gyp:extensions_api',
            '<(DEPTH)/extensions/extensions.gyp:extensions_common',
            '<(DEPTH)/extensions/extensions_resources.gyp:extensions_resources',
            '<(DEPTH)/extensions/extensions_strings.gyp:extensions_strings',
            '<(DEPTH)/media/cast/cast.gyp:cast_net',
          ],
          'export_dependent_settings': [
            '<(DEPTH)/chrome/common/extensions/api/api.gyp:chrome_api',
          ],
        }],
        ['OS=="win" or OS=="mac"', {
          'sources': [ '<@(chrome_common_win_mac_sources)' ],
        }],
        ['(OS=="win" or OS=="mac" or chromeos==1) and use_openssl==1', {
          'sources': [ '<@(chrome_common_networking_private_sources_openssl)' ],
          'dependencies': [
            '../third_party/boringssl/boringssl.gyp:boringssl',
          ],
        }],
        ['(OS=="win" or OS=="mac" or chromeos==1) and use_openssl!=1', {
          'sources': [ '<@(chrome_common_networking_private_sources_nss)' ],
        }],
        ['OS=="mac"', {
          'sources': [ '<@(chrome_common_mac_sources)' ],
        }],
        ['OS != "ios"', {
          'dependencies': [
            '<(DEPTH)/components/components.gyp:autofill_core_common',
            '<(DEPTH)/components/components.gyp:autofill_content_common',
            '<(DEPTH)/components/components.gyp:password_manager_core_common',
            '<(DEPTH)/components/components.gyp:password_manager_content_common',
            '<(DEPTH)/components/components.gyp:signin_core_common',
            '<(DEPTH)/components/components.gyp:translate_content_common',
            '<(DEPTH)/components/components.gyp:visitedlink_common',
            '<(DEPTH)/extensions/extensions.gyp:extensions_common_constants',
            '<(DEPTH)/ipc/ipc.gyp:ipc',
            '<(DEPTH)/third_party/re2/re2.gyp:re2',
            '<(DEPTH)/third_party/widevine/cdm/widevine_cdm.gyp:widevine_cdm_version_h',
          ],
        }, {  # OS == ios
          'sources/': [
            ['exclude', '^common/child_process_'],
            ['exclude', '^common/chrome_content_client\\.cc$'],
            ['exclude', '^common/chrome_version_info_posix\\.cc$'],
            ['exclude', '^common/common_message_generator\\.cc$'],
            ['exclude', '^common/common_param_traits'],
            ['exclude', '^common/custom_handlers/'],
            ['exclude', '^common/extensions/'],
            ['exclude', '^common/logging_chrome\\.'],
            ['exclude', '^common/media_galleries/'],
            ['exclude', '^common/multi_process_'],
            ['exclude', '^common/profiling\\.'],
            ['exclude', '^common/spellcheck_'],
            ['exclude', '^common/validation_message_'],
            ['exclude', '^common/web_apps\\.'],
            # TODO(ios): Include files here as they are made to work; once
            # everything is online, remove everything below here and just
            # use the exclusions above.
            ['exclude', '\\.(cc|mm)$'],
            ['include', '_ios\\.(cc|mm)$'],
            ['include', '(^|/)ios/'],
            ['include', '^common/chrome_version_info\\.cc$'],
            ['include', '^common/translate'],
            ['include', '^common/zip'],
          ],
          'include_dirs': [
            '<(DEPTH)/breakpad/src',
          ],
        }],
        ['disable_nacl==0', {
          'dependencies': [
            '<(DEPTH)/components/nacl.gyp:nacl_common',
          ],
        }],
        ['enable_ipc_fuzzer==1', {
          'sources': [ '<@(chrome_common_ipc_fuzzer_sources)' ],
        }],
        ['enable_plugins==1', {
          'dependencies': [
            '<(DEPTH)/third_party/adobe/flash/flash_player.gyp:flapper_version_h',
          ],
          'sources': [
            'common/pepper_flash.cc',
            'common/pepper_flash.h',
            'common/ppapi_utils.cc',
            'common/ppapi_utils.h',
          ],
        }],
        ['enable_plugins==1 and enable_extensions==1', {
          'sources': [
            'common/pepper_permission_util.cc',
            'common/pepper_permission_util.h',
          ],
        }],
        ['enable_basic_printing==1 or enable_print_preview==1', {
          'dependencies': [
            '<(DEPTH)/components/components.gyp:printing_common',
            '<(DEPTH)/printing/printing.gyp:printing',
          ],
        }],
        ['enable_print_preview==1', {
          'sources': [ '<@(chrome_common_service_process_sources)' ],
        }],
        ['enable_service_discovery==1', {
          'sources' : [
            'common/local_discovery/service_discovery_client.cc',
            'common/local_discovery/service_discovery_client.h',
           ]
        }],
        ['enable_mdns==1', {
          'sources' : [
            'common/local_discovery/service_discovery_client_impl.cc',
            'common/local_discovery/service_discovery_client_impl.h',
          ]
        }],
        ['OS=="android"', {
          'sources!': [
            'common/badge_util.cc',
            'common/chrome_version_info_posix.cc',
            'common/extensions/api/spellcheck/spellcheck_handler.cc',
            'common/extensions/manifest_handlers/extension_action_handler.cc',
            'common/extensions/manifest_handlers/minimum_chrome_version_checker.cc',
            'common/icon_with_badge_image_source.cc',
            'common/media_galleries/metadata_types.h',
            'common/net/url_util.cc',
            'common/spellcheck_common.cc',
          ],
        }, {
          # Non-Android.
          'sources': [ '<@(chrome_common_importer_sources)' ]
        }],
        ['OS=="win"', {
          'include_dirs': [
            '<(DEPTH)/breakpad/src',
            '<(DEPTH)/third_party/wtl/include',
          ],
          'dependencies': [
            '<(DEPTH)/components/components.gyp:dom_distiller_core',  # Needed by chrome_content_client.cc.
          ],
        }],
        ['enable_mdns == 1', {
            'sources': [
              'common/local_discovery/local_discovery_messages.h',
            ]
        }],
        ['OS=="mac"', {
          'dependencies': [
            '../third_party/google_toolbox_for_mac/google_toolbox_for_mac.gyp:google_toolbox_for_mac',
            '../third_party/mach_override/mach_override.gyp:mach_override',
          ],
          'include_dirs': [
            '<(DEPTH)/breakpad/src',
          ],
          'sources!': [
            'common/chrome_version_info_posix.cc',
          ],
        }],
        ['enable_webrtc==0', {
          'sources!': [
            'common/media/webrtc_logging_messages.h',
          ]
        }],
        ['configuration_policy==1', {
          'dependencies': [
            '<(DEPTH)/components/components.gyp:policy',
          ],
        }],
        ['safe_browsing==1', {
          'defines': [ 'FULL_SAFE_BROWSING' ],
          'sources': [ '<@(chrome_common_full_safe_browsing_sources)', ],
        }],
        ['safe_browsing==2', {
          'defines': [ 'MOBILE_SAFE_BROWSING' ],
        }],
      ],
      'target_conditions': [
        ['OS == "ios"', {
          'sources/': [
            # Pull in specific Mac files for iOS (which have been filtered out
            # by file name rules).
            ['include', '^common/chrome_version_info_mac\\.mm$'],
          ],
        }],
      ],
      'export_dependent_settings': [
        '../base/base.gyp:base',
      ],
    },
    {
      # GN version: //chrome/common:version
      'target_name': 'common_version',
      'type': 'none',
      'direct_dependent_settings': {
        'include_dirs': [
          '<(SHARED_INTERMEDIATE_DIR)',
        ],
      },
      # Because generate_version generates a header, we must set the
      # hard_dependency flag.
      'hard_dependency': 1,
      'actions': [
        {
          'action_name': 'generate_version',
          'variables': {
            'lastchange_path': '<(DEPTH)/build/util/LASTCHANGE',
            'version_py_path': '<(DEPTH)/build/util/version.py',
            'version_path': 'VERSION',
            'template_input_path': 'common/chrome_version_info_values.h.version',
          },
          'conditions': [
            [ 'branding == "Chrome"', {
              'variables': {
                  'branding_path':
                    'app/theme/google_chrome/BRANDING',
              },
            }, { # else branding!="Chrome"
              'variables': {
                  'branding_path':
                    'app/theme/chromium/BRANDING',
              },
            }],
          ],
          'inputs': [
            '<(template_input_path)',
            '<(version_path)',
            '<(branding_path)',
            '<(lastchange_path)',
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/chrome/common/chrome_version_info_values.h',
          ],
          'action': [
            'python',
            '<(version_py_path)',
            '-f', '<(version_path)',
            '-f', '<(branding_path)',
            '-f', '<(lastchange_path)',
            '<(template_input_path)',
            '<@(_outputs)',
          ],
          'message': 'Generating version information',
        },
      ],
    },
    {
      # GN version: //chrome/common/net
      'target_name': 'common_net',
      'type': 'static_library',
      'sources': [
        'common/net/net_resource_provider.cc',
        'common/net/net_resource_provider.h',
        'common/net/url_util.cc',
        'common/net/url_util.h',
        'common/net/x509_certificate_model.cc',
        'common/net/x509_certificate_model.h',
        'common/net/x509_certificate_model_nss.cc',
        'common/net/x509_certificate_model_openssl.cc',
      ],
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/chrome/chrome_resources.gyp:chrome_resources',
        '<(DEPTH)/chrome/chrome_resources.gyp:chrome_strings',
        '<(DEPTH)/components/components.gyp:network_hints_common',
        '<(DEPTH)/components/components.gyp:error_page_common',
        '<(DEPTH)/crypto/crypto.gyp:crypto',
        '<(DEPTH)/net/net.gyp:net_resources',
        '<(DEPTH)/net/net.gyp:net',
        '<(DEPTH)/third_party/icu/icu.gyp:icui18n',
        '<(DEPTH)/third_party/icu/icu.gyp:icuuc',
        '<(DEPTH)/ui/base/ui_base.gyp:ui_base',
      ],
      'conditions': [
        ['OS != "ios"', {
          'dependencies': [
            '<(DEPTH)/gpu/gpu.gyp:gpu_ipc',
          ],
        }, {  # OS == ios
          'sources!': [
            'common/net/net_resource_provider.cc',
            'common/net/x509_certificate_model.cc',
          ],
        }],
        ['os_posix == 1 and OS != "mac" and OS != "ios" and OS != "android"', {
            'dependencies': [
              '../build/linux/system.gyp:ssl',
            ],
          },
        ],
        ['os_posix != 1 or OS == "mac" or OS == "ios"', {
            'sources!': [
              'common/net/x509_certificate_model_nss.cc',
              'common/net/x509_certificate_model_openssl.cc',
            ],
          },
        ],
        ['OS == "android"', {
            'dependencies': [
              '../third_party/boringssl/boringssl.gyp:boringssl',
            ],
            'sources!': [
              'common/net/x509_certificate_model.cc',
              'common/net/x509_certificate_model_openssl.cc',
            ],
        }],
        ['use_openssl==1', {
            'sources!': [
              'common/net/x509_certificate_model_nss.cc',
            ],
            'dependencies': [
              '<(DEPTH)/third_party/boringssl/boringssl.gyp:boringssl',
            ],
          },
          {  # else !use_openssl: remove the unneeded files
            'sources!': [
              'common/net/x509_certificate_model_openssl.cc',
            ],
          },
        ],
        ['OS=="win"', {
            # TODO(jschuh): crbug.com/167187 fix size_t to int truncations.
            'msvs_disabled_warnings': [4267, ],
          },
        ],
      ],
    },
    {
      # Protobuf compiler / generator for the safebrowsing client
      # model proto and the client-side detection (csd) request
      # protocol buffer.

      # GN version: //chrome/common/safe_browsing:proto
      'target_name': 'safe_browsing_proto',
      'type': 'static_library',
      'sources': [
        'common/safe_browsing/client_model.proto',
        'common/safe_browsing/crx_info.proto',
        'common/safe_browsing/csd.proto'
      ],
      'variables': {
        'proto_in_dir': 'common/safe_browsing',
        'proto_out_dir': 'chrome/common/safe_browsing',
      },
      'includes': [ '../build/protoc.gypi' ],
    },
  ],
}
