#!/usr/bin/ruby

require 'gst'
require 'readline'

raise "Usage: #{$0} videofile imagesdir [algorithm=surf] [tolerance=30]" unless(ARGV.length>=2)

$video=ARGV[0]
$imgdir=ARGV[1]

if ( ARGV.length == 3 )
  $algorithm=ARGV[2]
else
  $algorithm = 'surf'
end

if ( ARGV.length == 4 )
  $tolerance=ARGV[3]
else
  $tolerance = 0.1 
end

Gst::init()
loop=GLib::MainLoop::new(nil,false)
$pip=nil

def start_pipe(video,imgdir,algorithm,tolerance)

  if ( $pip != nil )
    $pip.stop
  end

  cmd=<<-eof
    filesrc location=#{video}  ! decodebin ! ffmpegcolorspace ! imgspot width=320 height=240 imgdir=#{imgdir} algorithm=#{algorithm} tolerance=#{tolerance} ! ffmpegcolorspace ! sdlvideosink
  eof

  $pip=Gst::Parse::launch(cmd)

  bus=$pip.bus
  bus.add_watch() do |bus,message|
    puts message.type.name
    puts message.source
    puts message.structure
    puts
    case message.type
    when Gst::Message::EOS
      STDERR.puts("End of file")
      loop.quit
    when Gst::Message::ERROR
      p ['ERROR!!',pip,bus,message.parse]
      loop.quit
    end
    true
  end

  $pip.play

end

start_pipe($video,$imgdir,$algorithm,$tolerance)


while $imgdir = Readline.readline('', true)
  start_pipe($video,$imgdir,$algorithm,$tolerance)
end
