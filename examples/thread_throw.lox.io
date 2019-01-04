/**
 * Not run in test_examples because the test lib doesn't support saying the
 * script threw an error.
 */
var t = newThread(fun() {
  sleep(1);
  print "Throwing in main";
  Thread.main().throw(Error("OMG")); // should interrupt main's sleep
});
print "Sleeping in main";
sleep(10);
print "done sleeping in main (shouldn't get here!)";

__END__
-- expect: Error --
