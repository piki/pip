import java.io.*;

public class Annotate {
	public static native void init();
	public static native void startTask(String name);
	public static native void endTask(String name);
	public static native void setPathID(byte[] pathid);
	public static void setPathID(int pathid) { setPathID(pickle(pathid)); }
	public static void setPathID(Serializable pathid) { setPathID(pickle(pathid)); }
	public static native byte[] getPathID();
	public static Object getPathIDObj() { return unpickleObject(getPathID()); }
	public static int getPathIDInt() { return unpickleInt(getPathID()); }
	public static native void endPathID(byte[] pathid);
	public static void endPathID(int pathid) { endPathID(pickle(pathid)); }
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

	private static byte[] pickle(int i) {
		ByteArrayOutputStream bo = null;
		try {
			bo = new ByteArrayOutputStream();
			new DataOutputStream(bo).writeInt(i);
		}
		catch (Exception e) {}
		return bo.toByteArray();
	}

	private static Object unpickleObject(byte[] arr) {
		try {
			ByteArrayInputStream bi = new ByteArrayInputStream(arr);
			return new ObjectInputStream(bi).readObject();
		}
		catch (Exception e) {}
		return null;
	}

	private static int unpickleInt(byte[] arr) {
		try {
			ByteArrayInputStream bi = new ByteArrayInputStream(arr);
			return new DataInputStream(bi).readInt();
		}
		catch (Exception e) {}
		return -1;
	}

	static {
		System.loadLibrary("jannotate");
	}
}
