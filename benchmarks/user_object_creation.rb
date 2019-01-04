class MyObject
  def initialize
    super
  end
end
i = 0
a = nil
while i < 1000000
  a = MyObject.new
  i += 1
end
