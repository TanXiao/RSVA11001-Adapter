hostname = "192.168.12.168"
username = "1111"
password = "1111"
channel = 3

import string

requestTemplate = \
string.Template( "GET /snapshot.html?${username}&${password}&PTZCONTROL=CH${channel}\r\n"\
"User-Agent: requestBuilder.py\r\n"\
"Accept: */*\r\n"\
"Host: ${hostname}\r\n"\
"Connection: Close\r\n\r\n")


import socket

s = socket.socket(socket.AF_INET,socket.SOCK_STREAM)
s.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)

s.connect((hostname,80))

request = requestTemplate.substitute(hostname=hostname,username=username,password=password,channel=str(channel))

print(request)

i = 0

def send(amount = 1):
	global i
	
	if i == len(request):
		print("no more bytes to send");
		return False
	
	bytes = request[i:i+amount]

	s.sendall(bytes)

	
	
	for byte,number in zip(bytes,range(i,i+amount)):
		print ( "'" + byte + "'" + str(ord(byte)) + "," + str(number) + ' / ' + str(len(request)))
	
	i += amount
	
	buffer = s.recv(4096)
	print ("received " + str(len(buffer)) + " bytes")
	
	return True

send(32)	

while send():
	pass
		


