/*---------------------------------------------------------------------------------------------
 *  Copyright © 2016-present Earth Computing Corporation. All rights reserved.
 *  Licensed under the MIT License. See LICENSE.txt in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
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
