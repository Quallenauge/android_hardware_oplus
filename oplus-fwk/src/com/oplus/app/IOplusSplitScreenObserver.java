package com.oplus.app;

import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.IInterface;

public interface IOplusSplitScreenObserver extends IInterface {

    void onStateChanged(String event, Bundle bundle);

    public static abstract class Stub extends Binder implements IOplusSplitScreenObserver {
        @Override
        public IBinder asBinder() {
            return this;
        }
    }
}
