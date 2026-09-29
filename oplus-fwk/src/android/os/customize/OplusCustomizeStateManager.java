package android.os.customize;

import android.content.Context;

public class OplusCustomizeStateManager {

    private static OplusCustomizeStateManager sInstance;

    public static OplusCustomizeStateManager getInstance(Context context) {
        if (sInstance == null) {
            sInstance = new OplusCustomizeStateManager();
        }
        return sInstance;
    }
}
