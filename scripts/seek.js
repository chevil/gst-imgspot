var querystring = require('querystring');
var http = require('http');
var fs = require('fs');

if ( process.argv.length  < 3 )
{
   console.log( "usage : "+process.argv[1]+" seconds" );
   process.exit(-1);
}
seconds=process.argv[2];

var post_data = querystring.stringify({
    'seconds': seconds
});

// An object of options to indicate where to post to
var post_options = {
      host: 'localhost',
      port: '9999',
      path: '/seek',
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
