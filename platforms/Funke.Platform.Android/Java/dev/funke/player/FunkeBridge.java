package dev.funke.player;

import android.app.Activity;
import android.util.Log;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;

import org.libsdl.app.SDLActivity;

/**
 * C# scripts reach Java classes that packages ship through AndroidBridge.Call, which lands in call(); those classes
 * answer asynchronously with send(), delivered to AndroidBridge.Subscribe handlers on the game's main thread.
 */
public final class FunkeBridge {
    private static final String TAG = "Funke";

    private FunkeBridge() {}

    private static native void nativeSend(byte[] channel, byte[] message);

    /** Queues a message for the C# handlers of channel. Any thread. */
    public static void send(String channel, String message) {
        try {
            nativeSend(channel.getBytes(StandardCharsets.UTF_8), (message != null ? message : "").getBytes(StandardCharsets.UTF_8));
        } catch (UnsatisfiedLinkError e) {
            Log.w(TAG, "FunkeBridge.send(" + channel + ") before the engine was loaded");
        }
    }

    /** The game's activity, or null before it exists. */
    public static Activity getActivity() {
        return SDLActivity.getContext();
    }

    /**
     * Called from native code (Platform::CallJava): runs `public static String method(String)` on className.
     * Strings travel as UTF-8 bytes; null means the call failed.
     */
    static byte[] call(byte[] className, byte[] method, byte[] argument) {
        String target = new String(className, StandardCharsets.UTF_8);
        String name = new String(method, StandardCharsets.UTF_8);
        try {
            Class<?> type = Class.forName(target, true, FunkeBridge.class.getClassLoader());
            Method m = type.getMethod(name, String.class);
            Object answer = m.invoke(null, new String(argument, StandardCharsets.UTF_8));
            return answer != null ? answer.toString().getBytes(StandardCharsets.UTF_8) : new byte[0];
        } catch (InvocationTargetException e) {
            Log.e(TAG, "FunkeBridge: " + target + "." + name + " threw", e.getCause());
        } catch (ReflectiveOperationException | RuntimeException e) {
            Log.e(TAG, "FunkeBridge: cannot call " + target + "." + name + "(String): " + e);
        }
        return null;
    }
}
