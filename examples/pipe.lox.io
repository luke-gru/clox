var ps = IO.pipe();
var rd = ps[0];
var wr = ps[1];

var pid = fork(fun() {
  // in child
  IO.close(wr);
  var msg = IO.read(rd);
  print msg;
  print "Okay, sleeping";
  sleep(2);
});
IO.close(rd);
IO.write(wr, "Message from parent: go to sleep!");
print "I'm waiting...";
waitpid(pid);

__END__
-- noexpect: --
