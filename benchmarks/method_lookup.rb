class A
  def meth(); return 'A'; end
end

class B
  def meth(); return 'B'; end
end

class C
  def meth(); return 'C'; end
end

objs = [A.new(),B.new(),C.new()];

i = 0
while (i < 1000000)
  obj = objs[i%3];
  obj.meth();
  i += 1
end
