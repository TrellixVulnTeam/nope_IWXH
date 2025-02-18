// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the SdchManager class and two nested classes
// (Dictionary, DictionarySet). SdchManager::Dictionary contains all
// of the information about an SDCH dictionary. The manager is
// responsible for storing those dictionaries, and provides access to
// them through DictionarySet objects. A DictionarySet is an object
// whose lifetime is under the control of the consumer. It is a
// reference to a set of dictionaries, and guarantees that none of
// those dictionaries will be destroyed while the DictionarySet
// reference is alive.

#ifndef NET_BASE_SDCH_MANAGER_H_
#define NET_BASE_SDCH_MANAGER_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"
#include "net/base/net_export.h"
#include "net/base/sdch_problem_codes.h"
#include "url/gurl.h"

namespace base {
class Clock;
class Value;
}

namespace net {

class SdchObserver;

// Provides global database of differential decompression dictionaries for the
// SDCH filter (processes sdch enconded content).
//
// The SdchManager maintains a collection of memory resident dictionaries. It
// can find a dictionary (based on a server specification of a hash), store a
// dictionary, and make judgements about what URLs can use, set, etc. a
// dictionary.

// These dictionaries are acquired over the net, and include a header
// (containing metadata) as well as a VCDIFF dictionary (for use by a VCDIFF
// module) to decompress data.
//
// A dictionary held by the manager may nonetheless outlive the manager if
// a DictionarySet object refers to it; see below.
class NET_EXPORT SdchManager {
 public:
  class Dictionary;
  typedef std::map<std::string, scoped_refptr<base::RefCountedData<Dictionary>>>
      DictionaryMap;

  class NET_EXPORT_PRIVATE Dictionary {
   public:
    // Construct a vc-diff usable dictionary from the dictionary_text starting
    // at the given offset. The supplied client_hash should be used to
    // advertise the dictionary's availability relative to the suppplied URL.
    Dictionary(const std::string& dictionary_text,
               size_t offset,
               const std::string& client_hash,
               const std::string& server_hash,
               const GURL& url,
               const std::string& domain,
               const std::string& path,
               const base::Time& expiration,
               const std::set<int>& ports);

    ~Dictionary();

    // Sdch filters can get our text to use in decoding compressed data.
    const std::string& text() const { return text_; }

    const GURL& url() const { return url_; }
    const std::string& client_hash() const { return client_hash_; }
    const std::string& server_hash() const { return server_hash_; }
    const std::string& domain() const { return domain_; }
    const std::string& path() const { return path_; }
    const base::Time& expiration() const { return expiration_; }
    const std::set<int>& ports() const { return ports_; }

    // Security methods to check if we can establish a new dictionary with the
    // given data, that arrived in response to get of dictionary_url.
    static SdchProblemCode CanSet(const std::string& domain,
                                  const std::string& path,
                                  const std::set<int>& ports,
                                  const GURL& dictionary_url);

    // Security method to check if we can use a dictionary to decompress a
    // target that arrived with a reference to this dictionary.
    SdchProblemCode CanUse(const GURL& referring_url) const;

    // Compare paths to see if they "match" for dictionary use.
    static bool PathMatch(const std::string& path,
                          const std::string& restriction);

    // Compare domains to see if the "match" for dictionary use.
    static bool DomainMatch(const GURL& url, const std::string& restriction);

    // Is this dictionary expired?
    bool Expired() const;

    void SetClockForTesting(scoped_ptr<base::Clock> clock);

   private:
    friend class base::RefCountedData<Dictionary>;

    // Private copy-constructor to support RefCountedData<>, which requires
    // that an object stored in it be either DefaultConstructible or
    // CopyConstructible
    Dictionary(const Dictionary& rhs);

    // The actual text of the dictionary.
    std::string text_;

    // Part of the hash of text_ that the client uses to advertise the fact that
    // it has a specific dictionary pre-cached.
    std::string client_hash_;

    // Part of the hash of text_ that the server uses to identify the
    // dictionary it wants used for decoding.
    std::string server_hash_;

    // The GURL that arrived with the text_ in a URL request to specify where
    // this dictionary may be used.
    const GURL url_;

    // Metadate "headers" in before dictionary text contained the following:
    // Each dictionary payload consists of several headers, followed by the text
    // of the dictionary. The following are the known headers.
    const std::string domain_;
    const std::string path_;
    const base::Time expiration_;  // Implied by max-age.
    const std::set<int> ports_;

    scoped_ptr<base::Clock> clock_;

    void operator=(const Dictionary&) = delete;
  };

  // A handle for one or more dictionaries which will keep the dictionaries
  // alive and accessible for the handle's lifetime.
  class NET_EXPORT_PRIVATE DictionarySet {
   public:
    ~DictionarySet();

    // Return a comma separated list of client hashes.
    std::string GetDictionaryClientHashList() const;

    // Lookup a given dictionary based on server hash. Returned pointer
    // is guaranteed to be valid for the lifetime of the DictionarySet.
    // Returns NULL if hash is not a valid server hash for a dictionary
    // named by DictionarySet.
    const SdchManager::Dictionary* GetDictionary(const std::string& hash) const;

    bool Empty() const;

   private:
    // A DictionarySet may only be constructed by the SdchManager.
    friend class SdchManager;

    DictionarySet();
    void AddDictionary(const std::string& server_hash,
                       const scoped_refptr<base::RefCountedData<
                           SdchManager::Dictionary>>& dictionary);

    DictionaryMap dictionaries_;

    DISALLOW_COPY_AND_ASSIGN(DictionarySet);
  };

  SdchManager();
  ~SdchManager();

  // Clear data (for browser data removal).
  void ClearData();

  // Record stats on various errors.
  static void SdchErrorRecovery(SdchProblemCode problem);

  // Enables or disables SDCH compression.
  static void EnableSdchSupport(bool enabled);

  static bool sdch_enabled() { return g_sdch_enabled_; }

  // Enables or disables SDCH compression over secure connection.
  static void EnableSecureSchemeSupport(bool enabled);

  static bool secure_scheme_supported() { return g_secure_scheme_supported_; }

  // Briefly prevent further advertising of SDCH on this domain (if SDCH is
  // enabled). After enough calls to IsInSupportedDomain() the blacklisting
  // will be removed. Additional blacklists take exponentially more calls
  // to IsInSupportedDomain() before the blacklisting is undone.
  // Used when filter errors are found from a given domain, but it is plausible
  // that the cause is temporary (such as application startup, where cached
  // entries are used, but a dictionary is not yet loaded).
  void BlacklistDomain(const GURL& url, SdchProblemCode blacklist_reason);

  // Used when SEVERE filter errors are found from a given domain, to prevent
  // further use of SDCH on that domain.
  void BlacklistDomainForever(const GURL& url,
                              SdchProblemCode blacklist_reason);

  // Unit test only, this function resets enabling of sdch, and clears the
  // blacklist.
  void ClearBlacklistings();

  // Unit test only, this function resets the blacklisting count for a domain.
  void ClearDomainBlacklisting(const std::string& domain);

  // Unit test only: indicate how many more times a domain will be blacklisted.
  int BlackListDomainCount(const std::string& domain);

  // Unit test only: Indicate what current blacklist increment is for a domain.
  int BlacklistDomainExponential(const std::string& domain);

  // Check to see if SDCH is enabled (globally), and the given URL is in a
  // supported domain (i.e., not blacklisted, and either the specific supported
  // domain, or all domains were assumed supported). If it is blacklist, reduce
  // by 1 the number of times it will be reported as blacklisted.
  SdchProblemCode IsInSupportedDomain(const GURL& url);

  // Send out appropriate events notifying observers that a Get-Dictionary
  // header has been seen.
  SdchProblemCode OnGetDictionary(const GURL& request_url,
                                  const GURL& dictionary_url);

  // Send out appropriate events notifying observers that a dictionary
  // was successfully used to decode a request.
  void OnDictionaryUsed(const std::string& server_hash);

  // Get a handle to the available dictionaries that might be used
  // for encoding responses for the given URL. The return set will not
  // include expired dictionaries. If no dictionaries
  // are appropriate to use with the target_url, NULL is returned.
  scoped_ptr<DictionarySet> GetDictionarySet(const GURL& target_url);

  // Get a handle to a specific dictionary, by its server hash, confirming
  // that that specific dictionary is appropriate to use with |target_url|.
  // Expired dictionaries will be returned. If no dictionary with that
  // hash exists that is usable with |target_url|, NULL is returned.
  // If there is a usability problem, |*error_code| is set to the
  // appropriate problem code.
  scoped_ptr<DictionarySet> GetDictionarySetByHash(
      const GURL& target_url,
      const std::string& server_hash,
      SdchProblemCode* problem_code);

  // Construct the pair of hashes for client and server to identify an SDCH
  // dictionary. This is only made public to facilitate unit testing, but is
  // otherwise private
  static void GenerateHash(const std::string& dictionary_text,
                           std::string* client_hash, std::string* server_hash);

  // For Latency testing only, we need to know if we've succeeded in doing a
  // round trip before starting our comparative tests. If ever we encounter
  // problems with SDCH, we opt-out of the test unless/until we perform a
  // complete SDCH decoding.
  bool AllowLatencyExperiment(const GURL& url) const;

  void SetAllowLatencyExperiment(const GURL& url, bool enable);

  base::Value* SdchInfoToValue() const;

  // Add an SDCH dictionary to our list of availible
  // dictionaries. This addition will fail if addition is illegal
  // (data in the dictionary is not acceptable from the
  // dictionary_url; dictionary already added, etc.).
  // If |server_hash| is non-null, returns the server hash that may be
  // used as an argument to GetDictionarySetByHash.
  // Returns SDCH_OK if the addition was successfull, and corresponding error
  // code otherwise.
  SdchProblemCode AddSdchDictionary(const std::string& dictionary_text,
                                    const GURL& dictionary_url,
                                    std::string* server_hash_p);

  // Remove an SDCH dictionary
  SdchProblemCode RemoveSdchDictionary(const std::string& server_hash);

  // Registration for events generated by the SDCH subsystem.
  void AddObserver(SdchObserver* observer);
  void RemoveObserver(SdchObserver* observer);

  static scoped_ptr<DictionarySet> CreateEmptyDictionarySetForTesting();

  // For investigation of http://crbug.com/454198; remove when resolved.
  base::WeakPtr<SdchManager> GetWeakPtr();

 private:
  struct BlacklistInfo {
    BlacklistInfo() : count(0), exponential_count(0), reason(SDCH_OK) {}

    int count;               // # of times to refuse SDCH advertisement.
    int exponential_count;   // Current exponential backoff ratchet.
    SdchProblemCode reason;  // Why domain was blacklisted.
  };

  typedef std::map<std::string, BlacklistInfo> DomainBlacklistInfo;
  typedef std::set<std::string> ExperimentSet;

  // Determines whether a "Get-Dictionary" header is legal (dictionary
  // url has appropriate relationship to referrer url) in the SDCH
  // protocol. Return SDCH_OK if fetch is legal.
  SdchProblemCode CanFetchDictionary(const GURL& referring_url,
                                     const GURL& dictionary_url) const;

  // Support SDCH compression, by advertising in headers.
  static bool g_sdch_enabled_;

  // Support SDCH compression for HTTPS requests and responses. When supported,
  // HTTPS applicable dictionaries MUST have been acquired securely via HTTPS.
  static bool g_secure_scheme_supported_;

  // A simple implementation of a RFC 3548 "URL safe" base64 encoder.
  static void UrlSafeBase64Encode(const std::string& input,
                                  std::string* output);

  DictionaryMap dictionaries_;

  // List domains where decode failures have required disabling sdch.
  DomainBlacklistInfo blacklisted_domains_;

  // List of hostnames for which a latency experiment is allowed (because a
  // round trip test has recently passed).
  ExperimentSet allow_latency_experiment_;

  // Observers that want to be notified of SDCH events.
  // Assert list is empty on destruction since if there is an observer
  // that hasn't removed itself from the list, that observer probably
  // has a reference to the SdchManager.
  ObserverList<SdchObserver, true> observers_;

  base::ThreadChecker thread_checker_;

  base::WeakPtrFactory<SdchManager> factory_;

  DISALLOW_COPY_AND_ASSIGN(SdchManager);
};

}  // namespace net

#endif  // NET_BASE_SDCH_MANAGER_H_
