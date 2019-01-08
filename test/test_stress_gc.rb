require 'timeout'
TIMEOUT = 30
examples_dir = File.join(File.expand_path("../../", __FILE__), "examples");
fails =  Hash.new { |h,k| h[k] = [] }
timeouts =  Hash.new { |h,k| h[k] = [] }
skip_pats = [/mutex_blocking/, /thread/, /mutex/]
skips = []
run_young = false
run_full = true
run_both = false
Dir.glob(File.join(examples_dir, "*.lox")).each do |file|
  if File.file?(file) && File.extname(file) == ".lox"
    if skip_pats.any? { |pat| pat.match?(file) }
      skips << file
      next
    end
    if run_young
      puts file + " (young)"
      begin
        Timeout.timeout(TIMEOUT) do
          system("./bin/clox -f #{file} --stress-GC=young")
        end
      rescue Timeout::Error
        timeouts[file] << "young"
      else
        if $?.exitstatus != 0
          fails[file] << "young"
        end
      end
    end
    if run_full
      puts file + " (full)"
      begin
        Timeout.timeout(TIMEOUT) do
          system("./bin/clox -f #{file} --stress-GC=full")
        end
      rescue Timeout::Error
        timeouts[file] << "full"
      else
        if $?.exitstatus != 0
          fails[file] << "full"
        end
      end
    end
    if run_both
      puts file + " (both)"
      begin
        Timeout.timeout(TIMEOUT) do
          system("./bin/clox -f #{file} --stress-GC=both")
        end
      rescue Timeout::Error
        timeouts[file] << "both"
      else
        if $?.exitstatus != 0
          fails[file] << "both"
        end
      end
    end
  end
end

if fails.empty? && timeouts.empty?
  puts "None"
else
  require 'pp'
  puts "Failures"
  PP.pp fails
  puts "Timeouts"
  PP.pp timeouts
end
