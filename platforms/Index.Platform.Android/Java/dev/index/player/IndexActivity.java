package dev.index.player;

import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Rect;
import android.util.Log;
import android.view.ActionMode;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;

import java.nio.charset.StandardCharsets;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/** Loads the Mono runtime before libmain.so, which links against it. */
public class IndexActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "monosgen-2.0", "main" };
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new IndexSurface(context);
    }

    // Must match Platform::TextAction / TextActionFlags.
    private static final int TEXT_CUT = 1, TEXT_COPY = 2, TEXT_PASTE = 3, TEXT_SELECT_ALL = 4;
    private static native void nativeTextAction(int action);

    private ActionMode mTextActions;
    private final Rect mTextActionsRect = new Rect();

    /** Called from native code (Platform::ShowTextActions): the system's floating Cut / Copy / Paste toolbar. */
    public void showTextActions(final int x, final int y, final int width, final int height, final int flags) {
        // Plain Runnables: the player's javac step has no lambda support.
        runOnUiThread(new Runnable() { @Override public void run() {
            if (mTextActions != null) mTextActions.finish();
            mTextActionsRect.set(x, y, x + width, y + height);
            View anchor = mSurface;
            if (anchor == null) return;
            mTextActions = anchor.startActionMode(new ActionMode.Callback2() {
                @Override
                public boolean onCreateActionMode(ActionMode mode, Menu menu) {
                    // System strings, so the labels follow the phone's language.
                    if ((flags & 1) != 0) menu.add(Menu.NONE, TEXT_CUT, 0, android.R.string.cut);
                    if ((flags & 2) != 0) menu.add(Menu.NONE, TEXT_COPY, 1, android.R.string.copy);
                    if ((flags & 4) != 0) menu.add(Menu.NONE, TEXT_PASTE, 2, android.R.string.paste);
                    if ((flags & 8) != 0) menu.add(Menu.NONE, TEXT_SELECT_ALL, 3, android.R.string.selectAll);
                    return menu.size() > 0;
                }

                @Override
                public boolean onPrepareActionMode(ActionMode mode, Menu menu) {
                    return false;
                }

                @Override
                public boolean onActionItemClicked(ActionMode mode, MenuItem item) {
                    nativeTextAction(item.getItemId());
                    mode.finish();
                    return true;
                }

                @Override
                public void onDestroyActionMode(ActionMode mode) {
                    if (mTextActions == mode) mTextActions = null;
                }

                @Override
                public void onGetContentRect(ActionMode mode, View view, Rect outRect) {
                    outRect.set(mTextActionsRect);
                }
            }, ActionMode.TYPE_FLOATING);
        }});
    }

    /** Called from native code (Platform::HideTextActions). */
    public void hideTextActions() {
        runOnUiThread(new Runnable() { @Override public void run() {
            if (mTextActions != null) {
                mTextActions.finish();
                mTextActions = null;
            }
        }});
    }

    /** Called from native code (Platform::SetMediaNowPlaying). Strings arrive as UTF-8 bytes; argb may be null. */
    public void setMediaNowPlaying(byte[] title, byte[] artist, byte[] album, long durationMs, int[] argb, int width, int height) {
        Bitmap art = null;
        if (argb != null && width > 0 && height > 0 && argb.length >= width * height) {
            art = Bitmap.createBitmap(argb, width, height, Bitmap.Config.ARGB_8888);
        }
        IndexAudioService.setNowPlaying(new String(title, StandardCharsets.UTF_8), new String(artist, StandardCharsets.UTF_8),
            new String(album, StandardCharsets.UTF_8), durationMs, art);
    }

    /** Called from native code (Platform::SetMediaPlaybackState). */
    public void setMediaPlaybackState(boolean playing, long positionMs) {
        IndexAudioService.setPlaybackState(playing, positionMs);
    }

    /** Called from native code (Platform::SetBackgroundAudioService) on the SDL main thread. */
    public void setBackgroundAudio(boolean enabled) {
        Intent service = new Intent(this, IndexAudioService.class);
        try {
            if (enabled) {
                startForegroundService(service);
            } else {
                stopService(service);
            }
        } catch (RuntimeException e) {
            // Undeclared service or a start the OS refuses (e.g. from the background): audio still plays while the process lives.
            Log.w("Index", "Background audio service: " + e);
        }
    }
}
