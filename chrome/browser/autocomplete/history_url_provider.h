// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOCOMPLETE_HISTORY_URL_PROVIDER_H_
#define CHROME_BROWSER_AUTOCOMPLETE_HISTORY_URL_PROVIDER_H_

#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/synchronization/cancellation_flag.h"
#include "chrome/browser/autocomplete/history_provider.h"
#include "components/history/core/browser/history_match.h"
#include "components/omnibox/autocomplete_input.h"
#include "components/omnibox/omnibox_field_trial.h"
#include "components/search_engines/template_url.h"

class AutocompleteProviderListener;
class Profile;
class SearchTermsData;

namespace base {
class MessageLoop;
}

namespace history {
class HistoryBackend;
class URLDatabase;
}

// How history autocomplete works
// ==============================
//
// Read down this diagram for temporal ordering.
//
//   Main thread                History thread
//   -----------                --------------
//   AutocompleteController::Start
//     -> HistoryURLProvider::Start
//       -> SuggestExactInput
//       [params_ allocated]
//       -> DoAutocomplete (for inline autocomplete)
//         -> URLDatabase::AutocompleteForPrefix (on in-memory DB)
//       -> HistoryService::ScheduleAutocomplete
//       (return to controller) ----
//                                 /
//                            HistoryBackend::ScheduleAutocomplete
//                              -> HistoryURLProvider::ExecuteWithDB
//                                -> DoAutocomplete
//                                  -> URLDatabase::AutocompleteForPrefix
//                              /
//   HistoryService::QueryComplete
//     [params_ destroyed]
//     -> AutocompleteProviderListener::OnProviderUpdate
//
// The autocomplete controller calls us, and must be called back, on the main
// thread.  When called, we run two autocomplete passes.  The first pass runs
// synchronously on the main thread and queries the in-memory URL database.
// This pass promotes matches for inline autocomplete if applicable.  We do
// this synchronously so that users get consistent behavior when they type
// quickly and hit enter, no matter how loaded the main history database is.
// Doing this synchronously also prevents inline autocomplete from being
// "flickery" in the AutocompleteEdit.  Because the in-memory DB does not have
// redirect data, results other than the top match might change between the
// two passes, so we can't just decide to use this pass' matches as the final
// results.
//
// The second autocomplete pass uses the full history database, which must be
// queried on the history thread.  Start() asks the history service schedule to
// callback on the history thread with a pointer to the main database.  When we
// are done doing queries, we schedule a task on the main thread that notifies
// the AutocompleteController that we're done.
//
// The communication between these threads is done using a
// HistoryURLProviderParams object.  This is allocated in the main thread, and
// normally deleted in QueryComplete().  So that both autocomplete passes can
// use the same code, we also use this to hold results during the first
// autocomplete pass.
//
// While the second pass is running, the AutocompleteController may cancel the
// request.  This can happen frequently when the user is typing quickly.  In
// this case, the main thread sets params_->cancel, which the background thread
// checks periodically.  If it finds the flag set, it stops what it's doing
// immediately and calls back to the main thread.  (We don't delete the params
// on the history thread, because we should only do that when we can safely
// NULL out params_, and that must be done on the main thread.)

// Used to communicate autocomplete parameters between threads via the history
// service.
struct HistoryURLProviderParams {
  // See comments on |promote_type| below.
  enum PromoteType {
    WHAT_YOU_TYPED_MATCH,
    FRONT_HISTORY_MATCH,
    NEITHER,
  };

  HistoryURLProviderParams(const AutocompleteInput& input,
                           bool trim_http,
                           const AutocompleteMatch& what_you_typed_match,
                           const std::string& languages,
                           TemplateURL* default_search_provider,
                           const SearchTermsData& search_terms_data);
  ~HistoryURLProviderParams();

  base::MessageLoop* message_loop;

  // A copy of the autocomplete input. We need the copy since this object will
  // live beyond the original query while it runs on the history thread.
  AutocompleteInput input;

  // Should inline autocompletion be disabled? This is initalized from
  // |input.prevent_inline_autocomplete()|, but set to false is the input
  // contains trailing white space.
  bool prevent_inline_autocomplete;

  // Set when "http://" should be trimmed from the beginning of the URLs.
  bool trim_http;

  // A match corresponding to what the user typed.
  AutocompleteMatch what_you_typed_match;

  // Set by the main thread to cancel this request.  If this flag is set when
  // the query runs, the query will be abandoned.  This allows us to avoid
  // running queries that are no longer needed.  Since we don't care if we run
  // the extra queries, the lack of signaling is not a problem.
  base::CancellationFlag cancel_flag;

  // Set by ExecuteWithDB() on the history thread when the query could not be
  // performed because the history system failed to properly init the database.
  // If this is set when the main thread is called back, it avoids changing
  // |matches_| at all, so it won't delete the default match Start() creates.
  bool failed;

  // List of matches written by DoAutocomplete().  Upon its return the provider
  // converts this list to ACMatches and places them in |matches_|.
  history::HistoryMatches matches;

  // True if the suggestion for exactly what the user typed appears as a known
  // URL in the user's history.  In this case, this will also be the first match
  // in |matches|.
  //
  // NOTE: There are some complications related to keeping things consistent
  // between passes and how we deal with intranet URLs, which are too complex to
  // explain here; see the implementations of DoAutocomplete() and
  // FixupExactSuggestion() for specific comments.
  bool exact_suggestion_is_in_history;

  // Tells the provider whether to promote the what you typed match, the first
  // element of |matches|, or neither as the first AutocompleteMatch.  If
  // |exact_suggestion_is_in_history| is true (and thus "the what you typed
  // match" and "the first element of |matches|" represent the same thing), this
  // will be set to WHAT_YOU_TYPED_MATCH.
  //
  // NOTE: The second pass of DoAutocomplete() checks what the first pass set
  // this to.  See comments in DoAutocomplete().
  PromoteType promote_type;

  // Languages we should pass to gfx::GetCleanStringFromUrl.
  std::string languages;

  // The default search provider and search terms data necessary to cull results
  // that correspond to searches (on the default engine).  These can only be
  // obtained on the UI thread, so we have to copy them into here to pass them
  // to the history thread.  We use a scoped_ptr<TemplateURL> for the DSP since
  // TemplateURLs can't be copied by value. We use a scoped_ptr<SearchTermsData>
  // so that we can store a snapshot of the SearchTermsData accessible from the
  // history thread.
  scoped_ptr<TemplateURL> default_search_provider;
  scoped_ptr<SearchTermsData> search_terms_data;

 private:
  DISALLOW_COPY_AND_ASSIGN(HistoryURLProviderParams);
};

// This class is an autocomplete provider and is also a pseudo-internal
// component of the history system.  See comments above.
class HistoryURLProvider : public HistoryProvider {
 public:
  // Various values used in scoring, made public so other providers
  // can insert results in appropriate ranges relative to these.
  static const int kScoreForBestInlineableResult;
  static const int kScoreForUnvisitedIntranetResult;
  static const int kScoreForWhatYouTypedResult;
  static const int kBaseScoreForNonInlineableResult;

  HistoryURLProvider(AutocompleteProviderListener* listener, Profile* profile);

  // HistoryProvider:
  virtual void Start(const AutocompleteInput& input,
                     bool minimal_changes,
                     bool called_due_to_focus) override;
  virtual void Stop(bool clear_cached_results,
                    bool due_to_user_inactivity) override;

  // Returns a match representing a navigation to |destination_url| given user
  // input of |text|.  |trim_http| controls whether the match's |fill_into_edit|
  // and |contents| should have any HTTP scheme stripped off, and should not be
  // set to true if |text| contains an http prefix.
  // NOTES: This does not set the relevance of the returned match, as different
  //        callers want different behavior. Callers must set this manually.
  //        This function should only be called on the UI thread.
  AutocompleteMatch SuggestExactInput(const base::string16& text,
                                      const GURL& destination_url,
                                      bool trim_http);

  // Runs the history query on the history thread, called by the history
  // system. The history database MAY BE NULL in which case it is not
  // available and we should return no data. Also schedules returning the
  // results to the main thread
  void ExecuteWithDB(HistoryURLProviderParams* params,
                     history::HistoryBackend* backend,
                     history::URLDatabase* db);

 private:
  FRIEND_TEST_ALL_PREFIXES(HistoryURLProviderTest, HUPScoringExperiment);

  enum MatchType {
    NORMAL,
    WHAT_YOU_TYPED,
    INLINE_AUTOCOMPLETE,
    UNVISITED_INTRANET,  // An intranet site that has never been visited.
  };
  class VisitClassifier;

  ~HistoryURLProvider();

  // Determines the relevance for a match, given its type.  If |match_type| is
  // NORMAL, |match_number| is a number indicating the relevance of the match
  // (higher == more relevant).  For other values of |match_type|,
  // |match_number| is ignored.  Only called some of the time; for some matches,
  // relevancy scores are assigned consecutively decreasing (1416, 1415, ...).
  static int CalculateRelevance(MatchType match_type, int match_number);

  // Returns a set of classifications that highlight all the occurrences of
  // |input_text| at word breaks in |description|.
  static ACMatchClassifications ClassifyDescription(
      const base::string16& input_text,
      const base::string16& description);

  // Actually runs the autocomplete job on the given database, which is
  // guaranteed not to be NULL.  Used by both autocomplete passes, and therefore
  // called on multiple different threads (though not simultaneously).
  void DoAutocomplete(history::HistoryBackend* backend,
                      history::URLDatabase* db,
                      HistoryURLProviderParams* params);

  // May promote either the what you typed match or first history match in
  // params->matches to the front of |matches_|, depending on the value of
  // params->promote_type.
  void PromoteMatchIfNecessary(const HistoryURLProviderParams& params);

  // Dispatches the results to the autocomplete controller. Called on the
  // main thread by ExecuteWithDB when the results are available.
  // Frees params_gets_deleted on exit.
  void QueryComplete(HistoryURLProviderParams* params_gets_deleted);

  // Looks up the info for params->what_you_typed_match in the DB.  If found,
  // fills in the title, promotes the match's priority to that of an inline
  // autocomplete match (maybe it should be slightly better?), and places it on
  // the front of params->matches (so we pick the right matches to throw away
  // when culling redirects to/from it).  Returns whether a match was promoted.
  bool FixupExactSuggestion(history::URLDatabase* db,
                            const VisitClassifier& classifier,
                            HistoryURLProviderParams* params) const;

  // Helper function for FixupExactSuggestion, this returns true if the input
  // corresponds to some intranet URL where the user has previously visited the
  // host in question.  In this case the input should be treated as a URL.
  bool CanFindIntranetURL(history::URLDatabase* db,
                          const AutocompleteInput& input) const;

  // Sees if a shorter version of the best match should be created, and if so
  // places it at the front of params->matches.  This can suggest history URLs
  // that are prefixes of the best match (if they've been visited enough,
  // compared to the best match), or create host-only suggestions even when they
  // haven't been visited before: if the user visited http://example.com/asdf
  // once, we'll suggest http://example.com/ even if they've never been to it.
  // Returns true if a match was successfully created/promoted that we're
  // willing to inline autocomplete.
  bool PromoteOrCreateShorterSuggestion(
      history::URLDatabase* db,
      bool have_what_you_typed_match,
      HistoryURLProviderParams* params);

  // Removes results that have been rarely typed or visited, and not any time
  // recently.  The exact parameters for this heuristic can be found in the
  // function body. Also culls results corresponding to queries from the default
  // search engine. These are low-quality, difficult-to-understand matches for
  // users, and the SearchProvider should surface past queries in a better way
  // anyway.
  void CullPoorMatches(HistoryURLProviderParams* params) const;

  // Removes results that redirect to each other, leaving at most |max_results|
  // results.
  void CullRedirects(history::HistoryBackend* backend,
                     history::HistoryMatches* matches,
                     size_t max_results) const;

  // Helper function for CullRedirects, this removes all but the first
  // occurance of [any of the set of strings in |remove|] from the |matches|
  // list.
  //
  // The return value is the index of the item that is after the item in the
  // input identified by |source_index|. If |source_index| or an item before
  // is removed, the next item will be shifted, and this allows the caller to
  // pick up on the next one when this happens.
  size_t RemoveSubsequentMatchesOf(history::HistoryMatches* matches,
                                   size_t source_index,
                                   const std::vector<GURL>& remove) const;

  // Converts a specified |match_number| from params.matches into an
  // autocomplete match for display.  If experimental scoring is enabled, the
  // final relevance score might be different from the given |relevance|.
  // NOTE: This function should only be called on the UI thread.
  AutocompleteMatch HistoryMatchToACMatch(
      const HistoryURLProviderParams& params,
      size_t match_number,
      MatchType match_type,
      int relevance);

  AutocompleteProviderListener* listener_;

  // Params for the current query.  The provider should not free this directly;
  // instead, it is passed as a parameter through the history backend, and the
  // parameter itself is freed once it's no longer needed.  The only reason we
  // keep this member is so we can set the cancel bit on it.
  HistoryURLProviderParams* params_;

  // Params controlling experimental behavior of this provider.
  HUPScoringParams scoring_params_;

  DISALLOW_COPY_AND_ASSIGN(HistoryURLProvider);
};

#endif  // CHROME_BROWSER_AUTOCOMPLETE_HISTORY_URL_PROVIDER_H_
