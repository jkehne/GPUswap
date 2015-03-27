#! /usr/bin/env ruby

# this script detects double allocations by pscnv's memory manager.
#
# make sure that you load pscnv with mm_debug == 1
#
# Usage: ./mm_check.rb [filename]
#
# if you provide a filename, this should contain a copy of the dmesg output.
# Otherwise dmesg is consulted directy.

class Range
    # we expect both Ranges to have exclude_end flag set
    def intersect?(r)
        if (r.begin <=  self.begin)
            return r.end > self.begin
        else
            return r.begin < self.end
        end
    end
    
    def to_s
        sprintf("%04x-%04x", self.begin, self.end)
    end
    
    def <=>(o)
        self.begin <=> o.begin
    end
end

def check_mm_debug
    begin
        mm_debug = File.read("/sys/module/pscnv/parameters/mm_debug").to_i
        puts "WARNING: pscnv.mm_debug=#{mm_debug}" unless mm_debug == 1
    rescue
        puts "WARNING: could not check pscnv.mm_debug parameter. Did you load pscnv?"
    end
end

$mm = {}

def dump_mm
    puts "---------------------------------------------"
    $mm.each do |domain, ranges|
        ranges.sort!
        puts "==== [#{domain}] ===="
        puts ranges.join(",  ")
        puts
    end
    puts "---------------------------------------------"
end

def add_range(name, first, last)
    domain = name.to_sym
    $mm[domain] = [] unless $mm[domain]
    
    # 3rd argument is the exclude_end flag
    r = Range.new(first, last, true)
    
    $mm[domain].each do |old|
        if r.intersect?(old)
            puts "[#{name}] Range #{old} and #{r} intersect!"
            puts
            dump_mm
        end
    end
    
    $mm[domain] << r
end

def remove_range(name, first, last)
    domain = name.to_sym
    fail "unknown mm domain \"#{$1}\"" unless $mm[domain] 
    
    # 3rd argument is the exclude_end flag
    r = Range.new(first, last, true)
    
    unless ($mm[domain].delete(r))
        # we delete something that has never been allocated
        puts "[#{name}] Range #{r} is not in domain!"
        dump_mm
    end
end

def scan_for_errors(f)
    while (ln = f.gets)
    
        case (ln)
        when /MM: \[(?<domain>\w+)\] Allocated size \h+ at (?<start>\h+)-(?<end>\h+)/
            add_range($1, $2.to_i(16) >> 12, $3.to_i(16) >> 12)
        when /MM: \[(?<domain>\w+)\] Freeing node (?<start>\h+)\.\.(?<end>\h+)/
            remove_range($1, $2.to_i(16) >> 12, $3.to_i(16) >> 12)
        end
    end
end

if (filename = ARGV[0])
    puts "reading #{filename}"
    puts
    File.open(filename) { |f| scan_for_errors(f) }
else
    check_mm_debug
    puts "reading dmesg"
    puts
    IO.popen("dmesg") { |dmesg| scan_for_errors(dmesg) }    
end

puts "done"