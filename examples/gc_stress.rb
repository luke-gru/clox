a = [];
s = "";
m = {};
o = Object.new();
i = 0;
while (i < 1_000_000)
  m = m.merge({});
  s << String(i);
  a = [o];
  i+=1;
end
puts a.inspect
puts m.inspect
4.times do
  GC.start()
end
