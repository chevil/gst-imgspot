var querystring = require('querystring');
var http = require('http');
var fs = require('fs');

if ( process.argv.length  < 4 )
{
   console.log( "usage : "+process.argv[1]+" url|file|device channel" );
   process.exit(-1);
}
url=process.argv[2];
channel=process.argv[3];

var post_data = querystring.stringify({
    'url' : url,
    'channel': channel
});

// An object of options to indicate where to post to
var post_options = {
      host: 'localhost',
      port: '9999',
      path: '/inputs/add',
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
