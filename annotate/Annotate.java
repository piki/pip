import java.io.*;

public class Annotate {
	public static native void init();
	public static native void startTask(String name);
	public static native void endTask(String name);
	public static native void setPathID(byte[] pathid);
	public static void setPathID(int pathid) { setPathID(new Integer(pathid)); }
	public static void setPathID(Serializable pathid) { setPathID(pickle(pathid)); }
	public static native byte[] getPathID();
	public static Object getPathIDObj() { return unpickle(getPathID()); }
	public static int getPathIDInt() { return ((Integer)unpickle(getPathID())).intValue(); }
	public static native void endPathID(byte[] pathid);
	public static void endPathID(int pathid) { endPathID(new Integer(pathid)); }
	public static void endPathID(Serializable pathid) { endPathID(pickle(pathid)); }
	public static native void notice(String str);
	public static native void send(byte[] msgid, int size);
	public static void send(int msgid, int size) { send(new Integer(msgid), size); }
	public static void send(Serializable msgid, int size) { send(pickle(msgid), size); }
	public static native void receive(byte[] msgid, int size);
	public static void receive(int msgid, int size) { send(new Integer(msgid), size); }
	public static void receive(Serializable msgid, int size) { receive(pickle(msgid), size); }

	private static byte[] pickle(Serializable obj) {
		ByteArrayOutputStream bo = null;
		try {
			bo = new ByteArrayOutputStream();
			new ObjectOutputStream(bo).writeObject(obj);
		}
		catch (Exception e) {}
		return bo.toByteArray();
	}

	private static Object unpickle(byte[] arr) {
		try {
			ByteArrayInputStream bi = new ByteArrayInputStream(arr);
			return new ObjectInputStream(bi).readObject();
		}
		catch (Exception e) {}
		return null;
	}

	static {
		System.loadLibrary("jannotate");
	}
}
