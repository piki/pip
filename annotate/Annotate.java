public class Annotate {
	public static native void init();
	public static native void startTask(String name);
	public static native void endTask(String name);
	public static native void setRequest(int reqid);
	public static native void notice(String str);
	public static native void send(int sender, int msgid, int size);
	public static native void receive(int sender, int msgid, int size);

	static {
		System.loadLibrary("jannotate");
	}
}
