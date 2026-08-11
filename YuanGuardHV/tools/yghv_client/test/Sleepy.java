public class Sleepy {
    public static void main(String[] args) throws Exception {
        byte[] buf = new byte[4096];
        System.out.println("sleepy pid=" + ProcessHandle.current().pid());
        Thread.sleep(600000);
        buf[0] = 1;
    }
}
