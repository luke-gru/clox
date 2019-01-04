/**
 * NOTE: not running because causes subsequent tests to fail (detach thread
 * cleanup code is buggy when they exit, and GC runs)
 */
requireScript("timeout");

var err = nil;
try {
  Timeout.timeout(3) -> () {
    sleep(4);
  };
} catch (TimeoutError e) {
  err = e;
}
print err and err.message; // should timeout after 3 seconds

err = nil;
try {
  Timeout.timeout(3) -> () {
    sleep(3);
  };
} catch (TimeoutError e) {
  err = e;
}
print err and err.message; // should be nil

__END__
-- expect: --
timeout
nil
