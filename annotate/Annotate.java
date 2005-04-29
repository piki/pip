import java.io.*;

public class Annotate {
	public static native void init();
	public static native void startTask(String roles, int level, String name);
	public static native void endTask(String roles, int level, String name);
	public static native void setPathID(String roles, int level, byte[] pathid);
	public static native byte[] getPathID();
	public static native void endPathID(String roles, int level, byte[] pathid);
	public static native void notice(String roles, int level, String str);
	public static native void send(String roles, int level, byte[] msgid, int size);
	public static native void receive(String roles, int level, byte[] msgid, int size);

	public static void setPathID(String roles, int level, int pathid) { setPathID(roles, level, pickle(pathid)); }
	public static void setPathID(String roles, int level, Serializable pathid) { setPathID(roles, level, pickle(pathid)); }
	public static void setPathID(String roles, int level, String pathid) { setPathID(roles, level, pathid.getBytes()); }
	public static Object getPathIDObj() { return unpickleObject(getPathID()); }
	public static int getPathIDInt() { return unpickleInt(getPathID()); }
	public static String getPathIDString() { return new String(getPathID()); }
	public static void endPathID(String roles, int level, int pathid) { endPathID(roles, level, pickle(pathid)); }
	public static void endPathID(String roles, int level, Serializable pathid) { endPathID(roles, level, pickle(pathid)); }
	public static void endPathID(String roles, int level, String pathid) { endPathID(roles, level, pathid.getBytes()); }
	public static void send(String roles, int level, int msgid, int size) { send(roles, level, new Integer(msgid), size); }
	public static void send(String roles, int level, Serializable msgid, int size) { send(roles, level, pickle(msgid), size); }
	public static void send(String roles, int level, String msgid, int size) { send(roles, level, msgid.getBytes(), size); }
	public static void receive(String roles, int level, int msgid, int size) { receive(roles, level, new Integer(msgid), size); }
	public static void receive(String roles, int level, Serializable msgid, int size) { receive(roles, level, pickle(msgid), size); }
	public static void receive(String roles, int level, String msgid, int size) { receive(roles, level, msgid.getBytes(), size); }

	private static byte[] pickle(Serializable obj) {
		System.out.println("Pickling an object: "+obj);
		ByteArrayOutputStream bo = null;
		try {
			bo = new ByteArrayOutputStream();
			new ObjectOutputStream(bo).writeObject(obj);
			System.out.println("  wrote it");
		}
		catch (Exception e) {System.err.println(e);}
		byte[] B = bo.toByteArray();
		System.out.println("  returning byte array "+B+" of length "+B.length);
		return bo.toByteArray();
	}

	private static byte[] pickle(int i) {
		ByteArrayOutputStream bo = null;
		try {
			bo = new ByteArrayOutputStream();
			new DataOutputStream(bo).writeInt(i);
		}
		catch (Exception e) {System.err.println(e);}
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
