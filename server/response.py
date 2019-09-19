import os
import socket
import sys

PATH = "server.socket"

# remove the old UNIX socket
try:
	os.unlink(PATH)
except OSError:
	if os.path.exists(PATH):
		raise

# create new UNIX socket
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
#sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
try:
	sock.bind(PATH)
except socket.error as msg:
	print 'Bind failed. Error Code : ' + str(msg[0]) + ' Message ' + msg[1]
	sys.exit()

#Start listening on socket
sock.listen(10)

#now keep talking with the client
while 1:
    #wait to accept a connection - blocking call
	conn, addr = sock.accept()
	data = conn.recv(4096)
	payload = data.split("\r\n\r\n", 1)[1]
	conn.send(payload)
	conn.close()

sock.close()
