public class AnnTest implements Runnable {
	public static void main(String[] args) {
		Annotate.init();
		Annotate.setPathID(5040);
		System.out.println("parent's pathid is "+Annotate.getPathIDInt());
		System.out.println("      as an object "+Annotate.getPathIDObj());
		System.out.println("   as a byte array "+Annotate.getPathID());
		new Thread(new AnnTest()).start();
		Annotate.startTask("one to a hundred million");
		for (int i=0; i<100000000; i++) {}
		Annotate.startTask("subtask");
		Annotate.endTask("subtask");
		Annotate.endTask("one to a hundred million");
		Annotate.startTask("has an event");
		Annotate.notice("the event");
		Annotate.endTask("has an event");
	}
	public void run() {
		Annotate.setPathID(8712);
		Annotate.startTask("child's hundred million");
		for (int i=0; i<100000000; i++) {}
		Annotate.endTask("child's hundred million");
	}
}
