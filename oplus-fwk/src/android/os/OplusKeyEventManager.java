package android.os;

import android.view.KeyEvent;

import android.content.Context;
import android.util.ArrayMap;

public class OplusKeyEventManager {

    private static OplusKeyEventManager sInstance;

    public static OplusKeyEventManager getInstance() {
        if (sInstance == null) {
            sInstance = new OplusKeyEventManager();
        }
        return sInstance;
    }

    public boolean registerKeyEventInterceptor(Context context, String key,
            OnKeyEventObserver observer, ArrayMap<Integer, Integer> config) {
        return false;
    }

    public boolean unregisterKeyEventInterceptor(Context context, String key,
            OnKeyEventObserver observer) {
        return false;
    }

    public interface OnKeyEventObserver {
        void onKeyEvent(KeyEvent event); 
    }
}
