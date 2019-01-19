var pid1 = Process.fork(fun() { sleep(0.1); });
var pid2 = Process.fork(fun() { sleep(0.2); });
Process.detach(pid1);
Process.waitpid(pid2);
sleep(2);
Process.system("ps -ho pid,state -p ${pid1}"); // should produce nothing

__END__
-- noexpect: --
