package com.oplus.osense.eventinfo;

import android.os.Bundle;
import android.os.Parcel;
import android.os.Parcelable;
import java.util.Arrays;

/* loaded from: classes.dex */
public class OsenseConfig implements Parcelable {
    public static final Parcelable.Creator<OsenseConfig> CREATOR = new Parcelable.Creator<OsenseConfig>() { // from class: com.oplus.osense.eventinfo.OsenseConfig.1
        /* JADX WARN: Can't rename method to resolve collision */
        @Override // android.os.Parcelable.Creator
        public OsenseConfig createFromParcel(Parcel in) {
            return new OsenseConfig(in);
        }

        /* JADX WARN: Can't rename method to resolve collision */
        @Override // android.os.Parcelable.Creator
        public OsenseConfig[] newArray(int size) {
            return new OsenseConfig[size];
        }
    };
    private static final String TAG = "OsenseConfig";
    private int mEventType;
    private Bundle mExtra;

    public OsenseConfig(Parcel in) {
        this.mEventType = in.readInt();
        this.mExtra = in.readBundle(getClass().getClassLoader());
    }

    public OsenseConfig(int eventType, Bundle extra) {
        this.mEventType = eventType;
        this.mExtra = extra;
    }

    public int getEventType() {
        return this.mEventType;
    }

    public Bundle getExtra() {
        return this.mExtra;
    }

    public boolean equals(Object object) {
        if (object == null) {
            return false;
        }
        if (this == object) {
            return true;
        }
        if (!(object instanceof OsenseConfig)) {
            return false;
        }
        OsenseConfig event = (OsenseConfig) object;
        if (this.mEventType != event.getEventType()) {
            return false;
        }
        return true;
    }

    public String toString() {
        return "OsenseConfig{mEventType=" + this.mEventType + ", mExtra=" + (this.mExtra == null ? "null" : this.mExtra) + '}';
    }

    public int hashCode() {
        return Arrays.hashCode(new Object[]{Integer.valueOf(this.mEventType)});
    }

    @Override // android.os.Parcelable
    public int describeContents() {
        return 0;
    }

    @Override // android.os.Parcelable
    public void writeToParcel(Parcel dest, int i) {
        dest.writeInt(this.mEventType);
        dest.writeBundle(this.mExtra);
    }
}
