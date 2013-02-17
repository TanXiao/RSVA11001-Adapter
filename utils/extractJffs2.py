import sys

magic = '\x85\x19\x03\x20\x0C\x00\x00\x00\xB1\xB0\x1E\xE4'

f = open(sys.argv[1],'r');

data = f.read()

magicOffset = data.find(magic)

if magicOffset == -1:
	print('cant find magic')
	quit()


print(len(data))

partitions = '384K(boot),1280K(kernel),5M(rootfs),9M(app),128K(para),128K(init_logo)'

partitions = partitions.split(',')

offset = magicOffset

for partition in partitions:
	
	size,name = partition.split('(')
	name = name[0:len(name)-1]

	modifier = size[-1]
	
	size = int(size[0:len(size)-1])
	
	if modifier == 'K':
		size = size * 1024
	if modifier == 'M':
		size = size * (1024**2)
		
	print((name,hex(size),hex(offset)))
	
	fout = open(name + '.jffs2','w')
	fout.write(data[offset:offset+size])
	fout.close()
	
	offset +=size
	
	
	
	
	
	
	
