// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.infobar;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;

import org.chromium.base.CalledByNative;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ResourceId;

/**
 * An infobar offers the user the ability to choose credentials for
 * authentication. User is presented with username along with avatar and
 * full name in case they are available.
 */
public class AccountChooserInfoBar extends InfoBar {
    private enum CredentialType {
        EMPTY(0),
        LOCAL(1),
        FEDERATED(2);

        private final int mType;
        private CredentialType(int type) {
            mType = type;
        };

        public int getValue() {
            return mType;
        }
    }

    private final String[] mUsernames;

    /**
     * Creates and shows the infobar wich allows user to choose credentials for login.
     * @param nativeInfoBar Pointer to the native infobar.
     * @param enumeratedIconId Enum ID corresponding to the icon that the infobar will show.
     * @param usernames Usernames to display in the infobar.
     */
    @CalledByNative
    private static InfoBar show(long nativeInfoBar, int enumeratedIconId, String[] usernames) {
        return new AccountChooserInfoBar(
                nativeInfoBar, ResourceId.mapToDrawableId(enumeratedIconId), usernames);
    }

    /**
     * Creates and shows the infobar  which allows user to choose credentials.
     * @param nativeInfoBar Pointer to the native infobar.
     * @param iconDrawableId Drawable ID corresponding to the icon that the infobar will show.
     * @param usernames list of usernames to display in infobar.
     */
    public AccountChooserInfoBar(long nativeInfoBar, int iconDrawableId, String[] usernames) {
        super(null /* Infobar Listener */, iconDrawableId, null /* bitmap*/,
                null /* message to show */);
        setNativeInfoBar(nativeInfoBar);
        mUsernames = usernames.clone();
    }


    @Override
    public void onCloseButtonClicked() {
        // Notifies the native infobar, which closes the infobar.
        nativeOnCloseButtonClicked(mNativeInfoBarPtr);
    }

    @Override
    public void onButtonClicked(boolean isPrimaryButton) {
        onCloseButtonClicked();
    }

    @Override
    public void createContent(InfoBarLayout layout) {
        layout.setMessage(getContext().getString(R.string.account_chooser_infobar_title));
        createAccountsView(layout);
        createCustomButtonsView(layout);
    }

    private void createAccountsView(InfoBarLayout layout) {
        ViewGroup accountsView = (ViewGroup) LayoutInflater.from(getContext()).inflate(
                R.layout.account_chooser_infobar_list, null, false);
        ArrayAdapter<String> adapter = generateAccountsArrayAdapter(getContext(), mUsernames);
        ListView listView = (ListView) accountsView.findViewById(R.id.account_list);
        listView.setAdapter(adapter);
        float numVisibleItems = adapter.getCount() > 2 ? 2.5f : adapter.getCount();
        int listViewHeight = (int) (numVisibleItems * getContext().getResources().getDimension(
                R.dimen.account_chooser_infobar_item_height));
        listView.setLayoutParams(new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, listViewHeight));
        layout.setCustomContent(accountsView);
    }

    private ArrayAdapter<String> generateAccountsArrayAdapter(Context context, String[] usernames) {
        return new ArrayAdapter<String>(context, 0, usernames) {
            @Override
            public View getView(int position, View convertView, ViewGroup parent) {
                if (convertView == null) {
                    convertView = (LinearLayout) LayoutInflater.from(getContext()).inflate(
                        R.layout.account_chooser_infobar_item, parent, false);
                }
                ImageView avatarView = (ImageView) convertView.findViewById(R.id.profile_image);
                TextView usernameView = (TextView) convertView.findViewById(R.id.username);
                TextView displayNameView = (TextView) convertView.findViewById(R.id.display_name);
                String username = getItem(position);
                usernameView.setText(username);
                // TODO(melandory): View should show the full name. Temporarily the view shows
                // username.
                displayNameView.setText(username);
                // TODO(melandory): View should show proper avatar. Temporarily the view shows
                // blue man icon.
                avatarView.setImageResource(R.drawable.account_management_no_picture);
                final int currentCredentialIndex = position;
                convertView.setOnClickListener(new View.OnClickListener() {
                    @Override
                    public void onClick(View view) {
                        passCredentialsToNative(currentCredentialIndex);
                    }
                });
                return convertView;
            }
        };
    }

    /**
     * Creates button row which consists of "No thanks" button and "More" button.
     * "No thanks" buttons dismisses infobar. "More" button opens a popup menu,
     * which allows to go to help center article or Settings.
     */
    private void createCustomButtonsView(InfoBarLayout layout) {
        layout.setButtons(getContext().getString(R.string.no_thanks), null);
        layout.setCustomViewInButtonRow(OverflowSelector.createOverflowSelector(getContext()));
    }

    private void passCredentialsToNative(int credentialIndex) {
        // TODO(melandory): Adding federated login support should change this
        // code.
        nativeOnCredentialClicked(
                mNativeInfoBarPtr, credentialIndex, CredentialType.LOCAL.getValue());
    }


    private native void nativeOnCredentialClicked(
            long nativeAccountChooserInfoBar, int credentialId, int credentialType);
}
