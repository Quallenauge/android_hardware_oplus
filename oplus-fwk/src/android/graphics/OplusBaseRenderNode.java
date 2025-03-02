package android.graphics;

import dalvik.annotation.optimization.CriticalNative;

public abstract class OplusBaseRenderNode {
    public static final int USAGE_OPLUS_FORCE_BACKGROUND = 9;
    public static final int USAGE_OPLUS_FORCE_FOREGROUND = 10;
    public static final int USAGE_OPLUS_FORCE_UNKNOWN = 8;
    private int mAlgorithmType = 0;
    private int mBackgroundUsageHint = 1;

    @CriticalNative
    private static native void nSetForceDarkNodeType(long j, int i);

    @CriticalNative
    private static native void nSetUsageForceDarkAlgorithmType(long j, int i);

    protected abstract long getNativeRenderNode();

    static {
        System.loadLibrary("oplushwui_jni");
    }

    public int getBackgroundUsageHint() {
        return this.mBackgroundUsageHint;
    }

    public void setBackgroundUsageHint(int usageHint) {
        this.mBackgroundUsageHint = usageHint;
    }

    public void setUsageForceDarkAlgorithmType(int algorithmType) {
        if (this.mAlgorithmType != algorithmType) {
            nSetUsageForceDarkAlgorithmType(getNativeRenderNode(), algorithmType);
            this.mAlgorithmType = algorithmType;
        }
    }
}
