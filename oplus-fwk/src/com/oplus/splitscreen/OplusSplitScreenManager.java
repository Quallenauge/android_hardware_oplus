package com.oplus.splitscreen;

import com.oplus.app.IOplusSplitScreenObserver;

public class OplusSplitScreenManager {

    private static OplusSplitScreenManager sInstance;

    public static OplusSplitScreenManager getInstance() {
        if (sInstance == null) {
            sInstance = new OplusSplitScreenManager();
        }
        return sInstance;
    }

    public boolean registerSplitScreenObserver(IOplusSplitScreenObserver observer) {
        return false;
    }

    public boolean unregisterSplitScreenObserver(IOplusSplitScreenObserver observer) {
        return false;
    }
}
