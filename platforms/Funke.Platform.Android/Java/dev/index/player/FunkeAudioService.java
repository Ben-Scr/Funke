package dev.funke.player;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.Bitmap;
import android.graphics.drawable.Icon;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Build;
import android.os.IBinder;

/**
 * Foreground media service behind Audio.PlayInBackground: keeps the backgrounded game alive and shows the
 * now-playing notification (cover, title, previous / play-pause / next, seek bar) with the same controls on the lock
 * screen and for headset buttons. Presses go to native code (Platform::PushMediaCommand); scripts poll them.
 */
public class FunkeAudioService extends Service {
    private static final String CHANNEL_ID = "funke_background_audio";
    private static final int NOTIFICATION_ID = 0x1D3A;
    private static final String ACTION_PREVIOUS = "dev.funke.player.PREVIOUS";
    private static final String ACTION_TOGGLE = "dev.funke.player.TOGGLE";
    private static final String ACTION_NEXT = "dev.funke.player.NEXT";

    // Must match Platform::MediaCommand.
    private static final int CMD_PLAY = 1, CMD_PAUSE = 2, CMD_NEXT = 3, CMD_PREVIOUS = 4, CMD_SEEK = 5;

    private static native void nativeMediaCommand(int command, long positionMs);

    // Last state from the game; kept statically because it can arrive before the service has started.
    private static String sTitle = "";
    private static String sArtist = "";
    private static String sAlbum = "";
    private static long sDurationMs;
    private static Bitmap sArt;
    private static boolean sPlaying;
    private static long sPositionMs;
    private static FunkeAudioService sInstance;

    private MediaSession mSession;

    static void setNowPlaying(String title, String artist, String album, long durationMs, Bitmap art) {
        sTitle = title;
        sArtist = artist;
        sAlbum = album;
        sDurationMs = durationMs;
        sArt = art;
        if (sInstance != null) sInstance.refresh();
    }

    static void setPlaybackState(boolean playing, long positionMs) {
        sPlaying = playing;
        sPositionMs = positionMs;
        if (sInstance != null) sInstance.refresh();
    }

    @Override
    public void onCreate() {
        super.onCreate();
        sInstance = this;
        mSession = new MediaSession(this, "FunkePlayer");
        mSession.setCallback(new MediaSession.Callback() {
            @Override public void onPlay() { nativeMediaCommand(CMD_PLAY, 0); }
            @Override public void onPause() { nativeMediaCommand(CMD_PAUSE, 0); }
            @Override public void onSkipToNext() { nativeMediaCommand(CMD_NEXT, 0); }
            @Override public void onSkipToPrevious() { nativeMediaCommand(CMD_PREVIOUS, 0); }
            @Override public void onSeekTo(long pos) { nativeMediaCommand(CMD_SEEK, pos); }
        });
        mSession.setActive(true);
    }

    @Override
    public void onDestroy() {
        sInstance = null;
        if (mSession != null) {
            mSession.setActive(false);
            mSession.release();
        }
        super.onDestroy();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent != null ? intent.getAction() : null;
        if (ACTION_PREVIOUS.equals(action)) nativeMediaCommand(CMD_PREVIOUS, 0);
        else if (ACTION_NEXT.equals(action)) nativeMediaCommand(CMD_NEXT, 0);
        else if (ACTION_TOGGLE.equals(action)) nativeMediaCommand(sPlaying ? CMD_PAUSE : CMD_PLAY, 0);
        // A start request must reach startForeground quickly, so post the notification on every start.
        startForegroundCompat(buildNotification());
        refresh();
        // The engine restarts the service itself; a killed process has no audio to bring back.
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void refresh() {
        MediaMetadata.Builder meta = new MediaMetadata.Builder()
            .putString(MediaMetadata.METADATA_KEY_TITLE, sTitle)
            .putString(MediaMetadata.METADATA_KEY_ARTIST, sArtist)
            .putString(MediaMetadata.METADATA_KEY_ALBUM, sAlbum)
            .putLong(MediaMetadata.METADATA_KEY_DURATION, sDurationMs);
        if (sArt != null) meta.putBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART, sArt);
        mSession.setMetadata(meta.build());
        // Android 13+ builds the notification's buttons and seek bar from these actions, not from the notification.
        mSession.setPlaybackState(new PlaybackState.Builder()
            .setActions(PlaybackState.ACTION_PLAY | PlaybackState.ACTION_PAUSE | PlaybackState.ACTION_PLAY_PAUSE
                | PlaybackState.ACTION_SKIP_TO_NEXT | PlaybackState.ACTION_SKIP_TO_PREVIOUS | PlaybackState.ACTION_SEEK_TO)
            .setState(sPlaying ? PlaybackState.STATE_PLAYING : PlaybackState.STATE_PAUSED, sPositionMs, sPlaying ? 1.0f : 0.0f)
            .build());

        Notification notification = buildNotification();
        if (sPlaying) {
            startForegroundCompat(notification);
        } else {
            // Paused: keep the controls but let the user swipe the notification away.
            stopForeground(STOP_FOREGROUND_DETACH);
            NotificationManager manager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            if (manager != null) manager.notify(NOTIFICATION_ID, notification);
        }
    }

    private void startForegroundCompat(Notification notification) {
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }
    }

    private PendingIntent action(String name, int code) {
        Intent intent = new Intent(this, FunkeAudioService.class).setAction(name);
        return PendingIntent.getService(this, code, intent, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    private Notification buildNotification() {
        NotificationManager manager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (manager != null && manager.getNotificationChannel(CHANNEL_ID) == null) {
            NotificationChannel channel = new NotificationChannel(CHANNEL_ID, "Playback", NotificationManager.IMPORTANCE_LOW);
            channel.setShowBadge(false);
            manager.createNotificationChannel(channel);
        }

        Intent open = new Intent(this, FunkeActivity.class);
        open.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        PendingIntent contentIntent = PendingIntent.getActivity(this, 0, open,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        int smallIcon = getApplicationInfo().icon != 0 ? getApplicationInfo().icon : android.R.drawable.ic_media_play;
        CharSequence title = sTitle.isEmpty() ? getApplicationInfo().loadLabel(getPackageManager()) : sTitle;
        Notification.Builder builder = new Notification.Builder(this, CHANNEL_ID)
            .setContentTitle(title)
            .setContentText(sArtist)
            .setSmallIcon(smallIcon)
            .setContentIntent(contentIntent)
            .setOngoing(sPlaying)
            .setShowWhen(false)
            .setVisibility(Notification.VISIBILITY_PUBLIC)
            .setCategory(Notification.CATEGORY_TRANSPORT)
            .addAction(new Notification.Action.Builder(
                Icon.createWithResource(this, android.R.drawable.ic_media_previous), "Previous", action(ACTION_PREVIOUS, 1)).build())
            .addAction(new Notification.Action.Builder(
                Icon.createWithResource(this, sPlaying ? android.R.drawable.ic_media_pause : android.R.drawable.ic_media_play),
                sPlaying ? "Pause" : "Play", action(ACTION_TOGGLE, 2)).build())
            .addAction(new Notification.Action.Builder(
                Icon.createWithResource(this, android.R.drawable.ic_media_next), "Next", action(ACTION_NEXT, 3)).build())
            .setStyle(new Notification.MediaStyle()
                .setMediaSession(mSession.getSessionToken())
                .setShowActionsInCompactView(0, 1, 2));
        if (sArt != null) builder.setLargeIcon(sArt);
        return builder.build();
    }
}
