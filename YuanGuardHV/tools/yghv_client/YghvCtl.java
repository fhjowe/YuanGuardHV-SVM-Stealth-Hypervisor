import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;

public class YghvCtl {
    static {
        System.loadLibrary("yghv_ctl_jni");
    }

    private static final int FN_SET_TARGET = 0x800;
    private static final int FN_ADD_PAGE = 0x801;
    private static final int FN_REMOVE_PAGE = 0x802;
    private static final int FN_START_PROTECT = 0x803;
    private static final int FN_STOP_PROTECT = 0x804;
    private static final int FN_GET_STATE = 0x805;

    private static native long openHandle();
    private static native void closeHandle(long handle);
    private static native int lastError();
    private static native int enableSeDebug();
    private static native int ioctl(long handle, int fn, byte[] in, byte[] out);
    private static native long[] enumeratePages(int pid, int maxPages);

    private static long handle;

    private static int ctl(int fn) {
        return (0x5947 << 16) | (fn << 2);
    }

    private static void fail(String what, int err) {
        System.err.printf("yghv: %s failed, Win32 error 0x%X%n", what, err);
        System.exit(1);
    }

    private static void check(int err, String what) {
        if (err != 0) {
            fail(what, err);
        }
    }

    private static void open() {
        int err = enableSeDebug();
        if (err != 0) {
            fail("enable SeDebugPrivilege", err);
        }
        handle = openHandle();
        if (handle == 0) {
            fail("CreateFile", lastError());
        }
    }

    private static void close() {
        if (handle != 0) {
            closeHandle(handle);
            handle = 0;
        }
    }

    private static byte[] u32(long value) {
        return ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN)
                .putInt((int) value).array();
    }

    private static byte[] u64(long value) {
        return ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)
                .putLong(value).array();
    }

    private static long[] state() {
        byte[] out = new byte[12];
        check(ioctl(handle, FN_GET_STATE, null, out), "GET_STATE");
        ByteBuffer b = ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN);
        return new long[] {
                Integer.toUnsignedLong(b.getInt()),
                Integer.toUnsignedLong(b.getInt()),
                Integer.toUnsignedLong(b.getInt())
        };
    }

    private static void printState() {
        long[] s = state();
        System.out.printf("state: active=%d pid=%d page_count=%d%n",
                s[0], s[1], s[2]);
    }

    private static void setTarget(long pid) {
        check(ioctl(handle, FN_SET_TARGET, u32(pid), null), "SET_TARGET");
        System.out.println("set-target: pid=" + pid + " OK");
    }

    private static void addPage(long va) {
        check(ioctl(handle, FN_ADD_PAGE, u64(va), null), "ADD_PAGE");
    }

    private static void removePage(long va) {
        check(ioctl(handle, FN_REMOVE_PAGE, u64(va), null), "REMOVE_PAGE");
    }

    private static void start() {
        check(ioctl(handle, FN_START_PROTECT, null, null), "START_PROTECT");
        System.out.println("start: OK");
    }

    private static void stop() {
        check(ioctl(handle, FN_STOP_PROTECT, null, null), "STOP_PROTECT");
        System.out.println("stop: OK");
    }

    private static void listJava() {
        ProcessHandle.allProcesses()
                .filter(p -> {
                    Optional<String> cmd = p.info().commandLine();
                    Optional<String> name = p.info().command();
                    String hay = cmd.orElse("") + " " + name.orElse("");
                    return hay.toLowerCase().contains("java");
                })
                .forEach(p -> System.out.printf("pid=%d cmd=%s%n",
                        p.pid(), p.info().commandLine()
                                .orElse(p.info().command().orElse(""))));
    }

    private static void protect(long pid, int maxPages) {
        open();
        try {
            ioctl(handle, FN_STOP_PROTECT, null, null);
            setTarget(pid);

            long[] pages = enumeratePages((int) pid, maxPages);
            if (pages == null) {
                fail("OpenProcess/enumerate", lastError());
            }
            System.out.printf("protect: enumerated %d pages (cap=%d)%n",
                    pages.length, maxPages);

            int ok = 0;
            int fails = 0;
            List<String> sample = new ArrayList<>();
            for (long va : pages) {
                if (ok >= maxPages) {
                    break;
                }
                int err = ioctl(handle, FN_ADD_PAGE, u64(va), null);
                if (err == 0) {
                    ok++;
                    if (sample.size() < 3) {
                        sample.add(String.format("0x%X", va));
                    }
                } else if (fails < 3) {
                    System.out.printf("protect: add fail err=0x%X va=0x%X%n",
                            err, va);
                    fails++;
                }
            }
            System.out.printf("protect: add-page ok=%d failed=%d sample=[%s]%n",
                    ok, pages.length - ok, String.join(", ", sample));
            if (ok == 0) {
                fail("ADD_PAGE", 0);
            }

            start();
            printState();
        } finally {
            close();
        }
    }

    private static void usage() {
        System.out.println("Usage:");
        System.out.println("  YghvCtl state");
        System.out.println("  YghvCtl set-target <pid>");
        System.out.println("  YghvCtl add-page <hex_va>");
        System.out.println("  YghvCtl remove-page <hex_va>");
        System.out.println("  YghvCtl start");
        System.out.println("  YghvCtl stop");
        System.out.println("  YghvCtl list-java");
        System.out.println("  YghvCtl protect <pid> [maxPages]");
    }

    public static void main(String[] args) {
        String cmd = args.length > 0 ? args[0].toLowerCase() : "help";
        try {
            switch (cmd) {
                case "state":
                    open();
                    try {
                        printState();
                    } finally {
                        close();
                    }
                    break;
                case "set-target":
                    open();
                    try {
                        setTarget(Long.parseLong(args[1]));
                    } finally {
                        close();
                    }
                    break;
                case "add-page":
                    open();
                    try {
                        addPage(Long.decode(args[1]));
                        System.out.println("add-page: OK");
                    } finally {
                        close();
                    }
                    break;
                case "remove-page":
                    open();
                    try {
                        removePage(Long.decode(args[1]));
                        System.out.println("remove-page: OK");
                    } finally {
                        close();
                    }
                    break;
                case "start":
                    open();
                    try {
                        start();
                    } finally {
                        close();
                    }
                    break;
                case "stop":
                    open();
                    try {
                        stop();
                    } finally {
                        close();
                    }
                    break;
                case "list-java":
                    listJava();
                    break;
                case "protect": {
                    long pid = Long.parseLong(args[1]);
                    int maxPages = args.length > 2
                            ? Math.min(Integer.parseInt(args[2]), 64)
                            : 64;
                    protect(pid, maxPages);
                    break;
                }
                default:
                    usage();
                    break;
            }
        } catch (ArrayIndexOutOfBoundsException | NumberFormatException e) {
            System.err.println("yghv: bad arguments");
            usage();
            System.exit(2);
        }
    }
}
