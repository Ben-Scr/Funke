package dev.index.player;

import android.content.Context;
import android.graphics.Insets;
import android.os.Build;
import android.view.View;
import android.view.WindowInsets;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/**
 * SDL counts the back-gesture zones at the left and right edge as unsafe, which squeezed full-width UI by ~150 px
 * on gesture-navigation phones. Only bars and the camera cutout actually cover content, so report just those.
 */
public class IndexSurface extends SDLSurface {
    public IndexSurface(Context context) {
        super(context);
    }

    @Override
    public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
        WindowInsets result = super.onApplyWindowInsets(v, insets);
        if (Build.VERSION.SDK_INT >= 30) {
            Insets visible = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            SDLActivity.onNativeInsetsChanged(visible.left, visible.right, visible.top, visible.bottom);
        }
        return result;
    }
}
