/**
 * NOTE: skipped during example run because __DIR__ not populated correctly in
 * test files running from ./bin/test_examples
 */
try {
  loadScript("anon_fun.lox");
} catch (LoadError e) {
  print e;
}
loadPath.push(__DIR__);
print requireScript("anon_fun.lox");
print requireScript("anon_fun.lox");

__END__
-- expect: --
<instance LoadError>
3
callbacks:
1
2
3
called!
true
false
