import annotate.Annotate;
public class AnnTest implements Runnable {
	public static void main(String[] args) {
//		Annotate.init();
		Annotate.setPathID(null, 0, 5040);
		System.out.println("parent's pathid is "+Annotate.getPathIDInt());
		System.out.print("   as a byte array { ");
		byte[] ba = Annotate.getPathID();
		for (int i=0; i<ba.length; i++)
			System.out.print((ba[i]<0 ? ba[i]+256 : ba[i])+" ");
		System.out.println("}");
		Annotate.setPathID(null, 0, "5040");
		System.out.println("parent's pathid is \""+Annotate.getPathIDString()+"\"");
		System.out.print("   as a byte array { ");
		ba = Annotate.getPathID();
		for (int i=0; i<ba.length; i++)
			System.out.print((ba[i]<0 ? ba[i]+256 : ba[i])+" ");
		System.out.println("}");

		new Thread(new AnnTest()).start();
		Annotate.startTask("counting", 0, "one to a hundred million");
		for (int i=0; i<100000000; i++) {}
		Annotate.startTask("subtasks", 1, "subtask");
		Annotate.endTask("subtasks", 1, "subtask");
		Annotate.endTask("counting", 0, "one to a hundred million");
		Annotate.startTask(null, 0, "has an event");
		Annotate.notice(null, 0, "the event");
		Annotate.endTask(null, 0, "has an event");
	}
	public void run() {
		Annotate.setPathID(null, 0, 8712);
		Annotate.startTask("counting", 0, "child's hundred million");
		for (int i=0; i<100000000; i++) {}
		Annotate.endTask("counting", 0, "child's hundred million");
	}
}
