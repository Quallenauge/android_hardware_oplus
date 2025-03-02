package com.oplus.osense;

import com.oplus.osense.eventinfo.EventConfig;
import com.oplus.osense.eventinfo.OsenseEventCallback;
import android.util.Log;
import android.os.RemoteException;

public class OsenseResEventClient {
    private static final String TAG = "OsenseResEventClient";
    private static OsenseResEventClient sInstance;

    public static OsenseResEventClient getInstance() {
        if (sInstance == null) {
            sInstance = new OsenseResEventClient();
        }
        return sInstance;
    }
    
    public int registerEventCallback(OsenseEventCallback callback, EventConfig config) {
        return 0;
    }

    public int unregisterEventCallback(OsenseEventCallback callback) {
        return 0;
    }
    
    public void requestInstantCpuLoad() {
        try {
            // Implementation would normally call into native/system services
            // For now, we're just providing the method signature to prevent the NoSuchMethodException
            Log.v(TAG, "requestInstantCpuLoad called");
        } catch (Exception e) {
            Log.e(TAG, "requestInstantCpuLoad exception: " + e.toString());
        }
    }
}
