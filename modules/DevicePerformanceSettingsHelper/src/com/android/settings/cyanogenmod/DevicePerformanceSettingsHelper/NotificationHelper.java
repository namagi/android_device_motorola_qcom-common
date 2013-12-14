/*
 * Copyright (C) 2013 The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.settings.cyanogenmod.DevicePerformanceSettingsHelper;

import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;


public class NotificationHelper {
    public static final String TAG = "PerformanceHelper";
    private Context mContext;
    private int NOTIFICATION_ID = 1;
    private Notification mNotification;
    private NotificationManager mNotificationManager;
    private PendingIntent mContentIntent;
    private CharSequence mContentTitle;

    public NotificationHelper(Context context) {
        mContext = context;
    }

    /**
     * Put the notification into the status bar
     */
    public void createNotification(String notifyText) {
        // get the notification manager
        mNotificationManager = (NotificationManager) mContext.getSystemService(Context.NOTIFICATION_SERVICE);

        // create the notification
        int icon = R.drawable.ic_notify_icon;
        CharSequence tickerText = ""; //Initial text that appears in the status bar
        long when = System.currentTimeMillis();
        mNotification = new Notification(icon, tickerText, when);

        // create the content which is shown in the notification pulldown
        mContentTitle = TAG;
        CharSequence contentText = notifyText;

        // Set a PendingIntent on a notification to tell the system what you want it to do when the notification is selected
        // I don't want to use this here so I'm just creating a blank one
        Intent notificationIntent = new Intent();
        mContentIntent = PendingIntent.getActivity(mContext, 0, notificationIntent, 0);

        // add the additional content and intent to the notification
        mNotification.setLatestEventInfo(mContext, mContentTitle, contentText, mContentIntent);

        // make this notification appear in the 'Ongoing events' section
        mNotification.flags = Notification.FLAG_AUTO_CANCEL;

        // show the notification
        mNotificationManager.notify(NOTIFICATION_ID, mNotification);
    }

    /**
     * called when the background task is complete, this removes the notification from the status bar.
     * We could also use this to add a new ‘task complete’ notification
     */
    public void completed() {
        // Remove the notification from the status bar
        mNotificationManager.cancel(NOTIFICATION_ID);
    }
}
