package io.github.laureladastra.mcdub3;

import android.service.notification.NotificationListenerService;

/*
 * Exists so the app can be granted "Notification access", which Android
 * requires before MediaSessionManager.getActiveSessions() returns the list of
 * players (android_media.c reads Spotify / YouTube Music / browser metadata
 * and sends transport commands through it).
 *
 * It deliberately does not read notifications: no callbacks are overridden and
 * nothing is stored. The user grants the access from the in-game Port Options
 * row (PHONE MUSIC), which opens the system screen.
 */
public class MediaSessionListener extends NotificationListenerService {
}
