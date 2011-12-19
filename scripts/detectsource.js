var querystring = require('querystring');
var http = require('http');
var fs = require('fs');

if ( process.argv.length  < 5 )
{
   console.log( "usage : "+process.argv[1]+" channel imagedir minscore" );
   process.exit(-1);
}
channel=process.argv[2];
imagedir=process.argv[3];
minscore=process.argv[4];

var post_data = querystring.stringify({
    'channel': channel,
    'imagedir': imagedir,
    'minscore': minscore
});

// An object of options to indicate where to post to
var post_options = {
      host: 'localhost',
      port: '9999',
      path: '/inputs/detect',
      method: 'POST',
      headers: {
          'Content-Type': 'text/plain',
          'Content-Length': post_data.length
      }
};

// Set up the request
var post_req = http.request(post_options, function(res) {
    res.setEncoding('utf8');
    res.on('data', function (chunk) {
        console.log('Response: ' + chunk);
    });
});

// post the data
post_req.write(post_data);
post_req.end();
