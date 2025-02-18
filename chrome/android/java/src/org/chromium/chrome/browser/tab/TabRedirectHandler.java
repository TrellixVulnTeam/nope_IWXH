// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tab;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.SystemClock;
import android.provider.Browser;
import android.text.TextUtils;

import org.chromium.chrome.browser.util.IntentUtils;
import org.chromium.ui.base.PageTransition;

import java.util.HashSet;
import java.util.List;

/**
 * This class contains the logic to determine effective navigation/redirect.
 */
public class TabRedirectHandler {
    /**
     * An invalid entry index.
     */
    public static final int INVALID_ENTRY_INDEX = -1;

    private static final int NAVIGATION_TYPE_NONE = 0;
    private static final int NAVIGATION_TYPE_FROM_INTENT = 1;
    private static final int NAVIGATION_TYPE_FROM_USER_TYPING = 2;
    private static final int NAVIGATION_TYPE_OTHER = 3;

    private Intent mInitialIntent;
    // A resolver list which includes all resolvers of |mInitialIntent|.
    private final HashSet<ComponentName> mCachedResolvers = new HashSet<ComponentName>();
    private boolean mIsInitialIntentHeadingToChrome;

    private long mLastNewUrlLoadingTime;
    private boolean mIsOnEffectiveRedirectChain;
    private int mInitialNavigationType;
    private int mLastCommittedEntryIndexBeforeStartingNavigation;

    private boolean mShouldStayInChromeUntilNewUrlLoading;

    private final Context mContext;

    public TabRedirectHandler(Context context) {
        mContext = context;
    }

    /**
     * Updates |mIntentHistory| and |mLastIntentUpdatedTime|. If |intent| comes from chrome and
     * currently |mIsOnEffectiveIntentRedirectChain| is true, that means |intent| was sent from
     * this tab because only the front tab or a new tab can receive an intent from chrome. In that
     * case, |intent| is added to |mIntentHistory|.
     * Otherwise, |mIntentHistory| and |mPreviousResolvers| are cleared, and then |intent| is put
     * into |mIntentHistory|.
     */
    public void updateIntent(Intent intent) {
        clear();

        if (mContext == null || intent == null || !Intent.ACTION_VIEW.equals(intent.getAction())) {
            return;
        }

        String chromePackageName = mContext.getPackageName();
        // If an intent is heading explicitly to Chrome, we should stay in Chrome.
        if (TextUtils.equals(chromePackageName, intent.getPackage())
                || TextUtils.equals(chromePackageName, intent.getStringExtra(
                        Browser.EXTRA_APPLICATION_ID))) {
            mIsInitialIntentHeadingToChrome = true;
        }

        // Copies minimum information to retrieve resolvers.
        mInitialIntent = new Intent(Intent.ACTION_VIEW);
        mInitialIntent.setData(intent.getData());
        if (intent.getCategories() != null) {
            for (String category : intent.getCategories()) {
                mInitialIntent.addCategory(category);
            }
        }
    }

    private void clearIntentHistory() {
        mIsInitialIntentHeadingToChrome = false;
        mInitialIntent = null;
        mCachedResolvers.clear();
    }

    /**
     * Resets all variables except timestamps.
     */
    public void clear() {
        clearIntentHistory();
        mInitialNavigationType = NAVIGATION_TYPE_NONE;
        mIsOnEffectiveRedirectChain = false;
        mLastCommittedEntryIndexBeforeStartingNavigation = 0;
        mShouldStayInChromeUntilNewUrlLoading = false;
    }

    public void setShouldStayInChromeUntilNewUrlLoading() {
        mShouldStayInChromeUntilNewUrlLoading = true;
    }

    /**
     * Updates new url loading information to trace navigation.
     * A time based heuristic is used to determine if this loading is an effective redirect or not
     * if core of |pageTransType| is LINK.
     *
     * http://crbug.com/322567 : Trace navigation started from an external app.
     * http://crbug.com/331571 : Trace navigation started from user typing to do not override such
     * navigation.
     * http://crbug.com/426679 : Trace every navigation and the last committed entry index right
     * before starting the navigation.
     *
     * @param pageTransType page transition type of this loading.
     * @param isRedirect whether this loading is http redirect or not.
     * @param lastUserInteractionTime time when the last user interaction was made.
     * @param lastCommittedEntryIndex the last committed entry index right before this loading.
     */
    public void updateNewUrlLoading(int pageTransType, boolean isRedirect,
            long lastUserInteractionTime, int lastCommittedEntryIndex) {
        long prevNewUrlLoadingTime = mLastNewUrlLoadingTime;
        mLastNewUrlLoadingTime = SystemClock.elapsedRealtime();

        int pageTransitionCore = pageTransType & PageTransition.CORE_MASK;

        boolean isNewLoadingStartedByUser = false;
        boolean isFromIntent = pageTransitionCore == PageTransition.LINK
                && (pageTransType & PageTransition.FROM_API) != 0;
        if (!isRedirect) {
            if ((pageTransType & PageTransition.FORWARD_BACK) != 0) {
                isNewLoadingStartedByUser = true;
            } else if (pageTransitionCore != PageTransition.LINK) {
                isNewLoadingStartedByUser = true;
            } else if (lastUserInteractionTime > prevNewUrlLoadingTime || isFromIntent) {
                isNewLoadingStartedByUser = true;
            }
        }

        if (isNewLoadingStartedByUser) {
            // Updates mInitialNavigationType for a new loading started by a user's gesture.
            if (isFromIntent && mInitialIntent != null) {
                mInitialNavigationType = NAVIGATION_TYPE_FROM_INTENT;
                mIsOnEffectiveRedirectChain = false;
            } else if (pageTransitionCore == PageTransition.TYPED) {
                mInitialNavigationType = NAVIGATION_TYPE_FROM_USER_TYPING;
                mIsOnEffectiveRedirectChain = false;
                clearIntentHistory();
            } else {
                mInitialNavigationType = NAVIGATION_TYPE_OTHER;
                mIsOnEffectiveRedirectChain = false;
                clearIntentHistory();
            }
            mLastCommittedEntryIndexBeforeStartingNavigation = lastCommittedEntryIndex;
            mShouldStayInChromeUntilNewUrlLoading = false;
        } else if (mInitialNavigationType != NAVIGATION_TYPE_NONE) {
            // Redirect chain starts from the second url loading.
            mIsOnEffectiveRedirectChain = true;
        }
    }

    /**
     * @return whether on effective intent redirect chain or not.
     */
    public boolean isOnEffectiveIntentRedirectChain() {
        return mInitialNavigationType == NAVIGATION_TYPE_FROM_INTENT && mIsOnEffectiveRedirectChain;
    }

    /**
     * @return whether we should stay in Chrome or not.
     */
    public boolean shouldStayInChrome() {
        return mIsInitialIntentHeadingToChrome
                || mInitialNavigationType == NAVIGATION_TYPE_FROM_USER_TYPING
                || mShouldStayInChromeUntilNewUrlLoading;
    }

    /**
     * @return whether on navigation or not.
     */
    public boolean isOnNavigation() {
        return mInitialNavigationType != NAVIGATION_TYPE_NONE;
    }

    /**
     * @return the last committed entry index which was saved before starting this navigation.
     */
    public int getLastCommittedEntryIndexBeforeStartingNavigation() {
        return mLastCommittedEntryIndexBeforeStartingNavigation;
    }

    /**
     * @return whether |intent| has a new resolver against |mIntentHistory| or not.
     */
    public boolean hasNewResolver(Intent intent) {
        if (mInitialIntent == null) {
            return intent != null;
        } else if (intent == null) {
            return false;
        }

        List<ComponentName> newList = IntentUtils.getIntentHandlers(mContext, intent);
        if (mCachedResolvers.isEmpty()) {
            mCachedResolvers.addAll(IntentUtils.getIntentHandlers(mContext, mInitialIntent));
        }
        for (ComponentName name : newList) {
            if (!mCachedResolvers.contains(name)) {
                return true;
            }
        }
        return false;
    }
}
