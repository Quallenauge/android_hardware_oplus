package com.oplus.osense.eventinfo;

import android.os.Parcel;
import android.os.Parcelable;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/* loaded from: classes.dex */
public class EventConfig implements Parcelable {
    public static final Parcelable.Creator<EventConfig> CREATOR = new Parcelable.Creator<EventConfig>() { // from class: com.oplus.osense.eventinfo.EventConfig.1
        /* JADX WARN: Can't rename method to resolve collision */
        @Override // android.os.Parcelable.Creator
        public EventConfig createFromParcel(Parcel in) {
            return new EventConfig(in);
        }

        /* JADX WARN: Can't rename method to resolve collision */
        @Override // android.os.Parcelable.Creator
        public EventConfig[] newArray(int size) {
            return new EventConfig[size];
        }
    };
    private Set<Integer> mEventSet;
    private Set<OsenseConfig> mOsenseConfigSet;

    public EventConfig(Parcel in) {
        ClassLoader loader = EventConfig.class.getClassLoader();
        List<Integer> eventSet = new ArrayList<>();
        in.readList(eventSet, loader);
        this.mEventSet = new HashSet();
        if (!eventSet.isEmpty()) {
            this.mEventSet.addAll(eventSet);
        }
        List<OsenseConfig> osenseConfigSet = new ArrayList<>();
        in.readList(osenseConfigSet, loader);
        this.mOsenseConfigSet = new HashSet();
        if (!osenseConfigSet.isEmpty()) {
            this.mOsenseConfigSet.addAll(osenseConfigSet);
        }
    }

    public EventConfig() {
        this.mEventSet = new HashSet();
        this.mOsenseConfigSet = new HashSet();
    }

    public EventConfig(HashSet<Integer> events) {
        this.mEventSet = new HashSet();
        if (events != null && !events.isEmpty()) {
            this.mEventSet.addAll(events);
        }
        this.mOsenseConfigSet = new HashSet();
    }

    public void setOsenseConfigSet(HashSet<OsenseConfig> osenseConfigSet) {
        this.mOsenseConfigSet.clear();
        this.mOsenseConfigSet.addAll(osenseConfigSet);
    }

    public Set<Integer> getEventSet() {
        if (this.mEventSet == null) {
            this.mEventSet = new HashSet();
        }
        return this.mEventSet;
    }

    public Set<OsenseConfig> getOsenseConfigSet() {
        if (this.mOsenseConfigSet == null) {
            this.mOsenseConfigSet = new HashSet();
        }
        return this.mOsenseConfigSet;
    }

    public Set<Integer> getAllEventTypes() {
        Set<Integer> allEventTypes = new HashSet<>();
        if (this.mOsenseConfigSet != null && !this.mOsenseConfigSet.isEmpty()) {
            for (OsenseConfig osenseConfig : this.mOsenseConfigSet) {
                allEventTypes.add(Integer.valueOf(osenseConfig.getEventType()));
            }
        }
        if (this.mEventSet != null && !this.mEventSet.isEmpty()) {
            allEventTypes.addAll(this.mEventSet);
        }
        return allEventTypes;
    }

    public Set<OsenseConfig> getAllOsenseConfigs() {
        Set<OsenseConfig> osenseConfigs = new HashSet<>();
        if (this.mEventSet != null && !this.mEventSet.isEmpty()) {
            for (Integer eventType : this.mEventSet) {
                OsenseConfig tempOsenseConfig = new OsenseConfig(eventType.intValue(), null);
                osenseConfigs.add(tempOsenseConfig);
            }
        }
        if (this.mOsenseConfigSet != null && !this.mOsenseConfigSet.isEmpty()) {
            osenseConfigs.addAll(this.mOsenseConfigSet);
        }
        return osenseConfigs;
    }

    public void addEvent(int eventType) {
        if (this.mEventSet == null) {
            this.mEventSet = new HashSet();
        }
        this.mEventSet.add(Integer.valueOf(eventType));
    }

    public void addOsenseConfig(OsenseConfig osenseConfig) {
        if (this.mOsenseConfigSet == null) {
            this.mOsenseConfigSet = new HashSet();
        }
        this.mOsenseConfigSet.add(osenseConfig);
    }

    public String toString() {
        return "EventConfig{mEventSet=" + this.mEventSet + ", mOsenseConfigSet=" + this.mOsenseConfigSet + '}';
    }

    @Override // android.os.Parcelable
    public int describeContents() {
        return 0;
    }

    @Override // android.os.Parcelable
    public void writeToParcel(Parcel dest, int i) {
        if (this.mEventSet != null) {
            dest.writeList(new ArrayList(this.mEventSet));
        }
        if (this.mOsenseConfigSet != null) {
            dest.writeList(new ArrayList(this.mOsenseConfigSet));
        }
    }
}
