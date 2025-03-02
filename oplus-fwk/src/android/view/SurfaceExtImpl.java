package android.view;

public class SurfaceExtImpl {
    private static volatile SurfaceExtImpl sInstance;
    private Object mLock;
    private long mNativeObject;
    private Surface mSurface;

    public native void nativeSetMaxDequeuedBufferCount(long j, int i);

    public SurfaceExtImpl(Object base) {
        this.mSurface = (Surface) base;
        //this.mLock = this.mSurface.getWrapper().getLock();
    }

    //static {
    //    System.loadLibrary("oplusgui_jni");
    //}

    public void setMaxDequeuedBufferCount(int bufferCount) {
        synchronized (this.mLock) {
           // this.mNativeObject = this.mSurface.getWrapper().getNativeObject();
            //if (this.mNativeObject != 0) {
            //    nativeSetMaxDequeuedBufferCount(this.mNativeObject, bufferCount);
            //}
        }
    }
}
