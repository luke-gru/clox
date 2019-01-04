/**
 * Not run in test_examples because detached threads
 * mess up subsequent runs of VM.
 */

for (var i = 0; i < 3; i+=1) {
  newThread(fun() {
    sleep(2);
  });
}
print "no join"; // script should exit immediately after printing this

__END__
-- noexpect: --
