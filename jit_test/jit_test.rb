require 'timeout'
require 'pp'
dir = File.expand_path("../", __FILE__)
if !File.exist?(dir)
  STDERR.puts "directory #{dir} doesn't exist"
  exit(1)
end
fails =  []
timeouts =  []
skip_pats = [] #[/thread/, /mutex/]
skips = []
TIMEOUT=10
Dir.glob(File.join(dir, "*")).to_a.each do |file|
  if File.file?(file) && File.extname(file) == ".jit"
    if skip_pats.any? { |pat| pat.match?(file) }
      skips << file
      next
    end

    begin
      Timeout.timeout(TIMEOUT) do
        puts "running #{file}"
        system("./bin/clox -f #{file} --enable-jit")
      end
    rescue Timeout::Error
      timeouts << file
    end
    if $?.exitstatus != 0
      fails << file
    end
  end
end

unless skips.empty?
  puts "Skips:"
  PP.pp skips
end
if fails.empty? && timeouts.empty?
  puts "Success"
else
  puts "Failures:"
  PP.pp fails
  puts "Timeouts:"
  PP.pp timeouts
end
exit(fails.size+timeouts.size)
