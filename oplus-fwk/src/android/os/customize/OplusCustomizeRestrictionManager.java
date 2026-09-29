package android.os.customize;

import android.content.Context;

public class OplusCustomizeRestrictionManager {

    private static OplusCustomizeRestrictionManager sInstance;

    public static OplusCustomizeRestrictionManager getInstance(Context context) {
        if (sInstance == null) {
            sInstance = new OplusCustomizeRestrictionManager();
        }
        return sInstance;
    }

    public boolean getForbidRecordScreenState() {
        return false;
    }
}
