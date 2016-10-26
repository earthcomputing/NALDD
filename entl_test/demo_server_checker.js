const s_port = 8081;
const s_addr = '172.16.1.45';

var net = require('net');

var client = new net.Socket();

client.connect(s_port, s_addr, function() {

	console.log('Connected');

});

client.on('data', function(data) {

	console.log('Received: ' + data);

});
