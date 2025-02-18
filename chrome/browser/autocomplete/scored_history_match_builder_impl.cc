// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autocomplete/scored_history_match_builder_impl.h"

#include <math.h>

#include <algorithm>
#include <vector>

#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/autocomplete/history_url_provider.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_utils.h"
#include "components/history/core/browser/history_client.h"
#include "components/omnibox/omnibox_field_trial.h"
#include "components/omnibox/url_prefix.h"
#include "content/public/browser/browser_thread.h"

namespace {

// The number of days of recency scores to precompute.
const int kDaysToPrecomputeRecencyScoresFor = 366;

// The number of raw term score buckets use; raw term scores greater this are
// capped at the score of the largest bucket.
const int kMaxRawTermScore = 30;

// If true, assign raw scores to be max(whatever it normally would be, a score
// that's similar to the score HistoryURL provider would assign). This variable
// is set in the constructor by examining the field trial state.
const bool kAlsoDoHupLikeScoring = false;

// Pre-computed information to speed up calculating recency scores.
// |days_ago_to_recency_score| is a simple array mapping how long ago a page was
// visited (in days) to the recency score we should assign it.  This allows easy
// lookups of scores without requiring math. This is initialized by
// InitDaysAgoToRecencyScoreArray called by
// ScoredHistoryMatchBuilderImpl::Init().
float days_ago_to_recency_score[kDaysToPrecomputeRecencyScoresFor];

// Pre-computed information to speed up calculating topicality scores.
// |raw_term_score_to_topicality_score| is a simple array mapping how raw terms
// scores (a weighted sum of the number of hits for the term, weighted by how
// important the hit is: hostname, path, etc.) to the topicality score we should
// assign it.  This allows easy lookups of scores without requiring math. This
// is initialized by InitRawTermScoreToTopicalityScoreArray() called from
// ScoredHistoryMatchBuilderImpl::Init().
float raw_term_score_to_topicality_score[kMaxRawTermScore];

// The maximum score that can be assigned to non-inlineable matches. This is
// useful because often we want inlineable matches to come first (even if they
// don't sometimes score as well as non-inlineable matches) because if a
// non-inlineable match comes first than all matches will get demoted later in
// HistoryQuickProvider to non-inlineable scores. Set to -1 to indicate no
// maximum score.
int max_assigned_score_for_non_inlineable_matches = -1;

// Whether ScoredHistoryMatchBuilderImpl::Init() has been called.
bool initialized = false;

// Precalculates raw_term_score_to_topicality_score, used in
// GetTopicalityScore().
void InitRawTermScoreToTopicalityScoreArray() {
  for (int term_score = 0; term_score < kMaxRawTermScore; ++term_score) {
    float topicality_score;
    if (term_score < 10) {
      // If the term scores less than 10 points (no full-credit hit, or
      // no combination of hits that score that well), then the topicality
      // score is linear in the term score.
      topicality_score = 0.1 * term_score;
    } else {
      // For term scores of at least ten points, pass them through a log
      // function so a score of 10 points gets a 1.0 (to meet up exactly
      // with the linear component) and increases logarithmically until
      // maxing out at 30 points, with computes to a score around 2.1.
      topicality_score = (1.0 + 2.25 * log10(0.1 * term_score));
    }
    raw_term_score_to_topicality_score[term_score] = topicality_score;
  }
}

// Pre-calculates days_ago_to_recency_score, used in GetRecencyScore().
void InitDaysAgoToRecencyScoreArray() {
  for (int days_ago = 0; days_ago < kDaysToPrecomputeRecencyScoresFor;
       days_ago++) {
    int unnormalized_recency_score;
    if (days_ago <= 4) {
      unnormalized_recency_score = 100;
    } else if (days_ago <= 14) {
      // Linearly extrapolate between 4 and 14 days so 14 days has a score
      // of 70.
      unnormalized_recency_score = 70 + (14 - days_ago) * (100 - 70) / (14 - 4);
    } else if (days_ago <= 31) {
      // Linearly extrapolate between 14 and 31 days so 31 days has a score
      // of 50.
      unnormalized_recency_score = 50 + (31 - days_ago) * (70 - 50) / (31 - 14);
    } else if (days_ago <= 90) {
      // Linearly extrapolate between 30 and 90 days so 90 days has a score
      // of 30.
      unnormalized_recency_score = 30 + (90 - days_ago) * (50 - 30) / (90 - 30);
    } else {
      // Linearly extrapolate between 90 and 365 days so 365 days has a score
      // of 10.
      unnormalized_recency_score =
          10 + (365 - days_ago) * (20 - 10) / (365 - 90);
    }
    days_ago_to_recency_score[days_ago] = unnormalized_recency_score / 100.0;
    if (days_ago > 0) {
      DCHECK_LE(days_ago_to_recency_score[days_ago],
                days_ago_to_recency_score[days_ago - 1]);
    }
  }
}

}  // namespace

// static
int ScoredHistoryMatchBuilderImpl::bookmark_value_ = 1;
bool ScoredHistoryMatchBuilderImpl::allow_tld_matches_ = false;
bool ScoredHistoryMatchBuilderImpl::allow_scheme_matches_ = false;
bool ScoredHistoryMatchBuilderImpl::hqp_experimental_scoring_enabled_ = false;
float ScoredHistoryMatchBuilderImpl::topicality_threshold_ = -1;
std::vector<ScoredHistoryMatchBuilderImpl::ScoreMaxRelevance>*
    ScoredHistoryMatchBuilderImpl::hqp_relevance_buckets_ = NULL;

ScoredHistoryMatchBuilderImpl::ScoredHistoryMatchBuilderImpl(
    const IsBookmarkedCallback& is_bookmarked)
    : is_bookmarked_(is_bookmarked) {
  Init();
}

ScoredHistoryMatchBuilderImpl::~ScoredHistoryMatchBuilderImpl() {
}

ScoredHistoryMatch ScoredHistoryMatchBuilderImpl::Build(
    const history::URLRow& row,
    const VisitInfoVector& visits,
    const std::string& languages,
    const base::string16& lower_string,
    const String16Vector& terms,
    const WordStarts& terms_to_word_starts_offsets,
    const RowWordStarts& word_starts,
    const base::Time now) const {
  ScoredHistoryMatch scored_history_match = ScoredHistoryMatch(
      row, 0, false, false, 0, TermMatches(), TermMatches(), false);

  GURL gurl = row.url();
  if (!gurl.is_valid())
    return scored_history_match;

  // Figure out where each search term appears in the URL and/or page title
  // so that we can score as well as provide autocomplete highlighting.
  base::OffsetAdjuster::Adjustments adjustments;
  base::string16 url =
      bookmarks::CleanUpUrlForMatching(gurl, languages, &adjustments);
  base::string16 title = bookmarks::CleanUpTitleForMatching(row.title());
  int term_num = 0;
  for (const auto& term : terms) {
    TermMatches url_term_matches = MatchTermInString(term, url, term_num);
    TermMatches title_term_matches = MatchTermInString(term, title, term_num);
    if (url_term_matches.empty() && title_term_matches.empty()) {
      // A term was not found in either URL or title - reject.
      return scored_history_match;
    }
    scored_history_match.url_matches.insert(
        scored_history_match.url_matches.end(), url_term_matches.begin(),
        url_term_matches.end());
    scored_history_match.title_matches.insert(
        scored_history_match.title_matches.end(), title_term_matches.begin(),
        title_term_matches.end());
    ++term_num;
  }

  // Sort matches by offset and eliminate any which overlap.
  // TODO(mpearson): Investigate whether this has any meaningful
  // effect on scoring.  (It's necessary at some point: removing
  // overlaps and sorting is needed to decide what to highlight in the
  // suggestion string.  But this sort and de-overlap doesn't have to
  // be done before scoring.)
  scored_history_match.url_matches =
      SortAndDeoverlapMatches(scored_history_match.url_matches);
  scored_history_match.title_matches =
      SortAndDeoverlapMatches(scored_history_match.title_matches);

  // We can inline autocomplete a match if:
  //  1) there is only one search term
  //  2) AND the match begins immediately after one of the prefixes in
  //     URLPrefix such as http://www and https:// (note that one of these
  //     is the empty prefix, for cases where the user has typed the scheme)
  //  3) AND the search string does not end in whitespace (making it look to
  //     the IMUI as though there is a single search term when actually there
  //     is a second, empty term).
  // |best_inlineable_prefix| stores the inlineable prefix computed in
  // clause (2) or NULL if no such prefix exists.  (The URL is not inlineable.)
  // Note that using the best prefix here means that when multiple
  // prefixes match, we'll choose to inline following the longest one.
  // For a URL like "http://www.washingtonmutual.com", this means
  // typing "w" will inline "ashington..." instead of "ww.washington...".
  if (!scored_history_match.url_matches.empty() && (terms.size() == 1) &&
      !IsWhitespace(*lower_string.rbegin())) {
    const base::string16 gurl_spec = base::UTF8ToUTF16(gurl.spec());
    const URLPrefix* best_inlineable_prefix =
        URLPrefix::BestURLPrefix(gurl_spec, terms[0]);
    if (best_inlineable_prefix) {
      // Initialize innermost_match.
      // The idea here is that matches that occur in the scheme or
      // "www." are worse than matches which don't.  For the URLs
      // "http://www.google.com" and "http://wellsfargo.com", we want
      // the omnibox input "w" to cause the latter URL to rank higher
      // than the former.  Note that this is not the same as checking
      // whether one match's inlinable prefix has more components than
      // the other match's, since in this example, both matches would
      // have an inlinable prefix of "http://", which is one component.
      //
      // Instead, we look for the overall best (i.e., most components)
      // prefix of the current URL, and then check whether the inlinable
      // prefix has that many components.  If it does, this is an
      // "innermost" match, and should be boosted.  In the example
      // above, the best prefixes for the two URLs have two and one
      // components respectively, while the inlinable prefixes each
      // have one component; this means the first match is not innermost
      // and the second match is innermost, resulting in us boosting the
      // second match.
      //
      // Now, the code that implements this.
      // The deepest prefix for this URL regardless of where the match is.
      const URLPrefix* best_prefix =
          URLPrefix::BestURLPrefix(gurl_spec, base::string16());
      DCHECK(best_prefix);
      // If the URL is inlineable, we must have a match.  Note the prefix that
      // makes it inlineable may be empty.
      scored_history_match.can_inline = true;
      scored_history_match.innermost_match =
          best_inlineable_prefix->num_components == best_prefix->num_components;
    }
  }

  const float topicality_score =
      GetTopicalityScore(terms.size(), url, terms_to_word_starts_offsets,
                         word_starts, &scored_history_match);
  const float frequency_score = GetFrequency(
      now, (!is_bookmarked_.is_null() && is_bookmarked_.Run(gurl)), visits);
  scored_history_match.raw_score = base::saturated_cast<int>(
      GetFinalRelevancyScore(topicality_score, frequency_score,
                             *hqp_relevance_buckets_));

  if (kAlsoDoHupLikeScoring && scored_history_match.can_inline) {
    // HistoryURL-provider-like scoring gives any match that is
    // capable of being inlined a certain minimum score.  Some of these
    // are given a higher score that lets them be shown in inline.
    // This test here derives from the test in
    // HistoryURLProvider::PromoteMatchForInlineAutocomplete().
    const bool promote_to_inline =
        (row.typed_count() > 1) ||
        (scored_history_match.IsHostOnly() && (row.typed_count() == 1));
    int hup_like_score = promote_to_inline ?
        HistoryURLProvider::kScoreForBestInlineableResult :
        HistoryURLProvider::kBaseScoreForNonInlineableResult;

    // Also, if the user types the hostname of a host with a typed
    // visit, then everything from that host get given inlineable scores
    // (because the URL-that-you-typed will go first and everything
    // else will be assigned one minus the previous score, as coded
    // at the end of HistoryURLProvider::DoAutocomplete().
    if (base::UTF8ToUTF16(gurl.host()) == terms[0])
      hup_like_score = HistoryURLProvider::kScoreForBestInlineableResult;

    // HistoryURLProvider has the function PromoteOrCreateShorterSuggestion()
    // that's meant to promote prefixes of the best match (if they've
    // been visited enough related to the best match) or
    // create/promote host-only suggestions (even if they've never
    // been typed).  The code is complicated and we don't try to
    // duplicate the logic here.  Instead, we handle a simple case: in
    // low-typed-count ranges, give host-only matches (i.e.,
    // http://www.foo.com/ vs. http://www.foo.com/bar.html) a boost so
    // that the host-only match outscores all the other matches that
    // would normally have the same base score.  This behavior is not
    // identical to what happens in HistoryURLProvider even in these
    // low typed count ranges--sometimes it will create/promote when
    // this test does not (indeed, we cannot create matches like HUP
    // can) and vice versa--but the underlying philosophy is similar.
    if (!promote_to_inline && scored_history_match.IsHostOnly())
      hup_like_score++;

    // All the other logic to goes into hup-like-scoring happens in
    // the tie-breaker case of MatchScoreGreater().

    // Incorporate hup_like_score into raw_score.
    scored_history_match.raw_score =
        std::max(scored_history_match.raw_score, hup_like_score);
  }

  // If this match is not inlineable and there's a cap on the maximum
  // score that can be given to non-inlineable matches, apply the cap.
  if (!scored_history_match.can_inline &&
      (max_assigned_score_for_non_inlineable_matches != -1)) {
    scored_history_match.raw_score =
        std::min(scored_history_match.raw_score,
                 max_assigned_score_for_non_inlineable_matches);
  }

  // Now that we're done processing this entry, correct the offsets of the
  // matches in |url_matches| so they point to offsets in the original URL
  // spec, not the cleaned-up URL string that we used for matching.
  std::vector<size_t> offsets =
      OffsetsFromTermMatches(scored_history_match.url_matches);
  base::OffsetAdjuster::UnadjustOffsets(adjustments, &offsets);
  scored_history_match.url_matches =
      ReplaceOffsetsInTermMatches(scored_history_match.url_matches, offsets);

  return scored_history_match;
}

// static
TermMatches ScoredHistoryMatchBuilderImpl::FilterTermMatchesByWordStarts(
    const TermMatches& term_matches,
    const WordStarts& terms_to_word_starts_offsets,
    const WordStarts& word_starts,
    size_t start_pos,
    size_t end_pos) {
  // Return early if no filtering is needed.
  if (start_pos == std::string::npos)
    return term_matches;
  TermMatches filtered_matches;
  WordStarts::const_iterator next_word_starts = word_starts.begin();
  WordStarts::const_iterator end_word_starts = word_starts.end();
  for (const auto& term_match : term_matches) {
    const size_t term_offset =
        terms_to_word_starts_offsets[term_match.term_num];
    // Advance next_word_starts until it's >= the position of the term we're
    // considering (adjusted for where the word begins within the term).
    while ((next_word_starts != end_word_starts) &&
           (*next_word_starts < (term_match.offset + term_offset)))
      ++next_word_starts;
    // Add the match if it's before the position we start filtering at or
    // after the position we stop filtering at (assuming we have a position
    // to stop filtering at) or if it's at a word boundary.
    if ((term_match.offset < start_pos) ||
        ((end_pos != std::string::npos) && (term_match.offset >= end_pos)) ||
        ((next_word_starts != end_word_starts) &&
         (*next_word_starts == term_match.offset + term_offset)))
      filtered_matches.push_back(term_match);
  }
  return filtered_matches;
}

void ScoredHistoryMatchBuilderImpl::Init() {
  // Because the code below is not thread safe, we check that we're only calling
  // it from one thread: the UI thread.  Specifically, we check "if we've heard
  // of the UI thread then we'd better be on it."  The first part is necessary
  // so unit tests pass.  (Many unit tests don't set up the threading naming
  // system; hence CurrentlyOn(UI thread) will fail.)
  using content::BrowserThread;
  DCHECK(!BrowserThread::IsThreadInitialized(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (initialized)
    return;

  initialized = true;

  // When doing HUP-like scoring, don't allow a non-inlineable match
  // to beat the score of good inlineable matches.  This is a problem
  // because if a non-inlineable match ends up with the highest score
  // from HistoryQuick provider, all HistoryQuick matches get demoted
  // to non-inlineable scores (scores less than 1200).  Without
  // HUP-like-scoring, these results would actually come from the HUP
  // and not be demoted, thus outscoring the demoted HQP results.
  // When the HQP provides these, we need to clamp the non-inlineable
  // results to preserve this behavior.
  if (kAlsoDoHupLikeScoring) {
    max_assigned_score_for_non_inlineable_matches =
        HistoryURLProvider::kScoreForBestInlineableResult - 1;
  }
  bookmark_value_ = OmniboxFieldTrial::HQPBookmarkValue();
  allow_tld_matches_ = OmniboxFieldTrial::HQPAllowMatchInTLDValue();
  allow_scheme_matches_ = OmniboxFieldTrial::HQPAllowMatchInSchemeValue();

  InitRawTermScoreToTopicalityScoreArray();
  InitDaysAgoToRecencyScoreArray();
  InitHQPExperimentalParams();
}

// static
float ScoredHistoryMatchBuilderImpl::GetTopicalityScore(
    const int num_terms,
    const base::string16& url,
    const WordStarts& terms_to_word_starts_offsets,
    const RowWordStarts& word_starts,
    ScoredHistoryMatch* scored_history_match) {
  DCHECK(initialized);
  // A vector that accumulates per-term scores.  The strongest match--a
  // match in the hostname at a word boundary--is worth 10 points.
  // Everything else is less.  In general, a match that's not at a word
  // boundary is worth about 1/4th or 1/5th of a match at the word boundary
  // in the same part of the URL/title.
  DCHECK_GT(num_terms, 0);
  std::vector<int> term_scores(num_terms, 0);
  WordStarts::const_iterator next_word_starts =
      word_starts.url_word_starts_.begin();
  WordStarts::const_iterator end_word_starts =
      word_starts.url_word_starts_.end();
  const size_t question_mark_pos = url.find('?');
  const size_t colon_pos = url.find(':');
  // The + 3 skips the // that probably appears in the protocol
  // after the colon.  If the protocol doesn't have two slashes after
  // the colon, that's okay--all this ends up doing is starting our
  // search for the next / a few characters into the hostname.  The
  // only times this can cause problems is if we have a protocol without
  // a // after the colon and the hostname is only one or two characters.
  // This isn't worth worrying about.
  const size_t end_of_hostname_pos = (colon_pos != std::string::npos) ?
      url.find('/', colon_pos + 3) : url.find('/');
  size_t last_part_of_hostname_pos =
      (end_of_hostname_pos != std::string::npos) ?
      url.rfind('.', end_of_hostname_pos) : url.rfind('.');
  // Loop through all URL matches and score them appropriately.
  // First, filter all matches not at a word boundary and in the path (or
  // later).
  scored_history_match->url_matches = FilterTermMatchesByWordStarts(
      scored_history_match->url_matches, terms_to_word_starts_offsets,
      word_starts.url_word_starts_, end_of_hostname_pos, std::string::npos);
  if (colon_pos != std::string::npos) {
    // Also filter matches not at a word boundary and in the scheme.
    scored_history_match->url_matches = FilterTermMatchesByWordStarts(
        scored_history_match->url_matches, terms_to_word_starts_offsets,
        word_starts.url_word_starts_, 0, colon_pos);
  }
  for (const auto& url_match : scored_history_match->url_matches) {
    const size_t term_offset = terms_to_word_starts_offsets[url_match.term_num];
    // Advance next_word_starts until it's >= the position of the term we're
    // considering (adjusted for where the word begins within the term).
    while ((next_word_starts != end_word_starts) &&
           (*next_word_starts < (url_match.offset + term_offset))) {
      ++next_word_starts;
    }
    const bool at_word_boundary =
        (next_word_starts != end_word_starts) &&
        (*next_word_starts == url_match.offset + term_offset);
    if ((question_mark_pos != std::string::npos) &&
        (url_match.offset > question_mark_pos)) {
      // The match is in a CGI ?... fragment.
      DCHECK(at_word_boundary);
      term_scores[url_match.term_num] += 5;
    } else if ((end_of_hostname_pos != std::string::npos) &&
               (url_match.offset > end_of_hostname_pos)) {
      // The match is in the path.
      DCHECK(at_word_boundary);
      term_scores[url_match.term_num] += 8;
    } else if ((colon_pos == std::string::npos) ||
               (url_match.offset > colon_pos)) {
      // The match is in the hostname.
      if ((last_part_of_hostname_pos == std::string::npos) ||
          (url_match.offset < last_part_of_hostname_pos)) {
        // Either there are no dots in the hostname or this match isn't
        // the last dotted component.
        term_scores[url_match.term_num] += at_word_boundary ? 10 : 2;
      } else {
        // The match is in the last part of a dotted hostname (usually this
        // is the top-level domain .com, .net, etc.).
        if (allow_tld_matches_)
          term_scores[url_match.term_num] += at_word_boundary ? 10 : 0;
      }
    } else {
      // The match is in the protocol (a.k.a. scheme).
      // Matches not at a word boundary should have been filtered already.
      DCHECK(at_word_boundary);
      scored_history_match->match_in_scheme = true;
      if (allow_scheme_matches_)
        term_scores[url_match.term_num] += 10;
    }
  }
  // Now do the analogous loop over all matches in the title.
  next_word_starts = word_starts.title_word_starts_.begin();
  end_word_starts = word_starts.title_word_starts_.end();
  int word_num = 0;
  scored_history_match->title_matches = FilterTermMatchesByWordStarts(
      scored_history_match->title_matches, terms_to_word_starts_offsets,
      word_starts.title_word_starts_, 0, std::string::npos);
  for (const auto& title_match : scored_history_match->title_matches) {
    const size_t term_offset =
        terms_to_word_starts_offsets[title_match.term_num];
    // Advance next_word_starts until it's >= the position of the term we're
    // considering (adjusted for where the word begins within the term).
    while ((next_word_starts != end_word_starts) &&
           (*next_word_starts < (title_match.offset + term_offset))) {
      ++next_word_starts;
      ++word_num;
    }
    if (word_num >= 10)
      break;  // only count the first ten words
    DCHECK(next_word_starts != end_word_starts);
    DCHECK_EQ(*next_word_starts, title_match.offset + term_offset)
        << "not at word boundary";
    term_scores[title_match.term_num] += 8;
  }
  // TODO(mpearson): Restore logic for penalizing out-of-order matches.
  // (Perhaps discount them by 0.8?)
  // TODO(mpearson): Consider: if the earliest match occurs late in the string,
  // should we discount it?
  // TODO(mpearson): Consider: do we want to score based on how much of the
  // input string the input covers?  (I'm leaning toward no.)

  // Compute the topicality_score as the sum of transformed term_scores.
  float topicality_score = 0;
  for (int term_score : term_scores) {
    // Drop this URL if it seems like a term didn't appear or, more precisely,
    // didn't appear in a part of the URL or title that we trust enough
    // to give it credit for.  For instance, terms that appear in the middle
    // of a CGI parameter get no credit.  Almost all the matches dropped
    // due to this test would look stupid if shown to the user.
    if (term_score == 0)
      return 0;
    topicality_score += raw_term_score_to_topicality_score[std::min(
        term_score, kMaxRawTermScore - 1)];
  }
  // TODO(mpearson): If there are multiple terms, consider taking the
  // geometric mean of per-term scores rather than the arithmetic mean.

  const float final_topicality_score = topicality_score / num_terms;

  // Demote the URL if the topicality score is less than threshold.
  if (hqp_experimental_scoring_enabled_ &&
      (final_topicality_score < topicality_threshold_)) {
    return 0.0;
  }

  return final_topicality_score;
}

// static
float ScoredHistoryMatchBuilderImpl::GetRecencyScore(int last_visit_days_ago) {
  DCHECK(initialized);
  // Lookup the score in days_ago_to_recency_score, treating
  // everything older than what we've precomputed as the oldest thing
  // we've precomputed.  The std::max is to protect against corruption
  // in the database (in case last_visit_days_ago is negative).
  return days_ago_to_recency_score[std::max(
      std::min(last_visit_days_ago, kDaysToPrecomputeRecencyScoresFor - 1), 0)];
}

// static
float ScoredHistoryMatchBuilderImpl::GetFrequency(
    const base::Time& now,
    const bool bookmarked,
    const VisitInfoVector& visits) {
  // Compute the weighted average |value_of_transition| over the last at
  // most kMaxVisitsToScore visits, where each visit is weighted using
  // GetRecencyScore() based on how many days ago it happened.  Use
  // kMaxVisitsToScore as the denominator for the average regardless of
  // how many visits there were in order to penalize a match that has
  // fewer visits than kMaxVisitsToScore.
  float summed_visit_points = 0;
  const size_t max_visit_to_score =
      std::min(visits.size(), ScoredHistoryMatch::kMaxVisitsToScore);
  for (size_t i = 0; i < max_visit_to_score; ++i) {
    int value_of_transition =
        (visits[i].second == ui::PAGE_TRANSITION_TYPED) ? 20 : 1;
    if (bookmarked)
      value_of_transition = std::max(value_of_transition, bookmark_value_);
    const float bucket_weight =
        GetRecencyScore((now - visits[i].first).InDays());
    summed_visit_points += (value_of_transition * bucket_weight);
  }
  return visits.size() * summed_visit_points /
         ScoredHistoryMatch::kMaxVisitsToScore;
}

// static
float ScoredHistoryMatchBuilderImpl::GetFinalRelevancyScore(
    float topicality_score,
    float frequency_score,
    const std::vector<ScoreMaxRelevance>& hqp_relevance_buckets) {
  DCHECK(hqp_relevance_buckets.size() > 0);
  DCHECK_EQ(hqp_relevance_buckets[0].first, 0.0);

  if (topicality_score == 0)
    return 0;
  // Here's how to interpret intermediate_score: Suppose the omnibox
  // has one input term.  Suppose we have a URL for which the omnibox
  // input term has a single URL hostname hit at a word boundary.  (This
  // implies topicality_score = 1.0.).  Then the intermediate_score for
  // this URL will depend entirely on the frequency_score with
  // this interpretation:
  // - a single typed visit more than three months ago, no other visits -> 0.2
  // - a visit every three days, no typed visits -> 0.706
  // - a visit every day, no typed visits -> 0.916
  // - a single typed visit yesterday, no other visits -> 2.0
  // - a typed visit once a week -> 11.77
  // - a typed visit every three days -> 14.12
  // - at least ten typed visits today -> 20.0 (maximum score)
  //
  // The below code maps intermediate_score to the range [0, 1399].
  // For example:
  // HQP default scoring buckets: "0.0:400,1.5:600,12.0:1300,20.0:1399"
  // We will linearly interpolate the scores between:
  //      0 to 1.5    --> 400 to 600
  //    1.5 to 12.0   --> 600 to 1300
  //    12.0 to 20.0  --> 1300 to 1399
  //       >= 20.0    --> 1399
  //
  // The score maxes out at 1399 (i.e., cannot beat a good inlineable result
  // from HistoryURL provider).
  const float intermediate_score = topicality_score * frequency_score;

  // Find the threshold where intermediate score is greater than bucket.
  size_t i = 1;
  for (; i < hqp_relevance_buckets.size(); ++i) {
    const ScoreMaxRelevance& hqp_bucket = hqp_relevance_buckets[i];
    if (intermediate_score >= hqp_bucket.first) {
      continue;
    }
    const ScoreMaxRelevance& previous_bucket = hqp_relevance_buckets[i-1];
    const float slope = ((hqp_bucket.second - previous_bucket.second) /
                         (hqp_bucket.first - previous_bucket.first));
    return (previous_bucket.second +
            (slope * (intermediate_score - previous_bucket.first)));
  }
  // It will reach this stage when the score is > highest bucket score.
  // Return the highest bucket score.
  return hqp_relevance_buckets[i-1].second;
}

// static
void ScoredHistoryMatchBuilderImpl::InitHQPExperimentalParams() {
  // These are default HQP relevance scoring buckets.
  // See GetFinalRelevancyScore() for details.
  std::string hqp_relevance_buckets_str = "0.0:400,1.5:600,12.0:1300,20.0:1399";

  // Fetch the experiment params if they are any.
  hqp_experimental_scoring_enabled_ =
    OmniboxFieldTrial::HQPExperimentalScoringEnabled();

  if (hqp_experimental_scoring_enabled_) {
    // Add the topicality threshold from experiment params.
    float hqp_experimental_topicality_threhold =
      OmniboxFieldTrial::HQPExperimentalTopicalityThreshold();
      topicality_threshold_ = hqp_experimental_topicality_threhold;

    // Add the HQP experimental scoring buckets.
    std::string hqp_experimental_scoring_buckets =
      OmniboxFieldTrial::HQPExperimentalScoringBuckets();
    if (!hqp_experimental_scoring_buckets.empty())
      hqp_relevance_buckets_str = hqp_experimental_scoring_buckets;
  }

  // Parse the hqp_relevance_buckets_str string once and store them in vector
  // which is easy to access.
  hqp_relevance_buckets_ =
      new std::vector<ScoredHistoryMatchBuilderImpl::ScoreMaxRelevance>();

  bool is_valid_bucket_str = GetHQPBucketsFromString(hqp_relevance_buckets_str,
                                                     hqp_relevance_buckets_);
  DCHECK(is_valid_bucket_str);
}

// static
bool ScoredHistoryMatchBuilderImpl::GetHQPBucketsFromString(
    const std::string& buckets_str,
    std::vector<ScoreMaxRelevance>* hqp_buckets) {
  DCHECK(hqp_buckets != NULL);
  DCHECK(!buckets_str.empty());

  base::StringPairs kv_pairs;
  if (base::SplitStringIntoKeyValuePairs(buckets_str,
                                         ':', ',', &kv_pairs)) {
    for (base::StringPairs::const_iterator it = kv_pairs.begin();
         it != kv_pairs.end(); ++it) {
      ScoreMaxRelevance bucket;
      bool is_valid_intermediate_score = base::StringToDouble(it->first,
                                                              &bucket.first);
      DCHECK(is_valid_intermediate_score);
      bool is_valid_hqp_score = base::StringToInt(it->second,
                                                  &bucket.second);
      DCHECK(is_valid_hqp_score);
      hqp_buckets->push_back(bucket);
    }
    return true;
  }
  return false;
}
