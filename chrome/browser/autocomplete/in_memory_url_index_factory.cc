// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autocomplete/in_memory_url_index_factory.h"

#include "base/memory/singleton.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/autocomplete/in_memory_url_index.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/keyed_service/core/service_access_type.h"

// static
InMemoryURLIndex* InMemoryURLIndexFactory::GetForProfile(Profile* profile) {
  return static_cast<InMemoryURLIndex*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
InMemoryURLIndexFactory* InMemoryURLIndexFactory::GetInstance() {
  return Singleton<InMemoryURLIndexFactory>::get();
}

InMemoryURLIndexFactory::InMemoryURLIndexFactory()
    : BrowserContextKeyedServiceFactory(
          "InMemoryURLIndex",
          BrowserContextDependencyManager::GetInstance()) {
  DependsOn(HistoryServiceFactory::GetInstance());
}

InMemoryURLIndexFactory::~InMemoryURLIndexFactory() {
}

KeyedService* InMemoryURLIndexFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  // Do not force creation of the HistoryService if saving history is disabled.
  Profile* profile = Profile::FromBrowserContext(context);
  InMemoryURLIndex* in_memory_url_index = new InMemoryURLIndex(
      HistoryServiceFactory::GetForProfile(profile,
                                           ServiceAccessType::IMPLICIT_ACCESS),
      profile->GetPath(),
      profile->GetPrefs()->GetString(prefs::kAcceptLanguages));
  in_memory_url_index->Init();
  return in_memory_url_index;
}

content::BrowserContext* InMemoryURLIndexFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}

bool InMemoryURLIndexFactory::ServiceIsNULLWhileTesting() const {
  return true;
}
