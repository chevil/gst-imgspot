#!/usr/bin/ruby

require 'gst'
require 'readline'

raise "Usage: #{$0} videodevice imagesdir [algorithm=surf] [minscore=30]" unless(ARGV.length>=2)

$videodev=ARGV[0]
$imgdir=ARGV[1]

if ( ARGV.length == 3 )
  $algorithm=ARGV[2]
else
  $algorithm = 'surf'
end

if ( ARGV.length == 4 )
  $minscore=ARGV[3]
else
  $minscore = 30 
end

Gst::init()
loop=GLib::MainLoop::new(nil,false)
$pip=nil

def start_pipe(videodev,imgdir,algorithm,minscore)

  if ( $pip != nil )
    $pip.stop
  end

  cmd=<<-eof
    v4l2src device=#{videodev}  ! video/x-raw-yuv | ffmpegcolorspace ! imgspot width=320 height=240 algorithm=#{algorithm} imgdir=#{imgdir} minscore=#{minscore} ! ffmpegcolorspace ! autovideosink
  eof

  p cmd

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

start_pipe($videodev,$imgdir,$algorithm,$minscore)


while $imgdir = Readline.readline('', true)
  start_pipe($videodev,$imgdir,$algorithm,$minscore)
end
